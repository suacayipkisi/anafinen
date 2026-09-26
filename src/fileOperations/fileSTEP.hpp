// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include "trussFileOperations/truss1D.hpp"

#include <memory>
#include <string>

namespace anaf::FILE {

  // Imports a STEP/IGES/BREP CAD file (via OpenCASCADE), meshes its curves into
  // 2-node line elements and returns the resulting nodes/elements.
  // On failure, the returned data has isSuccess() == false and getErrorMessage() set.
  std::shared_ptr<MeshImportData> importSTEP(const std::string& filePath);

  // Writes each line element of `data` as a straight CAD edge between two points
  // and saves the result as a STEP file. No meshing information is stored.
  bool exportSTEP(const std::string& filePath, const MeshImportData& data);

} // namespace anaf::FILE end
