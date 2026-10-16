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
#include <Eigen/SparseCore>

namespace FEM::TRUSS {

  // Consistent mass of a 3D bar (Logan): m = rho A L / 6 * [2I I; I 2I]. It does not depend on
  // the direction cosines, so no transformation is needed. Only the upper triangle is written,
  // like the stiffness: 3 + 3 diagonal terms and the 3 diagonal terms of the coupling block.
  std::vector<Eigen::Triplet<double>> assembleMassConsistent(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t allNodesSize
  ) {
    constexpr std::size_t tripletsPerElement = 9;
    const std::size_t elementCount = elements.size();
    std::vector<Eigen::Triplet<double>> triplets(elementCount * tripletsPerElement);
    const auto nodeCount = allNodesSize;

    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(elementCount); ++index) {
      const auto& element = elements[index];
      const auto& nodes = element.getEleNodes();
      const std::uint32_t dof1 = 3 * nodes[0];
      const std::uint32_t dof2 = 3 * nodes[1];
      const std::uint32_t crossRow = std::min(dof1, dof2);
      const std::uint32_t crossCol = std::max(dof1, dof2);
      const double massOverSix = allMaterials[element.getEleProperties()].getDensity()
        * element.getEleCrossSection()
        * element.getEleLength() / 6.0;
      std::size_t output = static_cast<std::size_t>(index) * tripletsPerElement;

      for (std::size_t axis = 0; axis < 3; ++axis) {
        triplets[output++] = {
          static_cast<int>(dof1 + axis), static_cast<int>(dof1 + axis), 2.0 * massOverSix
        };
        triplets[output++] = {
          static_cast<int>(dof2 + axis), static_cast<int>(dof2 + axis), 2.0 * massOverSix
        };
        triplets[output++] = {
          static_cast<int>(crossRow + axis), static_cast<int>(crossCol + axis), massOverSix
        };
      }
    }

    anaf::LOG::info(
      "Global Mass Matrix Created, size: {}x{}",
      nodeCount * 3, nodeCount * 3
    );

    return triplets;
  }

  // Lumped mass of a 3D bar (Logan): m = rho A L / 2 * [I 0; 0 I]. Half of the element mass goes
  // to each node on every translational DOF, so the matrix is diagonal: 3 + 3 terms per element.
  // Duplicate triplets of shared nodes are summed by setFromTriplets.
  std::vector<Eigen::Triplet<double>> assembleMassLumped(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t allNodesSize
  ) {
    constexpr std::size_t tripletsPerElement = 6;
    const std::size_t elementCount = elements.size();
    std::vector<Eigen::Triplet<double>> triplets(elementCount * tripletsPerElement);
    const auto nodeCount = allNodesSize;

    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(elementCount); ++index) {
      const auto& element = elements[index];
      const auto& nodes = element.getEleNodes();
      const std::uint32_t dof1 = 3 * nodes[0];
      const std::uint32_t dof2 = 3 * nodes[1];
      const double massOverTwo = allMaterials[element.getEleProperties()].getDensity()
        * element.getEleCrossSection()
        * element.getEleLength() / 2.0;
      std::size_t output = static_cast<std::size_t>(index) * tripletsPerElement;

      for (std::size_t axis = 0; axis < 3; ++axis) {
        triplets[output++] = {
          static_cast<int>(dof1 + axis), static_cast<int>(dof1 + axis), massOverTwo
        };
        triplets[output++] = {
          static_cast<int>(dof2 + axis), static_cast<int>(dof2 + axis), massOverTwo
        };
      }
    }

    anaf::LOG::info(
      "Global Lumped Mass Matrix Created, size: {}x{}",
      nodeCount * 3, nodeCount * 3
    );

    return triplets;
  }

} // namespace FEM::TRUSS end
