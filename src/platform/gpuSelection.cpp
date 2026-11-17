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

#include "gpuSelection.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>
#include <system_error>

#ifdef _WIN32
// Read by the NVIDIA (Optimus) and AMD (PowerXpress) drivers from the executable when the OpenGL
// driver loads, i.e. at the first context creation: applyGpuPreference() sets them before
// glfwInit(). Only the GUI executable defines them (anafinen-cli has its own, never creates a context).
extern "C" {
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;   // NOLINT(readability-identifier-naming)
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;     // NOLINT(readability-identifier-naming)
}
#endif

namespace anaf::PLATFORM {

  namespace {

#ifdef __linux__
    std::string readFirstLine(const std::filesystem::path& path) {
      std::ifstream file(path);
      std::string line;
      if (file) std::getline(file, line);
      return line;
    }

    // "0x10de" -> "10de"
    std::string hexId(const std::filesystem::path& path) {
      std::string value = readFirstLine(path);
      if (value.starts_with("0x")) value.erase(0, 2);
      return value;
    }

    std::string vendorName(std::string_view vendorId) {
      if (vendorId == "10de") return "NVIDIA";
      if (vendorId == "1002") return "AMD";
      if (vendorId == "8086") return "Intel";
      return std::string(vendorId);
    }

    // "<vendor> <device>" from pci.ids (hwdata / pciutils), empty when the file or the entry is missing.
    //   10de  NVIDIA Corporation
    //   \t2d59  GB206M [GeForce RTX 5060 Max-Q / Mobile]
    std::string pciIdsName(std::string_view vendorId, std::string_view deviceId) {
      for (const char* candidate : {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"}) {
        std::ifstream file(candidate);
        if (!file) continue;
        std::string line;
        std::string vendor;
        bool inVendor = false;
        while (std::getline(file, line)) {
          if (line.empty() || line.front() == '#') continue;
          if (line.front() != '\t') {
            if (inVendor) break; // the next vendor: the device is not listed
            if (line.starts_with(vendorId) && line.size() > vendorId.size() + 2) {
              inVendor = true;
              vendor = line.substr(vendorId.size() + 2);
            }
            continue;
          }
          if (inVendor && line.size() > 1 && line[1] != '\t' && std::string_view(line).substr(1).starts_with(deviceId) &&
              line.size() > deviceId.size() + 3) {
            return vendor + " " + line.substr(deviceId.size() + 3);
          }
        }
        return {};
      }
      return {};
    }

    // DRI_PRIME's PCI tag: "0000:06:00.0" -> "pci-0000_06_00_0"
    std::string driPrimeTag(std::string pciSlot) {
      std::ranges::replace(pciSlot, ':', '_');
      std::ranges::replace(pciSlot, '.', '_');
      return "pci-" + pciSlot;
    }

    void unsetIfEquals(const char* name, std::string_view value) {
      const char* current = std::getenv(name);
      if (current != nullptr && value == current) unsetenv(name);
    }
#endif // __linux__

    E_GpuPreference g_appliedPreference{E_GpuPreference::HighPerformance};

  } // namespace end

  E_GpuPreference appliedGpuPreference() { return g_appliedPreference; }

  std::vector<GpuDevice> listGpus() {
    std::vector<GpuDevice> gpus;
#ifdef __linux__
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
      const std::string node = entry.path().filename().string();
      // card0, card1, ...: one per GPU (card0-eDP-1 and the like are its connectors)
      if (!node.starts_with("card") || node.size() == 4 ||
          !std::all_of(node.begin() + 4, node.end(), [](const char c) { return c >= '0' && c <= '9'; })) {
        continue;
      }
      const std::filesystem::path device = entry.path() / "device";
      const std::string vendorId = hexId(device / "vendor");
      if (vendorId.empty()) continue; // not a PCI device (e.g. a virtual or platform display)

      GpuDevice gpu;
      gpu.pciSlot = std::filesystem::canonical(device, ec).filename().string();
      if (ec) continue;
      gpu.driver = std::filesystem::read_symlink(device / "driver", ec).filename().string();
      gpu.display = readFirstLine(device / "boot_vga") == "1";
      const std::string deviceId = hexId(device / "device");
      gpu.name = pciIdsName(vendorId, deviceId);
      if (gpu.name.empty()) gpu.name = std::format("{} GPU [{}:{}]", vendorName(vendorId), vendorId, deviceId);

      if (std::ranges::none_of(gpus, [&](const GpuDevice& known) { return known.pciSlot == gpu.pciSlot; })) {
        gpus.push_back(std::move(gpu));
      }
    }
    std::ranges::sort(gpus, {}, &GpuDevice::pciSlot);
#endif
    return gpus;
  }

  std::string applyGpuPreference(const E_GpuPreference preference) {
    g_appliedPreference = preference;
#ifdef _WIN32
    const bool discrete = preference == E_GpuPreference::HighPerformance;
    NvOptimusEnablement = discrete ? 1ul : 0ul;
    AmdPowerXpressRequestHighPerformance = discrete ? 1 : 0;
    switch (preference) {
      case E_GpuPreference::HighPerformance: return "GPU preference: high performance (NVIDIA Optimus / AMD PowerXpress request)";
      case E_GpuPreference::PowerSaving: return "GPU preference: power saving (no discrete GPU request; Windows graphics settings decide)";
      case E_GpuPreference::System: return "GPU preference: system default (Windows graphics settings decide)";
    }
    return {};
#elif defined(__linux__)
    if (preference == E_GpuPreference::System) return "GPU preference: system default (environment left as it is)";

    const std::vector<GpuDevice> gpus = listGpus();
    if (gpus.size() < 2) return "GPU preference: one GPU found, nothing to choose";

    // The display GPU is the integrated one on hybrid laptops; the discrete one is any other.
    const auto target = std::ranges::find_if(gpus, [&](const GpuDevice& gpu) {
      return preference == E_GpuPreference::PowerSaving ? gpu.display : !gpu.display;
    });
    if (target == gpus.end()) return "GPU preference: no matching GPU found, environment left as it is";

    // Overrides of a launcher (e.g. GNOME's "Launch using Discrete Graphics") give way to the setting.
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetIfEquals("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
    unsetenv("__VK_LAYER_NV_optimus");
    unsetenv("DRI_PRIME");

    if (target->driver == "nvidia") {
      // Only needed to move off the display GPU; the NVIDIA GPU driving the displays needs nothing.
      if (!target->display) {
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
        setenv("__VK_LAYER_NV_optimus", "NVIDIA_only", 1);
      }
    } else {
      setenv("DRI_PRIME", driPrimeTag(target->pciSlot).c_str(), 1);
    }
    return std::format("GPU preference: {} -> {} ({}, {})", preference == E_GpuPreference::HighPerformance ? "high performance" : "power saving",
                       target->name, target->pciSlot, target->driver.empty() ? "no driver" : target->driver);
#else
    (void)preference;
    return "GPU preference: not supported on this platform";
#endif
  }

} // namespace anaf::PLATFORM end
