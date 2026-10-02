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

// The one place that reads the hardware: the GUI status bar, the resource monitor and the
// solver referee's log all ask here. Part of anaf_core, so a CLI has it too.

#include <optional>
#include <string>
#include <string_view>

namespace anaf::PLATFORM {

  // Hardware that does not change while the program runs; query once.
  struct SystemInfo {
    std::string cpuName;      // shortened, e.g. "Ryzen 7 7735HS"
    unsigned threads{0};      // logical processors
    double totalRamGiB{0.0};  // physical memory visible to the OS
  };

  SystemInfo querySystemInfo();

  // Physical memory right now. available counts memory the OS can hand out without swapping
  // (Linux MemAvailable, page cache included; Windows ullAvailPhys).
  struct MemoryStatus {
    double totalGiB{0.0};
    double availableGiB{0.0};
  };
  std::optional<MemoryStatus> queryMemory();

  // Dedicated video memory of the GPU named gpuName (GL_RENDERER), without OpenGL:
  // Linux amdgpu sysfs, Windows DXGI. std::nullopt when unknown (e.g. NVIDIA on Linux:
  // the caller tries GL_NVX_gpu_memory_info first).
  std::optional<double> queryVideoMemoryGiB(std::string_view gpuName);

  // Status bar forms: vendor, trademarks, clock speeds, core counts and driver details removed.
  // "AMD Ryzen 7 7735HS with Radeon Graphics"             -> "Ryzen 7 7735HS"
  // "Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz"            -> "Core i7-8700K"
  // "AMD Radeon 680M (radeonsi, rembrandt, ACO, DRM 3.64)" -> "Radeon 680M"
  // "NVIDIA GeForce RTX 3070/PCIe/SSE2"                   -> "GeForce RTX 3070"
  std::string shortCpuName(std::string_view name);
  std::string shortGpuName(std::string_view name);

} // namespace anaf::PLATFORM end
