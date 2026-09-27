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

// Maps the structured parts of a MeshModel (constraints, loads, attributes, sets, tags)
// to plain named fields and back. Every format that stores data as named arrays uses the
// same names, so a model written by one format reads back identically from another.
//
// Field names written:
//   node    "Fixity"               3 comps, 1 = fixed, 0 = free
//   node    "AllowedMotionBasis"  10 comps, rank + 3 x 3 basis (only when a node has an inclined support)
//   node    "NodalForce"           3 comps, N
//   element "MaterialID", "CrossSectionArea", "Attribute:<name>"   1 comp each
//   node    "NodeSet:<name>"       1 comp, 1 = member      (VTK / VTU / sidecar)
//   element "ElementSet:<name>"    1 comp, 1 = member      (VTK / VTU / sidecar)
//   node    "NodeTag", element "ElementTag", "EntityTag"   original ids (VTK / VTU)
// Legacy names accepted on read: "FixityX/Y/Z", "AllowedMotionRank" + 9-comp "AllowedMotionBasis",
// "FixityDirection_*" (fixed directions).

#include "../model/meshModel.hpp"

#include <vector>

namespace anaf::IO::detail {

  struct CodecOptions {
    bool nodeSets{true};      // encode / decode "NodeSet:<name>" arrays
    bool elementSets{true};   // encode / decode "ElementSet:<name>" arrays
    bool tags{true};          // encode / decode "NodeTag" / "ElementTag" / "EntityTag"
  };

  // Fields to write in addition to `model.fields` (single step, time 0).
  std::vector<Field> encodeModelData(const MeshModel& model, const CodecOptions& options);

  // Moves recognised fields out of `model.fields` into constraints, loads, attributes, sets and tags.
  void decodeModelData(MeshModel& model, const CodecOptions& options);

  // Field restricted to one time step (-1 = last), for single-step formats.
  const std::vector<double>* selectStep(const Field& field, int timeStep);

} // namespace anaf::IO::detail end
