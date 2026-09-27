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

// Conversion between the format-neutral anaf::IO::MeshModel and the truss snapshot
// (anaf::BRIDGE::MeshData) used by the solver pipeline and the viewport.

#include <bridge/generalStatus.hpp>
#include <io/model/meshModel.hpp>
#include <material/properties.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace FEM::TRUSS::ADAPTER {

  // Snapshot + boundary conditions -> model (for export). Results are included when the
  // snapshot carries them (MeshData::hasResults).
  anaf::IO::MeshModel toMeshModel(const anaf::BRIDGE::MeshData& mesh, const anaf::BRIDGE::FixedDOFMap& fixity);

  struct ImportedTruss {
    std::shared_ptr<anaf::BRIDGE::MeshData> mesh;
    anaf::BRIDGE::FixedDOFMap fixity;
    std::vector<std::string> notes; // user-facing remarks (e.g. elements shown as wireframe)
  };

  // Model -> snapshot (for the viewport). Line elements become bars; surface and volume
  // elements are shown as their edges. Node ids in the snapshot are 0-based model indices.
  ImportedTruss toMeshData(const anaf::IO::MeshModel& model, std::span<const anaf::MATERIAL::Material> materials);

} // namespace FEM::TRUSS::ADAPTER end
