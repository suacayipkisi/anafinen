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
#include <limits>
#include <utility>
#include <vector>

namespace FEM::BEAM {

  // Edits shared by every front end (GUI frame editor, CLI): node ids stay equal to positions,
  // distributed loads follow their element.

  // Results no longer match an edited model: displacements, rotations, section forces and
  // stresses are zeroed and hasResults is cleared.
  void dropResults(MeshData& mesh);

  // Removes the elements for which drop(element) is true; distributed loads follow their
  // element (and disappear with it).
  template <typename Predicate>
  void removeElements(MeshData& mesh, Predicate&& drop) {
    constexpr std::uint32_t removedIndex = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> newIndex(mesh.elements.size(), removedIndex);
    std::vector<BeamElement> kept;
    for (std::size_t i = 0; i < mesh.elements.size(); ++i) {
      if (drop(std::as_const(mesh.elements[i]))) continue;
      newIndex[i] = static_cast<std::uint32_t>(kept.size());
      kept.push_back(mesh.elements[i]);
    }
    mesh.elements = std::move(kept);
    std::erase_if(mesh.distributedLoads, [&](const DistributedLoad& load) {
      return load.element >= newIndex.size() || newIndex[load.element] == removedIndex;
    });
    for (auto& load : mesh.distributedLoads) load.element = newIndex[load.element];
  }

  // Removes node k with its elements and loads; later ids move down by one. The other nodes
  // keep their supports.
  void deleteNode(MeshData& mesh, std::uint32_t k);

} // namespace FEM::BEAM end
