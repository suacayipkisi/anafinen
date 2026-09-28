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

#pragma once

#include <chrono>
#include <cstdint>

namespace anaf::PLATFORM {

  // Process CPU / RAM and system RAM usage, sampled at most every 500 ms.
  // Linux: /proc. Windows: GetProcessTimes / GetSystemTimes, GetProcessMemoryInfo,
  // GlobalMemoryStatusEx. Other platforms report nothing (available == false).
  class ResourceMonitor {
  public:
    struct Usage {
      bool available{false};
      float processCpuPercent{0.0f}; // share of all cores (100 % = every core busy)
      float processRamMiB{0.0f};     // resident set / working set
      float systemRamPercent{0.0f};  // used physical memory of the whole machine
    };

    // Refreshes the values when the sampling interval has passed; cheap otherwise.
    const Usage& sample();

  private:
    Usage m_usage{};
    std::chrono::steady_clock::time_point m_lastSample{};
    std::uint64_t m_lastProcessTicks{0};
    std::uint64_t m_lastSystemTicks{0};

    void updateCpu(std::uint64_t processTicks, std::uint64_t systemTicks);
  };

} // namespace anaf::PLATFORM end
