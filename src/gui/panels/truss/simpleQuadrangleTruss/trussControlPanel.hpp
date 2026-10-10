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

#include <bridge/generalStatus.hpp>
#include <guiMaterials/iPanel.hpp>
#include <panels/dynamicAnalysisInputs.hpp>
#include <truss_1D/trussProperties/appliedForce.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <vector>

namespace anaf::GUI {

  class TrussControlPanel : public IPanel {
  private:
    std::uint32_t m_cubeNumX {10};
    std::uint32_t m_cubeNumY {1};
    std::uint32_t m_cubeNumZ {10};
    std::uint32_t m_materialID{1}; // stable Material ID, resolved to an index when a job starts
    double m_cubeEdgeLength{1.0};
    double m_crossSectionalArea{80.0};
    std::uint32_t m_forceNodeId{126};
    std::array<double, 3> m_forceVector{0.0, 10000.0, 0.0};
    // Loads and supports are panel inputs like the grid: the grid is rebuilt for every preview
    // and solve, and these are put on its nodes (node id -> fixed x / y / z).
    std::vector<FEM::TRUSS::ForceApplied> m_appliedForces;
    std::map<std::uint32_t, std::array<bool, 3>> m_supports;
    std::array<bool, 3> m_fixed{false, false, false};
    std::uint32_t m_lastFixNode{std::numeric_limits<std::uint32_t>::max()};
    DynamicAnalysisInputs m_dynamic; // shown for LoadKind::Dynamic

    void renderSummary(BRIDGE::GuiCalcBridge& bridge);
    void renderGridTab(BRIDGE::GuiCalcBridge& bridge);
    // Builds the grid with the current inputs on the worker and publishes it.
    void startPreview(BRIDGE::GuiCalcBridge& bridge, std::uint32_t materialIndex);
    // dynamic: supports only (the loads stay in the inputs).
    void renderLoadsTab(BRIDGE::GuiCalcBridge& bridge, std::uint32_t currentSelectedNode, bool dynamic);
    // Deformation scale and the run button (constant load kind).
    void renderSolve(BRIDGE::GuiCalcBridge& bridge);
  public:
    ~TrussControlPanel() override = default;
    std::function<void()> onOpenMaterialHandler;

    // Back to the default inputs (loads, supports, grid). The model itself lives in
    // the bridge and is cleared with GuiCalcBridge::resetModel().
    void resetState();
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
