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

// Public entry point of the truss FEM core. No GUI types: a CLI or a test calls it directly,
// the GUI calls it from TRUSS_WORKER::startSolve().

#include <material/properties.hpp>
#include <truss_1D/trussProperties/meshData.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace FEM::TRUSS {

  struct StaticResult {
    // Copy of the input with displacements on every node and stress on every solved bar
    // (wireframe edges keep 0); hasResults = true. Loads and supports are kept.
    std::shared_ptr<MeshData> mesh;
    // Energy check: the stored elastic energy must equal half the external work.
    bool energyCheckPassed{false};
    double energyDiff{};          // |U - W / 2|, J
    double energyRelativeDiff{};  // energyDiff / max(|U|, |W / 2|, 1)
  };

  // Progress of a running solve, 0..1.
  using ProgressCallback = std::function<void(float)>;

  // Static solve of a truss snapshot (generated, built-in, imported or entered by hand):
  // stiffness, self weight, supports (u = T q, inclined ones included), displacements,
  // stresses and the energy check. materials is the list the bars' materialID values index
  // into. Returns why the model cannot be solved (no nodes or bars, a bar without area, an
  // unknown material, node ids that are not 0..n-1, a zero-length bar, a failed solve) or
  // "cancelled" when st was stopped. Nodes used by no bar are held fixed.
  std::expected<StaticResult, std::string> solveStatic(
    const MeshData& mesh,
    std::span<const anaf::MATERIAL::Material> materials,
    std::stop_token st = {},
    const ProgressCallback& progress = {}
  );

} // namespace FEM::TRUSS end
