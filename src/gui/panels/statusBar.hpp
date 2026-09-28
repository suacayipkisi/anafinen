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

#include <platform/resourceMonitor.hpp>

#include <string>

namespace anaf::GUI {

  // One status line: worker state and a short hardware summary on the left, live resource
  // usage on the right. Drawn by the console as its footer, so it spans the console's width.
  class StatusBar {
  private:
    PLATFORM::ResourceMonitor m_monitor;
    std::string m_hardware; // built once on the first render (needs the GL context for the GPU)

    void queryHardware();
  public:
    // Call inside a window, on the GUI thread with the GL context current.
    void render();
    // Height to reserve for render(), including the separator above it.
    static float height();
  };

} // namespace anaf::GUI end
