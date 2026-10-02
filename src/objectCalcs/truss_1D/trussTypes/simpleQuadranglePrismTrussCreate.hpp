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

#include <bridge/generalStatus.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace FEM::TRUSS {

  // Simple quadrangle prism truss (SQPT): cubeNum[axis] unit cubes along x / y / z, with a
  // bar on every cube edge and both diagonals of every cube face. Node id
  // i + j (nx + 1) + k (nx + 1)(ny + 1) sits at (i, j, k) * edgeLength. Every bar gets
  // materialIndex (into the material list) and areaM2. The snapshot has no supports, loads
  // or results; it is solved like any other model by Truss_Imported_or_Entered.
  // Fails on a zero cube number, length or area, and on grids too large for 32-bit ids.
  std::expected<anaf::BRIDGE::MeshData, std::string> buildSimpleTruss(
    std::array<std::uint32_t, 3> cubeNum,
    double edgeLength,
    double areaM2,
    std::uint32_t materialIndex
  );

} // namespace FEM::TRUSS end
