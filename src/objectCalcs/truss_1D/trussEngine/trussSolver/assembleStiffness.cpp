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
#include <log/anaf_info.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace FEM::TRUSS {

  Eigen::SparseMatrix<double> assembleStiffness(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t nodeCount,
    const ReducedDofs& dofs
  ) {
    // Element e in global DOFs: the 3 x 3 block k = (AE / L) c c^T on both node diagonals
    // (upper triangles) and -k between them, 21 upper entries.
    const auto nodesOf = [&](const std::size_t index) -> const std::array<std::uint32_t, 2>& { return elements[index].getEleNodes(); };
    auto matrix = assembleReduced(elements.size(), nodeCount, dofs, nodesOf, [&](const std::size_t index, auto&& add) {
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

      for (std::uint32_t row = 0; row < 3; ++row) {
        for (std::uint32_t col = row; col < 3; ++col) {
          const double value = cosines[row] * cosines[col] * aeOverLength;
          add(dof1 + row, dof1 + col, value);
          add(dof2 + row, dof2 + col, value);
        }
      }
      for (std::uint32_t row = 0; row < 3; ++row) {
        for (std::uint32_t col = 0; col < 3; ++col) {
          add(crossRow + row, crossCol + col, -cosines[row] * cosines[col] * aeOverLength);
        }
      }
    });

    anaf::LOG::info("Reduced stiffness assembled: {} DOFs, {} stored upper entries", dofs.count(), matrix.nonZeros());
    return matrix;
  }

} // namespace FEM::TRUSS end
