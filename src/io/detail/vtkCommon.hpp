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

// Shared by the legacy VTK and the VTU (XML) implementations.

#include "../model/meshModel.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace anaf::IO::detail {

  // Turns VTK cells (VTK node order, file order) into model element blocks (Gmsh node order,
  // grouped by type) and remembers which file cell produced each element, so cell data can
  // be remapped. Composite cells (poly-vertex, poly-line, triangle strip, polygon) are split.
  class VtkCellCollector {
  public:
    VtkCellCollector(MeshModel& model, std::size_t nodeOffset) : m_model(model), m_nodeOffset(nodeOffset) {}

    void addCell(int vtkType, std::span<const std::int64_t> points, std::size_t fileCell);

    // Call once after all cells: returns, per global element index, the file cell it came from.
    std::vector<std::size_t> finish();

    // Expands a file-ordered cell array (`components` per cell) into global element order.
    static std::vector<double> remapCellData(std::span<const double> fileValues, int components,
                                             const std::vector<std::size_t>& sourceCell);

  private:
    void emit(E_ElementType type, std::span<const std::int64_t> vtkOrderedPoints, std::size_t fileCell);

    MeshModel& m_model;
    std::size_t m_nodeOffset;
    std::vector<std::vector<std::size_t>> m_sourceCellPerBlock;
    std::map<int, std::size_t> m_skipped;
    bool m_warnedPolygon{false};
  };

  // Model -> VTK arrays in global element order.
  struct VtkCells {
    std::vector<std::int64_t> connectivity; // VTK node order
    std::vector<std::int64_t> offsets;      // end offset per cell (VTK >= 9 / VTU convention)
    std::vector<std::uint8_t> types;
    std::vector<std::string> warnings;
  };
  VtkCells buildVtkCells(const MeshModel& model);

  // Legacy VTK names cannot contain whitespace; VTK escapes such characters as %XX.
  std::string encodeLegacyName(const std::string& name);
  std::string decodeLegacyName(const std::string& name);

} // namespace anaf::IO::detail end
