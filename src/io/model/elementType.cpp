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

#include "elementType.hpp"

#include <array>
#include <cstddef>

namespace anaf::IO {

  namespace {

    // Node order references:
    //   Gmsh: https://gmsh.info/doc/texinfo/gmsh.html#Node-ordering
    //   VTK:  vtkQuadraticTetra / vtkQuadraticHexahedron / vtkQuadraticWedge / ... class docs
    constexpr std::array<std::uint8_t, 10> tet10Vtk{0, 1, 2, 3, 4, 5, 6, 7, 9, 8};
    constexpr std::array<std::uint8_t, 20> hex20Vtk{0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 9, 16, 18, 19, 17, 10, 12, 14, 15};
    constexpr std::array<std::uint8_t, 27> hex27Vtk{0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 9, 16, 18, 19, 17, 10, 12, 14, 15,
                                                     22, 23, 21, 24, 20, 25, 26};
    constexpr std::array<std::uint8_t, 15> prism15Vtk{0, 1, 2, 3, 4, 5, 6, 9, 7, 12, 14, 13, 8, 10, 11};
    constexpr std::array<std::uint8_t, 13> pyramid13Vtk{0, 1, 2, 3, 4, 5, 8, 10, 6, 7, 9, 11, 12};

    // Edge lists in Gmsh local numbering; quadratic edges are split at their mid node.
    constexpr std::array<std::uint8_t, 2> line2Edges{0, 1};
    constexpr std::array<std::uint8_t, 4> line3Edges{0, 2, 2, 1};
    constexpr std::array<std::uint8_t, 6> tri3Edges{0, 1, 1, 2, 2, 0};
    constexpr std::array<std::uint8_t, 12> tri6Edges{0, 3, 3, 1, 1, 4, 4, 2, 2, 5, 5, 0};
    constexpr std::array<std::uint8_t, 8> quad4Edges{0, 1, 1, 2, 2, 3, 3, 0};
    constexpr std::array<std::uint8_t, 16> quad8Edges{0, 4, 4, 1, 1, 5, 5, 2, 2, 6, 6, 3, 3, 7, 7, 0};
    constexpr std::array<std::uint8_t, 12> tet4Edges{0, 1, 1, 2, 2, 0, 3, 0, 3, 2, 3, 1};
    constexpr std::array<std::uint8_t, 24> tet10Edges{0, 4, 4, 1, 1, 5, 5, 2, 2, 6, 6, 0,
                                                       3, 7, 7, 0, 3, 8, 8, 2, 3, 9, 9, 1};
    constexpr std::array<std::uint8_t, 24> hex8Edges{0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4,
                                                      0, 4, 1, 5, 2, 6, 3, 7};
    // Gmsh hex20 mid nodes: 8(0,1) 9(0,3) 10(0,4) 11(1,2) 12(1,5) 13(2,3) 14(2,6) 15(3,7) 16(4,5) 17(4,7) 18(5,6) 19(6,7)
    constexpr std::array<std::uint8_t, 48> hex20Edges{0, 8, 8, 1, 0, 9, 9, 3, 0, 10, 10, 4, 1, 11, 11, 2,
                                                       1, 12, 12, 5, 2, 13, 13, 3, 2, 14, 14, 6, 3, 15, 15, 7,
                                                       4, 16, 16, 5, 4, 17, 17, 7, 5, 18, 18, 6, 6, 19, 19, 7};
    constexpr std::array<std::uint8_t, 18> prism6Edges{0, 1, 1, 2, 2, 0, 3, 4, 4, 5, 5, 3, 0, 3, 1, 4, 2, 5};
    // Gmsh prism15 mid nodes: 6(0,1) 7(0,2) 8(0,3) 9(1,2) 10(1,4) 11(2,5) 12(3,4) 13(3,5) 14(4,5)
    constexpr std::array<std::uint8_t, 36> prism15Edges{0, 6, 6, 1, 0, 7, 7, 2, 0, 8, 8, 3, 1, 9, 9, 2,
                                                         1, 10, 10, 4, 2, 11, 11, 5, 3, 12, 12, 4, 3, 13, 13, 5,
                                                         4, 14, 14, 5};
    constexpr std::array<std::uint8_t, 16> pyramid5Edges{0, 1, 1, 2, 2, 3, 3, 0, 0, 4, 1, 4, 2, 4, 3, 4};
    // Gmsh pyramid13 mid nodes: 5(0,1) 6(0,3) 7(0,4) 8(1,2) 9(1,4) 10(2,3) 11(2,4) 12(3,4)
    constexpr std::array<std::uint8_t, 32> pyramid13Edges{0, 5, 5, 1, 0, 6, 6, 3, 0, 7, 7, 4, 1, 8, 8, 2,
                                                           1, 9, 9, 4, 2, 10, 10, 3, 2, 11, 11, 4, 3, 12, 12, 4};

