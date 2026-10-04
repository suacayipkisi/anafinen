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

// Public entry point of the 3D beam / frame FEM core. No GUI types: a CLI or a test calls it
// directly.

#include <beam/beamProperties/meshData.hpp>
#include <beam/beamSection/beamSection.hpp>
#include <material/properties.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace FEM::BEAM {

  struct StaticResult {
    // Copy of the input with displacements and rotations on every node and section forces on
    // every element; hasResults = true. Loads and supports are kept.
    std::shared_ptr<MeshData> mesh;
    // Energy check: the stored elastic energy must equal half the external work.
    bool energyCheckPassed{false};
    double energyDiff{};          // |U - W / 2|, J
    double energyRelativeDiff{};  // energyDiff / max(|U|, |W / 2|, 1)
  };

  // Progress of a running solve, 0..1.
  using ProgressCallback = std::function<void(float)>;

  // Linear static solve of a beam model: Euler-Bernoulli and Timoshenko elements (chosen per
  // element), supports as allowed motion / rotation bases (inclined ones included), nodal
  // forces and moments, uniform distributed loads, self weight and end releases (hinges, by
  // static condensation; a node direction released at every element end there is held when
  // unloaded). materials and sections are the lists the elements' materialID and sectionID
  // values index into (E, G, density and Poisson's ratio are used; the section properties come
  // from computeProperties()). Returns why the model cannot be solved (no nodes or elements, an
  // unknown material or section, an invalid section, a load on a missing node or element, node
  // ids that are not 0..n-1, a bad orientation vector, end releases that make an element or a
  // loaded node a mechanism, a failed solve) or "cancelled" when st was stopped. Nodes used by
  // no element are held fixed.
  std::expected<StaticResult, std::string> solveStatic(
    const MeshData& mesh,
    std::span<const anaf::MATERIAL::Material> materials,
    std::span<const BeamSection> sections,
    std::stop_token st = {},
    const ProgressCallback& progress = {}
  );

} // namespace FEM::BEAM end
