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

// Results along a solved beam element: displacement, axial and shear force, torque and
// bending moments at any point, for diagrams and for the deformed shape between the nodes.
//
// The two-node elements are exact at the nodes for nodal and uniform loads, and the fields
// below are the exact solution between them, not an interpolation:
//   displacement = homogeneous solution through the nodal values (Hermite cubic for
//                  Euler-Bernoulli, the interdependent interpolation for Timoshenko, linear
//                  axial) + the clamped-clamped particular solution of the uniform load q:
//                  axial  q_x x (L - x) / (2 E A)
//                  v / w  q x^2 (L - x)^2 / (24 E I)  [+ q x (L - x) / (2 G As), Timoshenko]
//   forces       = statics of the part [0, x] from the node 1 section forces S0 and q:
//                  N = N0 - q_x x,  Vy = Vy0 - q_y x,  Vz = Vz0 - q_z x,  T = T0,
//                  My = My0 + Vz0 x - q_z x^2 / 2,  Mz = Mz0 - Vy0 x + q_y x^2 / 2
// so dMz/dx = -Vy and dMy/dx = Vz, and the values at x = L are the node 2 section forces.

#include <beam/beamProperties/meshData.hpp>
#include <beam/beamSection/beamSection.hpp>
#include <material/properties.hpp>

#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace FEM::BEAM {

  // State of one cross-section of an element.
  struct SectionState {
    double position{};                         // x from node 1 along the element, m
    std::array<double, 3> location{};          // undeformed global position
    std::array<double, 3> displacement{};      // global, m
    std::array<double, 3> localDisplacement{}; // u, v, w along local x, y, z, m
    // {N, Vy, Vz, T, My, Mz}, local axes, section sign convention (same as BeamElement::sectionForces).
    std::array<double, 6> forces{};
  };

  // {N, Vy, Vz, T, My, Mz} at x from node 1, from the element's section forces and its total
  // local uniform load (the statics formulas above).
  std::array<double, 6> sectionForcesAt(const std::array<double, 12>& sectionForces, const Eigen::Vector3d& localLoad, double x);

  // State at xi = x / L in [0, 1] of element `element` of a solved model (MeshData from
  // solveStatic, hasResults = true) with the material and section lists of the solve.
  // localLoad is the element's total uniform load in local axes, from elementLocalLoads().
  // Throws std::invalid_argument for a model without results, an element, material or
  // section index out of range or xi outside [0, 1].
  SectionState sectionAt(
    const MeshData& solved,
    std::size_t element,
    double xi,
    const Eigen::Vector3d& localLoad,
    std::span<const anaf::MATERIAL::Material> materials,
    std::span<const BeamSection> sections
  );

  // `count` (>= 2) evenly spaced sections of one element, both ends included.
  std::vector<SectionState> sampleElement(
    const MeshData& solved,
    std::size_t element,
    std::size_t count,
    const Eigen::Vector3d& localLoad,
    std::span<const anaf::MATERIAL::Material> materials,
    std::span<const BeamSection> sections
  );

  // sampleElement() for every element; the local loads are computed once.
  std::vector<std::vector<SectionState>> sampleAllElements(
    const MeshData& solved,
    std::size_t countPerElement,
    std::span<const anaf::MATERIAL::Material> materials,
    std::span<const BeamSection> sections
  );

} // namespace FEM::BEAM end
