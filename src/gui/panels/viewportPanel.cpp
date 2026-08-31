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

#include "viewportPanel.hpp"
#include "imgui.h"

namespace anaf::GUI {

    void ViewportPanel:: onImGuiRender() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("3D Simulation Viewport", &isOpen);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

        if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {

            std::uint32_t w = static_cast<uint32_t>(viewportPanelSize.x);
            std::uint32_t h = static_cast<uint32_t>(viewportPanelSize.y);

            m_fbo_->resize(w, h);
        }

        std::uint32_t textureID = m_fbo_->getTextureID();
        //std::cout << "[DEBUG] Viewport Texture ID: " << textureID << std::endl;
        if (textureID > 0) {
            ImGui::Image(
                static_cast<ImTextureID>(textureID),
                viewportPanelSize, 
                ImVec2(0, 1), 
                ImVec2(1, 0)
            );
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace anaf::GUI end