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

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace anaf::IO {

  // Finite element topologies understood by every reader/writer. The canonical local
  // node order inside anafinen is Gmsh's order; writers for other formats permute it.
  enum class E_ElementType : std::uint8_t {
    Point1,
    Line2,
    Line3,
    Tri3,
    Tri6,
    Quad4,
    Quad8,
    Quad9,
    Tet4,
    Tet10,
    Hex8,
    Hex20,
    Hex27,
    Prism6,
    Prism15,
    Pyramid5,
    Pyramid13,
    Count
  };

  struct ElementTypeInfo {
    E_ElementType type;
    std::string_view name;
    int dimension;
    int nodeCount;
    int gmshType; // Gmsh MSH element type id
    int vtkType;  // VTK cell type id, 0 when VTK has no equivalent
    // vtkFromGmsh[i] is the Gmsh local index of VTK local node i; empty means identical order.
    std::span<const std::uint8_t> vtkFromGmsh;
    // Local node pairs forming the element edges (used for wireframe previews).
    std::span<const std::uint8_t> edges;
  };

  const ElementTypeInfo& elementInfo(E_ElementType type) noexcept;
  std::span<const ElementTypeInfo> allElementTypes() noexcept;

  std::optional<E_ElementType> elementTypeFromGmsh(int gmshType) noexcept;
  std::optional<E_ElementType> elementTypeFromVtk(int vtkType) noexcept;

} // namespace anaf::IO end
