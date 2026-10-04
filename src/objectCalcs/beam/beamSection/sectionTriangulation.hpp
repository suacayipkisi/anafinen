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

// Triangles of a cross-section's face (the end caps of a rendered beam). The outline loops of
// sectionOutline() are merged into one polygon (each hole joined to the outer boundary by a
// bridge to a visible vertex) and cut into triangles by ear clipping. No GL types: the viewport
// extrudes the result.

#include "beamSection.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace FEM::BEAM {

  struct SectionTriangulation {
    std::vector<std::array<double, 2>> points;     // {y, z}, the outline points (bridge points repeated)
    std::vector<std::array<std::uint32_t, 3>> triangles; // indices into points
  };

  // Empty for a GeneralSection (no shape).
  SectionTriangulation triangulateSection(const SectionShape& shape, int segmentsPerQuarter = 4);

} // namespace FEM::BEAM end
