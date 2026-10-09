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

#include <filesystem>
#include <functional>

namespace anaf::GUI {

  // Start panel shown in front of the empty workspace (and from Help > Welcome), so a new user
  // sees where to begin: a solved example, a new beam / truss model, or a file import. Whether it
  // opens at startup is a per-user setting in the user config directory.
  class WelcomePanel : public IPanel {
  public:
    WelcomePanel();
    void onImGuiRender() override;

    void open();

    std::function<void(const std::filesystem::path&)> onOpenExample; // solved example model file
    std::function<void()> onNewBeam;
    std::function<void()> onNewTruss;
    std::function<void()> onImport;

  private:
    bool m_showOnStartup{true};
    bool m_focusRequested{true};
    bool m_autoCenter{true}; // until the user drags it: follows window resizes and its own height
    std::filesystem::path m_examplePath; // empty when the asset is missing
  };

} // namespace anaf::GUI end
