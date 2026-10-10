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

#include <GLFW/glfw3.h>

#include <functional>
#include <utility>
#include <vector>

namespace anaf::GUI {

  // One row of the Panels menu: reopens (or closes) a panel the user can close.
  struct PanelMenuEntry {
    const char* label{nullptr};
    IPanel* panel{nullptr};
    std::function<bool()> isAvailable; // empty = always; false = the active analysis does not use it
    const char* unavailableHint{nullptr};
  };

  class MainDockSpaceHost : public IPanel {
  private:
    GLFWwindow* m_window;
    std::vector<PanelMenuEntry> m_panelMenu;

    void renderPanelsMenu();
    void renderSettingsMenu(); // Settings > Theme

  public:
    std::function<void()> onSelectTruss; // Analyze > Truss (1D Element)
    std::function<void()> onSelectBeam;  // Analyze > Beam / Frame (3D Element)
    std::function<void()> onImportMesh;
    std::function<void()> onExportResults;
    std::function<void()> onShowAbout;
    std::function<void()> onShowWelcome; // Help > Welcome

    explicit MainDockSpaceHost(GLFWwindow* window) : m_window(window) {}

    void addPanelMenuEntry(PanelMenuEntry entry) { m_panelMenu.push_back(std::move(entry)); }

    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
