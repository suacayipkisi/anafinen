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

#include "meshData.hpp"

#include <cstdint>

namespace FEM::TRUSS {

  // Edits shared by every front end (GUI model editor, CLI): node ids stay equal to positions.

  // Results no longer match an edited model: displacements and stresses are zeroed and
  // hasResults is cleared.
  void dropResults(MeshData& mesh);

  // Removes node k with its bars and loads; later ids move down by one (ids = positions: the
  // solver and the viewport index nodes by id). The other nodes keep their supports.
  void deleteNode(MeshData& mesh, std::uint32_t k);

} // namespace FEM::TRUSS end
