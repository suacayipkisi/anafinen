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
// (FEM::TRUSS::MeshData) used by the solver pipeline and the viewport.

#include <truss_1D/trussProperties/meshData.hpp>
#include <io/model/meshModel.hpp>
#include <material/properties.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FEM::TRUSS::ADAPTER {

  // Materials travel by name: one element set "Material:<name>" per material used. Every
  // format keeps element sets (MSH physical groups, VTK / VTU / sidecar membership arrays),
  // so a file stays correct when the material list changes (new built-ins, user materials
  // added or removed, another machine). The MaterialID attribute (index into the list at
  // export time) is still written for viewers such as ParaView and for older anafinen.
  inline constexpr std::string_view kMaterialSetPrefix = "Material:";

  // Snapshot -> model (for export). Supports come from the nodes (Node::isSupported());
  // results are included when the snapshot carries them (MeshData::hasResults). materials:
  // the list the snapshot's materialID values index into (bridge.allMaterials).
  anaf::IO::MeshModel toMeshModel(const FEM::TRUSS::MeshData& mesh, std::span<const anaf::MATERIAL::Material> materials);

  struct ImportedTruss {
    std::shared_ptr<FEM::TRUSS::MeshData> mesh; // supports are set on its nodes
    std::vector<std::string> notes; // user-facing remarks (e.g. elements shown as wireframe)
  };

  // Model -> snapshot (for the viewport). Line elements become bars; surface and volume
  // elements are shown as their edges. Node ids in the snapshot are 0-based model indices.
  // Bar materials: "Material:<name>" sets are matched by name (case-insensitive); an unknown
  // name or index falls back to material 0 with a note. Files without these sets
  // (anafinen <= 0.1.2) use the MaterialID index, which matches the built-in order.
  ImportedTruss toMeshData(const anaf::IO::MeshModel& model, std::span<const anaf::MATERIAL::Material> materials);

} // namespace FEM::TRUSS::ADAPTER end
