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

#include "assembleMass.hpp"
#include <log/anaf_info.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FEM::TRUSS {

  // Consistent mass of a 3D bar (Logan): m = rho A L / 6 * [2I I; I 2I]. It does not depend on
  // the direction cosines, so no transformation is needed. Only the upper triangle is given,
  // like the stiffness: 3 + 3 diagonal terms and the 3 diagonal terms of the coupling block.
  Eigen::SparseMatrix<double> assembleMassConsistent(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t nodeCount,
    const ReducedDofs& dofs
  ) {
    const auto nodesOf = [&](const std::size_t index) -> const std::array<std::uint32_t, 2>& { return elements[index].getEleNodes(); };
    auto matrix = assembleReduced(elements.size(), nodeCount, dofs, nodesOf, [&](const std::size_t index, auto&& add) {
      const auto& element = elements[index];
      const auto& nodes = element.getEleNodes();
      const std::uint32_t dof1 = 3 * nodes[0];
      const std::uint32_t dof2 = 3 * nodes[1];
      const std::uint32_t crossRow = std::min(dof1, dof2);
      const std::uint32_t crossCol = std::max(dof1, dof2);
      const double massOverSix = allMaterials[element.getEleProperties()].getDensity()
        * element.getEleCrossSection()
        * element.getEleLength() / 6.0;
      for (std::uint32_t axis = 0; axis < 3; ++axis) {
        add(dof1 + axis, dof1 + axis, 2.0 * massOverSix);
        add(dof2 + axis, dof2 + axis, 2.0 * massOverSix);
        add(crossRow + axis, crossCol + axis, massOverSix);
      }
    });

    anaf::LOG::info("Reduced consistent mass assembled: {} DOFs, {} stored upper entries", dofs.count(), matrix.nonZeros());
    return matrix;
  }

  // Lumped mass of a 3D bar (Logan): m = rho A L / 2 * [I 0; 0 I], half of the element mass on
  // every translational DOF of each node. The nodal block m_n I is isotropic and the support
  // bases are orthonormal, so T^T M T is diagonal with m_n on every reduced DOF of node n: no
  // element coupling and no triplets are needed.
  Eigen::SparseMatrix<double> assembleMassLumped(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t nodeCount,
    const ReducedDofs& dofs
  ) {
    std::vector<double> nodeMass(nodeCount, 0.0);
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(elements.size()); ++index) {
      const auto& element = elements[static_cast<std::size_t>(index)];
      const double massOverTwo = allMaterials[element.getEleProperties()].getDensity()
        * element.getEleCrossSection()
        * element.getEleLength() / 2.0;
      for (const auto node : element.getEleNodes()) {
        #pragma omp atomic
        nodeMass[node] += massOverTwo;
      }
    }

    const std::int32_t count = dofs.count();
    Eigen::SparseMatrix<double> matrix(count, count);
    matrix.resizeNonZeros(count);
    auto* outer = matrix.outerIndexPtr();
    auto* inner = matrix.innerIndexPtr();
    double* values = matrix.valuePtr();
    for (std::int32_t q = 0; q <= count; ++q) outer[q] = q;
    const auto& slots = dofs.nodeDofSlots();
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
      const std::int32_t q = slots[slot];
      if (q < 0) continue;
      inner[q] = q;
      values[q] = nodeMass[slot / 3];
    }

    anaf::LOG::info("Reduced lumped mass assembled: {} DOFs (diagonal)", count);
    return matrix;
  }

} // namespace FEM::TRUSS end