    constexpr std::span<const std::uint8_t> identityOrder{};

    constexpr std::array<ElementTypeInfo, static_cast<std::size_t>(E_ElementType::Count)> elementTypeTable{{
      {E_ElementType::Point1,    "Point1",    0,  1, 15,  1, identityOrder,     {}},
      {E_ElementType::Line2,     "Line2",     1,  2,  1,  3, identityOrder,     line2Edges},
      {E_ElementType::Line3,     "Line3",     1,  3,  8, 21, identityOrder,     line3Edges},
      {E_ElementType::Tri3,      "Tri3",      2,  3,  2,  5, identityOrder,     tri3Edges},
      {E_ElementType::Tri6,      "Tri6",      2,  6,  9, 22, identityOrder,     tri6Edges},
      {E_ElementType::Quad4,     "Quad4",     2,  4,  3,  9, identityOrder,     quad4Edges},
      {E_ElementType::Quad8,     "Quad8",     2,  8, 16, 23, identityOrder,     quad8Edges},
      {E_ElementType::Quad9,     "Quad9",     2,  9, 10, 28, identityOrder,     quad8Edges},
      {E_ElementType::Tet4,      "Tet4",      3,  4,  4, 10, identityOrder,     tet4Edges},
      {E_ElementType::Tet10,     "Tet10",     3, 10, 11, 24, tet10Vtk,     tet10Edges},
      {E_ElementType::Hex8,      "Hex8",      3,  8,  5, 12, identityOrder,     hex8Edges},
      {E_ElementType::Hex20,     "Hex20",     3, 20, 17, 25, hex20Vtk,     hex20Edges},
      {E_ElementType::Hex27,     "Hex27",     3, 27, 12, 29, hex27Vtk,     hex20Edges},
      {E_ElementType::Prism6,    "Prism6",    3,  6,  6, 13, identityOrder,     prism6Edges},
      {E_ElementType::Prism15,   "Prism15",   3, 15, 18, 26, prism15Vtk,   prism15Edges},
      {E_ElementType::Pyramid5,  "Pyramid5",  3,  5,  7, 14, identityOrder,     pyramid5Edges},
      {E_ElementType::Pyramid13, "Pyramid13", 3, 13, 19, 27, pyramid13Vtk, pyramid13Edges},
    }};

    // Every table row must sit at the index of its enum value and carry a full permutation.
    constexpr bool tableIsConsistent() {
      for (std::size_t i = 0; i < elementTypeTable.size(); ++i) {
        const auto& row = elementTypeTable[i];
        if (static_cast<std::size_t>(row.type) != i) return false;
        if (!row.vtkFromGmsh.empty() && row.vtkFromGmsh.size() != static_cast<std::size_t>(row.nodeCount)) return false;
        for (const auto localNode : row.edges) {
          if (localNode >= row.nodeCount) return false;
        }
      }
      return true;
    }
    static_assert(tableIsConsistent(), "element type table is out of sync with ElementType");

  } // namespace end

  const ElementTypeInfo& elementInfo(const E_ElementType type) noexcept {
    return elementTypeTable[static_cast<std::size_t>(type)];
  }

  std::span<const ElementTypeInfo> allElementTypes() noexcept {
    return elementTypeTable;
  }

  std::optional<E_ElementType> elementTypeFromGmsh(const int gmshType) noexcept {
    for (const auto& row : elementTypeTable) {
      if (row.gmshType == gmshType) return row.type;
    }
    return std::nullopt;
  }

  std::optional<E_ElementType> elementTypeFromVtk(const int vtkType) noexcept {
    if (vtkType == 0) return std::nullopt;
    for (const auto& row : elementTypeTable) {
      if (row.vtkType == vtkType) return row.type;
    }
    return std::nullopt;
  }

} // namespace anaf::IO end
