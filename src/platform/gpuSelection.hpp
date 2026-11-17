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

#include <cstdint>
#include <string>
#include <vector>

// GPU choice on hybrid-graphics machines (laptops with an integrated and a discrete GPU).
//
// The OpenGL driver is picked when the GL / EGL library loads and the context is created, so the
// choice is applied once, before glfwInit(), and a change takes effect at the next start.
//
//   Linux    PRIME render offload through the environment, as switcheroo-control / GNOME's
//            "Launch using Discrete Graphics" set it:
//              NVIDIA proprietary driver  __NV_PRIME_RENDER_OFFLOAD=1, __GLX_VENDOR_LIBRARY_NAME=nvidia,
//                                         __VK_LAYER_NV_optimus=NVIDIA_only
//              Mesa drivers (amdgpu, i915, xe, nouveau, radeon)  DRI_PRIME=pci-<domain>_<bus>_<slot>_<fn>
//            The display GPU (boot_vga) counts as the integrated one, the other as discrete.
//   Windows  the NvOptimusEnablement / AmdPowerXpressRequestHighPerformance exports of the
//            executable, read by the NVIDIA / AMD drivers when the OpenGL driver loads; 1 asks
//            for the discrete GPU, 0 leaves it to Windows (Settings > Display > Graphics).
//
// GUI thread, before any OpenGL call.
namespace anaf::PLATFORM {

  enum class E_GpuPreference : std::uint8_t {
    HighPerformance, // discrete GPU
    PowerSaving,     // integrated GPU (the one driving the displays)
    System           // whatever the launcher / the OS / the driver profile picks
  };

  struct GpuDevice {
    std::string name;    // from pci.ids when found, else the vendor and the PCI device id
    std::string pciSlot; // "0000:01:00.0"
    std::string driver;  // kernel driver: "nvidia", "amdgpu", "i915", ...
    bool display{false}; // boot_vga: drives the displays (the integrated GPU on hybrid laptops)
  };

  // GPUs with a DRM card node (Linux); empty elsewhere.
  std::vector<GpuDevice> listGpus();

  // Applies the preference for the OpenGL context that is created next and returns a one-line
  // summary for the log. Call before glfwInit().
  std::string applyGpuPreference(E_GpuPreference preference);

  // The preference applied at startup (the running OpenGL context follows it); HighPerformance
  // before applyGpuPreference() ran.
  E_GpuPreference appliedGpuPreference();

} // namespace anaf::PLATFORM end
