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

#include "node.hpp"
#include <objectCalcs/common/supportBasis.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace FEM::TRUSS {

  void Node::setMovable(const std::array<bool, 3> isMovable) {
    m_isMovable = isMovable;
    m_allowedMotionDirections.clear();
    for (std::size_t axis = 0; axis < isMovable.size(); ++axis) {
      if (isMovable[axis]) {
        std::array<double, 3> direction{};
        direction[axis] = 1.0;
        m_allowedMotionDirections.push_back(direction);
      }
    }
  }

  // Gram-Schmidt orthonormalization: the directions become an orthonormal basis of the
  // subspace (line, plane or space) the node may move in.
  void Node::setAllowedMotionDirections(std::vector<std::array<double, 3>> directions) {
    auto orthonormalBasis = FEM::SUPPORT::orthonormalize(std::move(directions));
    m_allowedMotionDirections = std::move(orthonormalBasis);
    for (std::size_t axis = 0; axis < m_isMovable.size(); ++axis) {
      std::array<double, 3> unit{};
      unit[axis] = 1.0;
      const auto residual = FEM::SUPPORT::componentOutside(unit, m_allowedMotionDirections);
      m_isMovable[axis] = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1] + residual[2] * residual[2]) <= 1e-12;
    }
  }

  bool Node::hasInclinedSupport() const {
    return std::ranges::any_of(m_allowedMotionDirections, [](const std::array<double, 3>& direction) {
      return std::ranges::count(direction, 0.0) != 2;
    });
  }

} // namespace FEM::TRUSS end
