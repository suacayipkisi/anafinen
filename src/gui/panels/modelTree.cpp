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

#include "modelTree.hpp"

#include "imgui.h"

namespace anaf::GUI {

    
    void ModelTree::onImGuiRender() {
        ImGui::Begin("Model Tree");
        if (ImGui::TreeNode("Root Assembly")) {
            if (ImGui::TreeNode("1D Truss Subsystem")) {
                ImGui::BulletText("Element 0: [Node 0 -> Node 1]");
                ImGui::BulletText("Element 1: [Node 1 -> Node 2]");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Boundary Conditions")) {
                ImGui::BulletText("Fixed: Node 0 (UX, UY, UZ)");
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::End();
    }


} // namespace anaf::GUI end
