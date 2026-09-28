// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemInfo.hpp"

#include <algorithm>
#include <cstdint>
#include <regex>
#include <thread>

#if defined(__linux__)
#include <filesystem>
#include <fstream>
#include <sstream>
#elif defined(_WIN32)
#include <windows.h>
#include <dxgi.h>
#endif

namespace anaf::PLATFORM {

  namespace {
    std::string collapseSpaces(std::string text) {
      text = std::regex_replace(text, std::regex(R"(\s+)"), " ");
      const auto first = text.find_first_not_of(' ');
      if (first == std::string::npos) return {};
      return text.substr(first, text.find_last_not_of(' ') - first + 1);
    }

    std::string removeAll(std::string text, const char* pattern) {
      return std::regex_replace(text, std::regex(pattern, std::regex::icase), " ");
    }

#if defined(_WIN32)
    std::string toUtf8(const std::wstring_view text) {
      if (text.empty()) return {};
      const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
      std::string result(static_cast<std::size_t>(size), '\0');
      WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
      return result;
    }
#endif
  } // namespace end

  std::string shortCpuName(const std::string_view name) {
    std::string text(name);
    text = removeAll(text, R"(\((R|TM|tm)\))");
    text = removeAll(text, R"(\s+with\s.*$)");     // "with Radeon Graphics"
    text = removeAll(text, R"(\s*@.*$)");          // "@ 3.70GHz"
    text = removeAll(text, R"(\b\d+-Cores?\b)");
    text = removeAll(text, R"(\b(CPU|Processor|APU)\b)");
    text = removeAll(text, R"(^\s*(AMD|Intel|Genuine Intel|\d+(st|nd|rd|th) Gen Intel)\s)");
    text = collapseSpaces(text);
    return text.empty() ? std::string(name) : text;
  }

  std::string shortGpuName(const std::string_view name) {
    std::string text(name);
    if (const auto slash = text.find('/'); slash != std::string::npos) text.resize(slash);     // "/PCIe/SSE2"
    if (const auto paren = text.find(" ("); paren != std::string::npos) text.resize(paren);  // driver details
    text = removeAll(text, R"(\((R|TM|tm)\))");
    text = removeAll(text, R"(^\s*(Mesa\s+)?(AMD|NVIDIA|Intel|ATI)\s)");
    text = removeAll(text, R"(\b(GPU|Series)\b)");
    text = collapseSpaces(text);
    return text.empty() ? std::string(name) : text;
  }

  SystemInfo querySystemInfo() {
    SystemInfo info;
    info.threads = std::thread::hardware_concurrency();

#if defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
      if (line.starts_with("model name")) {
        if (const auto colon = line.find(':'); colon != std::string::npos) info.cpuName = line.substr(colon + 1);
        break;
      }
    }
    std::ifstream meminfo("/proc/meminfo");
    while (std::getline(meminfo, line)) {
      if (line.starts_with("MemTotal:")) {
        std::istringstream fields(line.substr(9));
        double kib = 0.0;
        fields >> kib;
        info.totalRamGiB = kib / (1024.0 * 1024.0);
        break;
      }
    }
#elif defined(_WIN32)
    wchar_t buffer[256]{};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, buffer, &bytes) == ERROR_SUCCESS) {
      info.cpuName = toUtf8(buffer);
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) info.totalRamGiB = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
#endif

    info.cpuName = info.cpuName.empty() ? std::string("CPU") : shortCpuName(info.cpuName);
    return info;
  }

  std::optional<double> queryVideoMemoryGiB([[maybe_unused]] const std::string_view gpuName) {
#if defined(__linux__)
    // amdgpu exposes VRAM per card; with several cards the largest is the discrete GPU.
    std::uint64_t largest = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
      std::ifstream file(entry.path() / "device" / "mem_info_vram_total");
      std::uint64_t bytes = 0;
      if (file >> bytes) largest = std::max(largest, bytes);
    }
    if (largest > 0) return static_cast<double>(largest) / (1024.0 * 1024.0 * 1024.0);
#elif defined(_WIN32)
    // DXGI covers every vendor. Prefer the adapter whose name matches the GL renderer,
    // else the hardware adapter with the most dedicated memory.
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
      const std::string wanted = shortGpuName(gpuName);
      std::uint64_t matched = 0, largest = 0;
      IDXGIAdapter1* adapter = nullptr;
      for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
          const auto bytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory);
          largest = std::max(largest, bytes);
          if (!wanted.empty() && toUtf8(desc.Description).find(wanted) != std::string::npos) matched = bytes;
        }
        adapter->Release();
      }
      factory->Release();
      const std::uint64_t bytes = matched > 0 ? matched : largest;
      if (bytes > 0) return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    }
#endif
    return std::nullopt;
  }

} // namespace anaf::PLATFORM end
