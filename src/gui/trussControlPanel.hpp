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

#pragma once

#include "iPanel.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>

#include "../trussEngine/trussSolver.hpp"

class TrussControlPanel : public IPanel {
private:
    std::uint32_t m_cubeNumX {5};
    std::uint32_t m_cubeNumY {1};
    std::uint32_t m_cubeNumZ {1};
    std::uint32_t m_type{0};
    double m_cubeEdgeLength{1.0};
    double m_crossSectionalArea{80.0};
public:
    void onImGuiRender() override {
        ImGui::Begin("Truss(1D Element) Analysis Set", &isOpen);

        ImGui::Text("Truss Parameters");
        ImGui::Separator();

        if (ImGui::BeginTable("TrussParamsTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        
            // Sutun oranlari: Sol sutun %60, Sag girdi sutunu %40
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.4f);

            // Satir 1: Cube Num X
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding(); // Metni input kutusuyla dikeyde hizalar
            ImGui::Text("Cube Num X (N)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN); // Kutuyu sutunun tamamina yayar
            ImGui::InputScalar("##cube_x", ImGuiDataType_U32, &m_cubeNumX);

            // Satir 2: Cube Num Y
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cube Num Y (N)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##cube_y", ImGuiDataType_U32, &m_cubeNumY);

            // Satir 3: Cube Num Z
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cube Num Z (N)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##cube_z", ImGuiDataType_U32, &m_cubeNumZ);

            // Satir 4: Material Type
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Material Type");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##mat_type", ImGuiDataType_U32, &m_type);

            // Satir 5: Edge Length
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cube Edge Length (m)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##edge_length", &m_cubeEdgeLength, 0.0, 0.0, "%.2f");

            // Satir 6: Cross Section Area
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cross Sectional Area");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##cross_area", &m_crossSectionalArea, 0.0, 0.0, "%.2f");

            ImGui::EndTable();
        }

        if (ImGui::Button("Run Solver for Truss", ImVec2(-1, 32))){
            trussCalculator(m_cubeNumX, m_cubeNumY, m_cubeNumZ, m_cubeEdgeLength, m_crossSectionalArea, m_type);
        }

        ImGui::End();

    }

    std::array<std::uint32_t, 3> getTrussSize() const {return {m_cubeNumX, m_cubeNumY, m_cubeNumZ};};
    std::uint32_t getType() const {return m_type;}
    double geElementLength() const {return m_cubeEdgeLength;}
    double getelementArea() const {return m_crossSectionalArea;}
    
};
