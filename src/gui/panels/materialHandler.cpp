// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include "materialHandler.hpp"

#include "generalStatus.hpp"
#include "imgui.h"

namespace anaf::GUI {

  void MaterialHandler::onImGuiRender() {
    if (!isOpen) return;
    BRIDGE::Gui_Calc_Bridge& bridge = BRIDGE::buildBridge();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(
      viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
      viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
    );

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 220.0f), ImGuiCond_FirstUseEver);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Material Handler");

    ImGui::Text("Coming Soon");

    if (ImGui::Button("Close")) {
      isOpen = false;
    }
    
    ImGui::End();
  }

} // namespace anaf::GUI end
