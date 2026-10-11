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

#include "assembleStiffness.hpp"
#include <Eigen/SparseCore>

namespace FEM::TRUSS {

  std::vector<Eigen::Triplet<double>> assembleStiffness(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t allNodesSize
  ) {
    constexpr std::size_t tripletsPerElement = 21;
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
      const double aeOverLength = element.getEleCrossSection()
        * allMaterials[element.getEleProperties()].getElasticityModulus()
        / element.getEleLength();
      const auto& cosines = element.getEleCosines();
      std::size_t output = static_cast<std::size_t>(index) * tripletsPerElement;

      for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = row; col < 3; ++col) {
          const double value = cosines[row] * cosines[col] * aeOverLength;
          triplets[output++] = {
            static_cast<int>(dof1 + row), static_cast<int>(dof1 + col), value
          };
          triplets[output++] = {
            static_cast<int>(dof2 + row), static_cast<int>(dof2 + col), value
          };
        }
      }
      for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
          const double value = cosines[row] * cosines[col] * aeOverLength;
          triplets[output++] = {
            static_cast<int>(crossRow + row), static_cast<int>(crossCol + col), -value
          };
        }
      }
    }

    anaf::LOG::info(
      "Global Stiffness Matrix Created, size: {}x{}",
      nodeCount * 3, nodeCount * 3
    );

    return triplets;
  }

} // namespace FEM::TRUSS end
