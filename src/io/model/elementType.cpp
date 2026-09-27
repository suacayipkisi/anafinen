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
    constexpr std::array<std::uint8_t, 10> kTet10Vtk{0, 1, 2, 3, 4, 5, 6, 7, 9, 8};
    constexpr std::array<std::uint8_t, 20> kHex20Vtk{0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 9, 16, 18, 19, 17, 10, 12, 14, 15};
    constexpr std::array<std::uint8_t, 27> kHex27Vtk{0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 9, 16, 18, 19, 17, 10, 12, 14, 15,
                                                     22, 23, 21, 24, 20, 25, 26};
    constexpr std::array<std::uint8_t, 15> kPrism15Vtk{0, 1, 2, 3, 4, 5, 6, 9, 7, 12, 14, 13, 8, 10, 11};
    constexpr std::array<std::uint8_t, 13> kPyramid13Vtk{0, 1, 2, 3, 4, 5, 8, 10, 6, 7, 9, 11, 12};

    // Edge lists in Gmsh local numbering; quadratic edges are split at their mid node.
    constexpr std::array<std::uint8_t, 2> kLine2Edges{0, 1};
    constexpr std::array<std::uint8_t, 4> kLine3Edges{0, 2, 2, 1};
    constexpr std::array<std::uint8_t, 6> kTri3Edges{0, 1, 1, 2, 2, 0};
    constexpr std::array<std::uint8_t, 12> kTri6Edges{0, 3, 3, 1, 1, 4, 4, 2, 2, 5, 5, 0};
    constexpr std::array<std::uint8_t, 8> kQuad4Edges{0, 1, 1, 2, 2, 3, 3, 0};
    constexpr std::array<std::uint8_t, 16> kQuad8Edges{0, 4, 4, 1, 1, 5, 5, 2, 2, 6, 6, 3, 3, 7, 7, 0};
    constexpr std::array<std::uint8_t, 12> kTet4Edges{0, 1, 1, 2, 2, 0, 3, 0, 3, 2, 3, 1};
    constexpr std::array<std::uint8_t, 24> kTet10Edges{0, 4, 4, 1, 1, 5, 5, 2, 2, 6, 6, 0,
                                                       3, 7, 7, 0, 3, 8, 8, 2, 3, 9, 9, 1};
    constexpr std::array<std::uint8_t, 24> kHex8Edges{0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4,
                                                      0, 4, 1, 5, 2, 6, 3, 7};
    // Gmsh hex20 mid nodes: 8(0,1) 9(0,3) 10(0,4) 11(1,2) 12(1,5) 13(2,3) 14(2,6) 15(3,7) 16(4,5) 17(4,7) 18(5,6) 19(6,7)
    constexpr std::array<std::uint8_t, 48> kHex20Edges{0, 8, 8, 1, 0, 9, 9, 3, 0, 10, 10, 4, 1, 11, 11, 2,
                                                       1, 12, 12, 5, 2, 13, 13, 3, 2, 14, 14, 6, 3, 15, 15, 7,
                                                       4, 16, 16, 5, 4, 17, 17, 7, 5, 18, 18, 6, 6, 19, 19, 7};
    constexpr std::array<std::uint8_t, 18> kPrism6Edges{0, 1, 1, 2, 2, 0, 3, 4, 4, 5, 5, 3, 0, 3, 1, 4, 2, 5};
    // Gmsh prism15 mid nodes: 6(0,1) 7(0,2) 8(0,3) 9(1,2) 10(1,4) 11(2,5) 12(3,4) 13(3,5) 14(4,5)
    constexpr std::array<std::uint8_t, 36> kPrism15Edges{0, 6, 6, 1, 0, 7, 7, 2, 0, 8, 8, 3, 1, 9, 9, 2,
                                                         1, 10, 10, 4, 2, 11, 11, 5, 3, 12, 12, 4, 3, 13, 13, 5,
                                                         4, 14, 14, 5};
    constexpr std::array<std::uint8_t, 16> kPyramid5Edges{0, 1, 1, 2, 2, 3, 3, 0, 0, 4, 1, 4, 2, 4, 3, 4};
    // Gmsh pyramid13 mid nodes: 5(0,1) 6(0,3) 7(0,4) 8(1,2) 9(1,4) 10(2,3) 11(2,4) 12(3,4)
    constexpr std::array<std::uint8_t, 32> kPyramid13Edges{0, 5, 5, 1, 0, 6, 6, 3, 0, 7, 7, 4, 1, 8, 8, 2,
                                                           1, 9, 9, 4, 2, 10, 10, 3, 2, 11, 11, 4, 3, 12, 12, 4};

    constexpr std::span<const std::uint8_t> kIdentity{};

    constexpr std::array<ElementTypeInfo, static_cast<std::size_t>(ElementType::Count)> kTable{{
      {ElementType::Point1,    "Point1",    0,  1, 15,  1, kIdentity,     {}},
      {ElementType::Line2,     "Line2",     1,  2,  1,  3, kIdentity,     kLine2Edges},
      {ElementType::Line3,     "Line3",     1,  3,  8, 21, kIdentity,     kLine3Edges},
      {ElementType::Tri3,      "Tri3",      2,  3,  2,  5, kIdentity,     kTri3Edges},
      {ElementType::Tri6,      "Tri6",      2,  6,  9, 22, kIdentity,     kTri6Edges},
      {ElementType::Quad4,     "Quad4",     2,  4,  3,  9, kIdentity,     kQuad4Edges},
      {ElementType::Quad8,     "Quad8",     2,  8, 16, 23, kIdentity,     kQuad8Edges},
      {ElementType::Quad9,     "Quad9",     2,  9, 10, 28, kIdentity,     kQuad8Edges},
      {ElementType::Tet4,      "Tet4",      3,  4,  4, 10, kIdentity,     kTet4Edges},
      {ElementType::Tet10,     "Tet10",     3, 10, 11, 24, kTet10Vtk,     kTet10Edges},
      {ElementType::Hex8,      "Hex8",      3,  8,  5, 12, kIdentity,     kHex8Edges},
      {ElementType::Hex20,     "Hex20",     3, 20, 17, 25, kHex20Vtk,     kHex20Edges},
      {ElementType::Hex27,     "Hex27",     3, 27, 12, 29, kHex27Vtk,     kHex20Edges},
      {ElementType::Prism6,    "Prism6",    3,  6,  6, 13, kIdentity,     kPrism6Edges},
      {ElementType::Prism15,   "Prism15",   3, 15, 18, 26, kPrism15Vtk,   kPrism15Edges},
      {ElementType::Pyramid5,  "Pyramid5",  3,  5,  7, 14, kIdentity,     kPyramid5Edges},
      {ElementType::Pyramid13, "Pyramid13", 3, 13, 19, 27, kPyramid13Vtk, kPyramid13Edges},
    }};

    // Every table row must sit at the index of its enum value and carry a full permutation.
    constexpr bool tableIsConsistent() {
      for (std::size_t i = 0; i < kTable.size(); ++i) {
        const auto& row = kTable[i];
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

  const ElementTypeInfo& elementInfo(const ElementType type) noexcept {
    return kTable[static_cast<std::size_t>(type)];
  }

  std::span<const ElementTypeInfo> allElementTypes() noexcept {
    return kTable;
  }

  std::optional<ElementType> elementTypeFromGmsh(const int gmshType) noexcept {
    for (const auto& row : kTable) {
      if (row.gmshType == gmshType) return row.type;
    }
    return std::nullopt;
  }

  std::optional<ElementType> elementTypeFromVtk(const int vtkType) noexcept {
    if (vtkType == 0) return std::nullopt;
    for (const auto& row : kTable) {
      if (row.vtkType == vtkType) return row.type;
    }
    return std::nullopt;
  }

} // namespace anaf::IO end
