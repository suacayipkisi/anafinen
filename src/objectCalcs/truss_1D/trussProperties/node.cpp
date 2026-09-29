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
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    if (directions.size() > 3) throw std::invalid_argument("A 3D node can have at most three independent motion directions");
    std::vector<std::array<double, 3>> orthonormalBasis;
    for (auto& direction : directions) {
      for (const auto& basis : orthonormalBasis) {
        const double projection = direction[0] * basis[0] + direction[1] * basis[1] + direction[2] * basis[2];
        for (std::size_t axis = 0; axis < 3; ++axis) direction[axis] -= projection * basis[axis];
      }
      const double norm = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
      if (norm <= 1e-12) throw std::invalid_argument("Allowed motion directions must be non-zero and linearly independent");
      for (double& value : direction) value /= norm;
      orthonormalBasis.push_back(direction);
    }
    m_allowedMotionDirections = std::move(orthonormalBasis);
    for (std::size_t axis = 0; axis < m_isMovable.size(); ++axis) {
      std::array<double, 3> residual{};
      residual[axis] = 1.0;
      for (const auto& basis : m_allowedMotionDirections) {
        const double projection = residual[0] * basis[0] + residual[1] * basis[1] + residual[2] * basis[2];
        for (std::size_t component = 0; component < 3; ++component) residual[component] -= projection * basis[component];
      }
      const double residualNorm = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1] + residual[2] * residual[2]);
      m_isMovable[axis] = residualNorm <= 1e-12;
    }
  }

  bool Node::hasInclinedSupport() const {
    return std::ranges::any_of(m_allowedMotionDirections, [](const std::array<double, 3>& direction) {
      return std::ranges::count(direction, 0.0) != 2;
    });
  }

} // namespace FEM::TRUSS end
