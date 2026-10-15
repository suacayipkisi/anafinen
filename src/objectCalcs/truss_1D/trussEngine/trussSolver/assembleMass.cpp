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

#include "trussProperties/element.hpp"
#include <Eigen/SparseCore>
#include <span>
#include <vector>

namespace FEM::TRUSS {

  std::vector<Eigen::Triplet<double>> assembleMass(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    const std::size_t allNodesSize
  ) {
    constexpr std::size_t tripletsPerElement = 21;
    const std::size_t elementCount = elements.size();
    std::vector<Eigen::Triplet<double>> triplets(elementCount * tripletsPerElement);
    const auto nodeCount = allNodesSize;
  }

} // namespace FEM::TRUSS end
