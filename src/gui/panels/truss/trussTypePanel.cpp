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

#include "imgui.h"

#include "trussTypePanel.hpp"
#include "../../../log/anaf_info.hpp"

namespace anaf::GUI {
    void TrussSelector::onImGuiRender() {
        ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_FirstUseEver);
        
        ImGui::Begin("Truss Build Type Select");

        
        if (ImGui::BeginCombo("Truss Type", m_types[static_cast<int>(m_trussType)].data())) {
        for (int i = 0; i < static_cast<int>(m_types.size()); ++i) {
            if (ImGui::Selectable(m_types[i].data(), m_trussType == static_cast<TrussTypes>(i))) {
                m_trussType = static_cast<TrussTypes>(i);
            }
        }
        ImGui::EndCombo();
    }

        if(ImGui::Button("Select", ImVec2(-1, 32))) {
            anaf::LOG::info("selected");
            if (onSelected) {
                onSelected(m_trussType);
            }
            isOpen = false;

        }

        ImGui::End();
    }
} // namespace anaf::GUI end




