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

#include "resourceMonitor.hpp"
#include "systemInfo.hpp"

#include <algorithm>

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <string>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace anaf::PLATFORM {

  namespace {
    constexpr auto kSampleInterval = std::chrono::milliseconds(500);

    float percent(const double part, const double whole) {
      return whole > 0.0 ? static_cast<float>(std::clamp(100.0 * part / whole, 0.0, 100.0)) : 0.0f;
    }

#if defined(_WIN32)
    std::uint64_t toTicks(const FILETIME& time) { // 100 ns units
      return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }
#endif
  } // namespace end

  void ResourceMonitor::updateCpu(const std::uint64_t processTicks, const std::uint64_t systemTicks) {
    if (m_lastSystemTicks != 0 && systemTicks > m_lastSystemTicks && processTicks >= m_lastProcessTicks) {
      m_usage.processCpuPercent = percent(static_cast<double>(processTicks - m_lastProcessTicks),
                                          static_cast<double>(systemTicks - m_lastSystemTicks));
    }
    m_lastProcessTicks = processTicks;
    m_lastSystemTicks = systemTicks;
  }

  const ResourceMonitor::Usage& ResourceMonitor::sample() {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastSample.time_since_epoch().count() != 0 && now - m_lastSample < kSampleInterval) return m_usage;
    m_lastSample = now;

#if defined(__linux__)
    // CPU: utime + stime of this process against the sum of all fields of the "cpu" line.
    std::ifstream processStat("/proc/self/stat");
    std::string line;
    if (std::getline(processStat, line)) {
      // The command name may contain spaces; fields start after the last ") ".
      if (const std::size_t commandEnd = line.rfind(") "); commandEnd != std::string::npos) {
        std::istringstream fields(line.substr(commandEnd + 2));
        std::string field;
        std::uint64_t utime = 0, stime = 0;
        for (int index = 0; fields >> field && index <= 12; ++index) {
          if (index == 11) utime = std::stoull(field);
          if (index == 12) stime = std::stoull(field);
        }
        std::ifstream systemStat("/proc/stat");
        std::string cpuLine;
        std::getline(systemStat, cpuLine);
        std::istringstream cpuFields(cpuLine);
        cpuFields >> field; // "cpu"
        std::uint64_t systemTicks = 0, tick = 0;
        while (cpuFields >> tick) systemTicks += tick;
        updateCpu(utime + stime, systemTicks);
      }
    }

    std::ifstream status("/proc/self/status");
    while (std::getline(status, line)) {
      if (line.starts_with("VmRSS:")) {
        std::istringstream fields(line.substr(6));
        double kib = 0.0;
        fields >> kib;
        m_usage.processRamMiB = static_cast<float>(kib / 1024.0);
        break;
      }
    }

    m_usage.available = true;

#elif defined(_WIN32)
    // CPU: process kernel + user time against system kernel (includes idle) + user time,
    // the same "share of all cores" as on Linux.
    FILETIME creation{}, exitTime{}, processKernel{}, processUser{};
    FILETIME systemIdle{}, systemKernel{}, systemUser{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &processKernel, &processUser)
        && GetSystemTimes(&systemIdle, &systemKernel, &systemUser)) {
      updateCpu(toTicks(processKernel) + toTicks(processUser), toTicks(systemKernel) + toTicks(systemUser));
    }

    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
      m_usage.processRamMiB = static_cast<float>(static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0));
    }

    m_usage.available = true;
#endif
    if (const auto memory = queryMemory()) {
      m_usage.systemRamPercent = percent(memory->totalGiB - memory->availableGiB, memory->totalGiB);
    }
    return m_usage;
  }

} // namespace anaf::PLATFORM end
