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

#include <guiMaterials/iPanel.hpp>
#include <platform/resourceMonitor.hpp>

namespace anaf::GUI {

  // Bottom bar of the main viewport: worker state on the left, resource usage on the right.
  // It shrinks the viewport work area, so the dockspace ends above it.
  class StatusBar : public IPanel {
  private:
    PLATFORM::ResourceMonitor m_monitor;
  public:
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
