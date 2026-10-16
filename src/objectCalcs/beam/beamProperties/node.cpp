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

#include <cstddef>

namespace FEM::BEAM {

  namespace {
    FEM::SUPPORT::Basis axesOf(const std::array<bool, 3>& isFree) {
      FEM::SUPPORT::Basis axes;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!isFree[axis]) continue;
        std::array<double, 3> unit{};
        unit[axis] = 1.0;
        axes.push_back(unit);
      }
      return axes;
    }
  } // namespace end

  void Node::setAllowedMotionDirections(const FEM::SUPPORT::Basis& directions) {
    m_allowedMotionDirections = FEM::SUPPORT::orthonormalize(directions);
  }

  void Node::setAllowedRotationAxes(const FEM::SUPPORT::Basis& axes) {
    m_allowedRotationAxes = FEM::SUPPORT::orthonormalize(axes);
  }

  void Node::setMovable(const std::array<bool, 3>& isMovable) {
    m_allowedMotionDirections = axesOf(isMovable);
  }

  void Node::setRotatable(const std::array<bool, 3>& isRotatable) {
    m_allowedRotationAxes = axesOf(isRotatable);
  }

} // namespace FEM::BEAM end
