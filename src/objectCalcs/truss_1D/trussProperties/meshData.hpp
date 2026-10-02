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

#include "appliedForce.hpp"
#include "node.hpp"

#include <cstdint>
#include <vector>

namespace FEM::TRUSS {

  // A bar as the snapshot keeps it: what the viewport, the model tree, export and the solver
  // need, without the solver's derived data (length, direction cosines, ...).
  struct RenderElement {
    std::uint32_t node1{};
    std::uint32_t node2{};
    float stress{};               // Pa, tension > 0
    bool isStressExceeded{false};
    std::uint32_t materialID{};   // index into the material list (Gui_Calc_Bridge::allMaterials in the GUI)
    double crossSectionArea{};    // m^2
    bool isWireframe{false};      // edge of an imported surface / volume element: drawn, never solved
  };

  // One truss model: nodes (with their supports and, after a solve, displacements), bars and
  // loads. Front ends publish it as an immutable shared_ptr<const MeshData> snapshot; the
  // solver (solveStatic) takes one and returns a solved copy. Node ids equal positions.
  struct MeshData {
    std::vector<Node> trussNodes;
    std::vector<RenderElement> trussElements;
    std::vector<ForceApplied> appliedForces;
    bool hasResults{false}; // displacements / stresses come from a solve (or a result file)
  };

} // namespace FEM::TRUSS end
