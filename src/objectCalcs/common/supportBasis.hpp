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

// Orthonormal bases for supports: the directions a node may move along (or rotate about) and
// their complement. Shared by every element type (truss, beam) and the GUI support editors.

#include <array>
#include <vector>

namespace FEM::SUPPORT {

  // Orthonormal basis (Gram-Schmidt) of the span of directions. Throws std::invalid_argument
  // when there are more than three, or when a direction is zero or (numerically) depends on
  // the ones before it.
  std::vector<std::array<double, 3>> orthonormalize(std::vector<std::array<double, 3>> directions);

  // Orthonormal basis of the directions perpendicular to an orthonormal basis: the allowed
  // motion of a support from its restrained directions, and the other way round.
  std::vector<std::array<double, 3>> orthogonalComplement(const std::vector<std::array<double, 3>>& basis);

  // direction minus its components along an orthonormal basis.
  std::array<double, 3> componentOutside(std::array<double, 3> direction, const std::vector<std::array<double, 3>>& basis);

} // namespace FEM::SUPPORT end
