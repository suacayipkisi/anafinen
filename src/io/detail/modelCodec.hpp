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

#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

  // ------------------------------------------------------------ single-step formats (VTK, VTU)
  //
  // A VTK dataset holds one state. Steps that are not a time history go into the same file:
  //   Mode / Frequency / LoadCase field "<name>" with n steps
  //     -> n arrays "<name>_Mode_001" ... (or _Frequency_ / _LoadCase_), 1-based, >= 3 digits
  //     -> global array "<name>_Mode_Values" (n tuples) with `times`
  //   Time fields -> the step chosen by WriteOptions::timeStep, plus the ParaView "TimeValue"
  //     global array (only when a field has a time history or a non-zero time).
  // Step labels are not stored (they survive in MSH and the CAD sidecar).

  struct FlatArray {
    std::string name;
    FieldLocation location{FieldLocation::Node};
    int components{1};
    const std::vector<double>* values{nullptr};
  };

  struct FlatData {
    std::vector<FlatArray> arrays;       // node / element arrays, in input order
    std::vector<GlobalArray> globals;    // model.globalData, generated *_Values arrays and TimeValue
    std::vector<std::string> warnings;
  };

  // Chooses the step of a Time field in model.fields; nullptr leaves the field out.
  using StepPicker = std::function<const std::vector<double>*(const Field&)>;

  // Arrays of model.fields followed by `encoded` (encodeModelData() output). Time fields use
  // `pick` when given, otherwise selectStep(timeStep). `time` replaces the TimeValue derived
  // from the fields (the .pvd writer passes the time of each file).
  FlatData flattenSteps(const MeshModel& model, const std::vector<Field>& encoded, int timeStep,
                        const StepPicker& pick = {}, std::optional<double> time = std::nullopt);

  // Inverse of flattenSteps() for a freshly read model, before decodeModelData(): groups the
  // "<name>_<Kind>_NNN" arrays back into one field, consumes the matching "_Values" globals
  // and applies a "TimeValue" global to the time of every Time field.
  void unflattenSteps(MeshModel& model);

  inline constexpr std::string_view kTimeValue = "TimeValue";

} // namespace anaf::IO::detail end
