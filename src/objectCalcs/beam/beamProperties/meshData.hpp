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

#include "element.hpp"
#include "loads.hpp"
#include "node.hpp"

#include <array>
#include <vector>

namespace FEM::BEAM {

  // One beam / frame model. solveStatic() takes one and returns a solved copy (node
  // displacements and rotations, element section forces). Node ids equal positions.
  struct MeshData {
    std::vector<Node> nodes;
    std::vector<BeamElement> elements;
    std::vector<NodalLoad> nodalLoads;
    std::vector<DistributedLoad> distributedLoads;
    // Self weight: density * area * gravity per unit length on every element.
    // A zero vector switches it off.
    std::array<double, 3> gravity{0.0, -9.80665, 0.0};
    bool hasResults{false};
  };

  // Sets one formulation on every element; the caller then changes single elements
  // (e.g. all Timoshenko, one Euler-Bernoulli).
  inline void setFormulationForAll(MeshData& mesh, const E_Formulation formulation) {
    for (auto& element : mesh.elements) element.formulation = formulation;
  }

} // namespace FEM::BEAM end
