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

#include "modelCodec.hpp"
#include "textIo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <string>
#include <string_view>

namespace anaf::IO::detail {

  namespace {

    constexpr std::string_view kFixity = "Fixity";
    constexpr std::string_view kFixityRotation = "FixityRotation";
    constexpr std::string_view kAllowedMotion = "AllowedMotionBasis";
    constexpr std::string_view kNodalForce = "NodalForce";
    constexpr std::string_view kNodalMoment = "NodalMoment";
    constexpr std::string_view kPrescribedDisplacement = "PrescribedDisplacement";
    constexpr std::string_view kPrescribedRotation = "PrescribedRotation";
    constexpr std::string_view kPrescribedTemperature = "PrescribedTemperature";
    constexpr std::string_view kNodalHeat = "NodalHeat";
    constexpr std::string_view kInitialPrefix = "Initial:";
    constexpr std::string_view kAmplitudePrefix = "Amplitude:";
    constexpr std::string_view kRayleighDamping = "RayleighDamping";
    constexpr std::string_view kModalDampingRatio = "ModalDampingRatio";
    constexpr std::string_view kBeamOrientation = "BeamOrientation";
    constexpr std::string_view kAttributePrefix = "Attribute:";
    constexpr std::string_view kNodeSetPrefix = "NodeSet:";
    constexpr std::string_view kElementSetPrefix = "ElementSet:";
    constexpr std::string_view kNodeTag = "NodeTag";
    constexpr std::string_view kElementTag = "ElementTag";
    constexpr std::string_view kEntityTag = "EntityTag";

    using Direction = std::array<double, 3>;

    Field makeField(std::string name, const FieldLocation location, const int components, std::vector<double> values) {
      Field field;
      field.name = std::move(name);
      field.location = location;
      field.components = components;
      field.times = {0.0};
      field.steps.push_back(std::move(values));
      return field;
    }

    bool isKnownAttribute(const std::string_view name) {
      return name == Attribute::MaterialId || name == Attribute::CrossSectionArea || name == Attribute::HeatGeneration;
    }

    // "<base>" for constant data, "<base>:<amplitude>" otherwise.
    std::string withAmplitude(const std::string_view base, const std::string& amplitude) {
      return amplitude.empty() ? std::string(base) : std::string(base) + ":" + amplitude;
    }

    // Amplitude name of "<base>" / "<base>:<amplitude>", or nullopt for other names.
    std::optional<std::string> amplitudeOf(const std::string& name, const std::string_view base) {
      if (name == base) return std::string();
      if (name.size() > base.size() + 1 && name.starts_with(base) && name[base.size()] == ':') return name.substr(base.size() + 1);
      return std::nullopt;
    }

    double dot(const Direction& a, const Direction& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    // Orthonormal basis of the directions left free by `fixedDirections` (Gram-Schmidt).
    std::vector<Direction> freeBasisFromFixed(const std::vector<Direction>& fixedDirections) {
      constexpr double tolerance = 1e-12;
      std::vector<Direction> fixedBasis;
      for (auto direction : fixedDirections) {
        for (const auto& basis : fixedBasis) {
          const double projection = dot(direction, basis);
          for (int a = 0; a < 3; ++a) direction[a] -= projection * basis[a];
        }
        const double norm = std::sqrt(dot(direction, direction));
        if (norm > tolerance) {
          for (double& v : direction) v /= norm;
          fixedBasis.push_back(direction);
        }
      }
      std::vector<Direction> freeBasis;
      for (int axis = 0; axis < 3 && freeBasis.size() + fixedBasis.size() < 3; ++axis) {
        Direction residual{};
        residual[axis] = 1.0;
        for (const auto* group : {&fixedBasis, &freeBasis}) {
          for (const auto& basis : *group) {
            const double projection = dot(residual, basis);
            for (int a = 0; a < 3; ++a) residual[a] -= projection * basis[a];
          }
        }
        const double norm = std::sqrt(dot(residual, residual));
        if (norm > tolerance) {
          for (double& v : residual) v /= norm;
          freeBasis.push_back(residual);
        }
      }
      return freeBasis;
    }

    // True when `allowedMotion` is exactly the axis-aligned basis implied by `fixed`.
    bool isAxisAligned(const NodeConstraint& constraint) {
      if (constraint.allowedMotion.empty()) return true;
      std::vector<Direction> expected;
      for (int axis = 0; axis < 3; ++axis) {
        if (!constraint.fixed[axis]) {
          Direction d{};
          d[axis] = 1.0;
          expected.push_back(d);
        }
      }
      if (expected.size() != constraint.allowedMotion.size()) return false;
      for (std::size_t i = 0; i < expected.size(); ++i) {
        for (int a = 0; a < 3; ++a) {
          if (std::abs(expected[i][a] - constraint.allowedMotion[i][a]) > 1e-12) return false;
        }
      }
      return true;
    }

    // Removes the first field matching (name, location) and returns it.
    std::optional<Field> takeField(MeshModel& model, const std::string_view name, const FieldLocation location) {
      const auto it = std::ranges::find_if(model.fields, [&](const Field& f) { return f.name == name && f.location == location; });
      if (it == model.fields.end()) return std::nullopt;
      Field field = std::move(*it);
      model.fields.erase(it);
      return field;
    }

    const std::vector<double>* lastStep(const Field& field) {
      return field.steps.empty() ? nullptr : &field.steps.back();
    }

    NodeConstraint& constraintFor(MeshModel& model, std::vector<int>& indexByNode, const std::uint32_t node) {
      if (indexByNode[node] < 0) {
        indexByNode[node] = static_cast<int>(model.constraints.size());
        model.constraints.push_back(NodeConstraint{node, {false, false, false}, {}, {}, {}});
      }
      return model.constraints[static_cast<std::size_t>(indexByNode[node])];
    }

  } // namespace end

  const std::vector<double>* selectStep(const Field& field, const int timeStep) {
    if (field.steps.empty()) return nullptr;
    if (timeStep < 0 || static_cast<std::size_t>(timeStep) >= field.steps.size()) return &field.steps.back();
    return &field.steps[static_cast<std::size_t>(timeStep)];
  }

  FlatData flattenSteps(const MeshModel& model, const std::vector<Field>& encoded, const int timeStep,
                        const StepPicker& pick, const std::optional<double> time) {
    FlatData out;
    out.globals = model.globalData;
    for (auto& global : encodeModelGlobals(model)) out.globals.push_back(std::move(global));
    std::optional<double> timeValue = time;
    bool labelsDropped = false;
    for (const auto* group : {&model.fields, &encoded}) {
      for (const auto& field : *group) {
        if (field.steps.empty()) continue;
        if (field.stepKind == StepKind::Time) {
          const bool picked = pick && group == &model.fields;
          const auto* values = picked ? pick(field) : selectStep(field, timeStep);
          if (!values) continue;
          const std::size_t step = static_cast<std::size_t>(values - field.steps.data());
          if (field.steps.size() > 1 && !picked) {
            out.warnings.push_back(std::format("field '{}': one time step written (step {} of {})", field.name, step + 1, field.steps.size()));
          }
          const double stepTime = step < field.times.size() ? field.times[step] : 0.0;
          if (group == &model.fields && !timeValue && (field.steps.size() > 1 || stepTime != 0.0)) timeValue = stepTime;
          out.arrays.push_back(FlatArray{field.name, field.location, field.components, values});
          continue;
        }
        const std::string kind(stepKindName(field.stepKind));
        const int digits = std::max(3, static_cast<int>(std::to_string(field.steps.size()).size()));
        for (std::size_t s = 0; s < field.steps.size(); ++s) {
          out.arrays.push_back(FlatArray{std::format("{}_{}_{:0{}}", field.name, kind, s + 1, digits), field.location,
                                         field.components, &field.steps[s]});
        }
        std::vector<double> values(field.steps.size(), 0.0);
        for (std::size_t s = 0; s < values.size() && s < field.times.size(); ++s) values[s] = field.times[s];
        out.globals.push_back(GlobalArray{std::format("{}_{}_Values", field.name, kind), 1, std::move(values)});
        labelsDropped = labelsDropped || !field.stepLabels.empty();
      }
    }
    if (timeValue && !model.findGlobal(std::string(kTimeValue))) {
      out.globals.push_back(GlobalArray{std::string(kTimeValue), 1, {*timeValue}});
    }
    if (labelsDropped) out.warnings.push_back("step labels are not stored in VTK files");
    return out;
  }

  void unflattenSteps(MeshModel& model) {
    struct Group {
      FieldLocation location;
      int components;
      StepKind kind;
      std::map<std::size_t, std::vector<double>> steps; // mode / case number -> values
    };
    std::map<std::pair<std::string, int>, Group> groups; // (base name, kind + location key)
    std::vector<Field> kept;
    for (std::size_t i = 0; i < model.fields.size(); ++i) {
      auto& field = model.fields[i];
      // "<base>_<Kind>_<digits>"
      const auto last = field.name.rfind('_');
      const auto kindSeparator = last == std::string::npos || last == 0 ? std::string::npos : field.name.rfind('_', last - 1);
      const std::string_view digits = last == std::string::npos ? std::string_view{} : std::string_view(field.name).substr(last + 1);
      const bool numbered = digits.size() >= 3 && std::ranges::all_of(digits, [](const char c) { return c >= '0' && c <= '9'; });
      const auto kind = numbered && kindSeparator != std::string::npos && kindSeparator > 0
        ? stepKindFromName(std::string_view(field.name).substr(kindSeparator + 1, last - kindSeparator - 1)) : std::nullopt;
      if (!kind || *kind == StepKind::Time || field.steps.size() != 1) {
        kept.push_back(std::move(field));
        continue;
      }
      const std::string base = field.name.substr(0, kindSeparator);
      const int key = static_cast<int>(*kind) * 2 + static_cast<int>(field.location);
      auto [it, inserted] = groups.try_emplace({base, key}, Group{field.location, field.components, *kind, {}});
      const auto number = Cursor::parseNumber<std::size_t>(digits);
      if (it->second.components != field.components || it->second.steps.contains(number)) {
        kept.push_back(std::move(field)); // not part of a consistent group: keep it as it is
        continue;
      }
      it->second.steps.emplace(number, std::move(field.steps.front()));
    }
    for (auto& [key, group] : groups) {
      Field field;
      field.name = key.first;
      field.location = group.location;
      field.components = group.components;
      field.stepKind = group.kind;
      const std::string valuesName = std::format("{}_{}_Values", key.first, stepKindName(group.kind));
      const auto global = std::ranges::find_if(model.globalData, [&](const GlobalArray& a) { return a.name == valuesName; });
      const bool haveValues = global != model.globalData.end() && global->components == 1 && global->values.size() == group.steps.size();
      std::size_t s = 0;
      for (auto& [number, values] : group.steps) {
        field.times.push_back(haveValues ? global->values[s] : static_cast<double>(number));
        field.steps.push_back(std::move(values));
        ++s;
      }
      if (haveValues) model.globalData.erase(global);
      kept.push_back(std::move(field));
    }
    model.fields = std::move(kept);

    const auto timeValue = std::ranges::find_if(model.globalData, [](const GlobalArray& a) { return a.name == kTimeValue; });
    if (timeValue != model.globalData.end() && timeValue->values.size() == 1) {
      for (auto& field : model.fields) {
        if (field.stepKind == StepKind::Time) std::ranges::fill(field.times, timeValue->values.front());
      }
      model.globalData.erase(timeValue);
    }
  }

  std::vector<Field> encodeModelData(const MeshModel& model, const CodecOptions& options) {
    std::vector<Field> out;
    const std::size_t nodeTotal = model.nodes.size();
    const std::size_t elementTotal = model.elementCount();

    if (!model.constraints.empty()) {
      std::vector<double> fixity(nodeTotal * 3, 0.0);
      bool needsBasis = false;
      for (const auto& constraint : model.constraints) {
        for (int axis = 0; axis < 3; ++axis) fixity[constraint.node * 3 + axis] = constraint.fixed[axis] ? 1.0 : 0.0;
        needsBasis = needsBasis || !isAxisAligned(constraint);
      }
      out.push_back(makeField(std::string(kFixity), FieldLocation::Node, 3, std::move(fixity)));

      if (needsBasis) {
        // rank, then up to three free directions; nodes without a constraint are fully free (rank 3, identity).
        std::vector<double> basis(nodeTotal * 10, 0.0);
        for (std::size_t n = 0; n < nodeTotal; ++n) {
          basis[n * 10] = 3.0;
          basis[n * 10 + 1] = 1.0;
          basis[n * 10 + 5] = 1.0;
          basis[n * 10 + 9] = 1.0;
        }
        for (const auto& constraint : model.constraints) {
          auto directions = constraint.allowedMotion;
          if (directions.empty()) {
            for (int axis = 0; axis < 3; ++axis) {
              if (!constraint.fixed[axis]) {
                Direction d{};
                d[axis] = 1.0;
                directions.push_back(d);
              }
            }
          }
          double* row = &basis[constraint.node * 10];
          std::fill(row, row + 10, 0.0);
          row[0] = static_cast<double>(directions.size());
          for (std::size_t i = 0; i < directions.size() && i < 3; ++i) {
            for (int a = 0; a < 3; ++a) row[1 + i * 3 + a] = directions[i][a];
          }
        }
        out.push_back(makeField(std::string(kAllowedMotion), FieldLocation::Node, 10, std::move(basis)));
      }

      bool anyRotational = false;
      for (const auto& constraint : model.constraints) {
        if (constraint.fixedRotation != std::array<bool, 3>{}) {
          anyRotational = true;
          break;
        }
      }
      if (anyRotational) {
        std::vector<double> fixityRot(nodeTotal * 3, 0.0);
        for (const auto& constraint : model.constraints) {
          for (int axis = 0; axis < 3; ++axis) fixityRot[constraint.node * 3 + axis] = constraint.fixedRotation[axis] ? 1.0 : 0.0;
        }
        out.push_back(makeField(std::string(kFixityRotation), FieldLocation::Node, 3, std::move(fixityRot)));
      }
    }

    // Per-amplitude arrays; std::map keeps the output order stable.
    std::map<std::string, std::vector<double>> prescribed;
    for (const auto& constraint : model.constraints) {
      const bool imposed = constraint.prescribed != std::array<double, 3>{} || !constraint.amplitude.empty();
      if (!imposed) continue;
      auto& values = prescribed[constraint.amplitude];
      if (values.empty()) values.assign(nodeTotal * 4, 0.0);
      values[constraint.node * 4] = 1.0;
      for (int axis = 0; axis < 3; ++axis) values[constraint.node * 4 + 1 + axis] = constraint.prescribed[axis];
    }
    for (auto& [amplitude, values] : prescribed) {
      out.push_back(makeField(withAmplitude(kPrescribedDisplacement, amplitude), FieldLocation::Node, 4, std::move(values)));
    }

    std::map<std::string, std::vector<double>> prescribedRot;
    for (const auto& constraint : model.constraints) {
      const bool imposed = constraint.prescribedRotation != std::array<double, 3>{} || !constraint.amplitudeRotation.empty();
      if (!imposed) continue;
      auto& values = prescribedRot[constraint.amplitudeRotation];
      if (values.empty()) values.assign(nodeTotal * 4, 0.0);
      values[constraint.node * 4] = 1.0;
      for (int axis = 0; axis < 3; ++axis) values[constraint.node * 4 + 1 + axis] = constraint.prescribedRotation[axis];
    }
    for (auto& [amplitude, values] : prescribedRot) {
      out.push_back(makeField(withAmplitude(kPrescribedRotation, amplitude), FieldLocation::Node, 4, std::move(values)));
    }

    std::map<std::string, std::vector<double>> forces;
    for (const auto& load : model.loads) {
      if (load.force == std::array<double, 3>{}) continue;
      auto& force = forces[load.amplitude];
      if (force.empty()) force.assign(nodeTotal * 3, 0.0);
      for (int axis = 0; axis < 3; ++axis) force[load.node * 3 + axis] += load.force[axis];
    }
    for (auto& [amplitude, force] : forces) {
      out.push_back(makeField(withAmplitude(kNodalForce, amplitude), FieldLocation::Node, 3, std::move(force)));
    }

    std::map<std::string, std::vector<double>> moments;
    for (const auto& load : model.loads) {
      if (load.moment == std::array<double, 3>{}) continue;
      auto& moment = moments[load.amplitude];
      if (moment.empty()) moment.assign(nodeTotal * 3, 0.0);
      for (int axis = 0; axis < 3; ++axis) moment[load.node * 3 + axis] += load.moment[axis];
    }
    for (auto& [amplitude, moment] : moments) {
      out.push_back(makeField(withAmplitude(kNodalMoment, amplitude), FieldLocation::Node, 3, std::move(moment)));
    }

    std::map<std::string, std::vector<double>> temperatures;
    for (const auto& constraint : model.temperatureConstraints) {
      auto& values = temperatures[constraint.amplitude];
      if (values.empty()) values.assign(nodeTotal * 2, 0.0);
      values[constraint.node * 2] = 1.0;
      values[constraint.node * 2 + 1] = constraint.temperature;
    }
    for (auto& [amplitude, values] : temperatures) {
      out.push_back(makeField(withAmplitude(kPrescribedTemperature, amplitude), FieldLocation::Node, 2, std::move(values)));
    }

    std::map<std::string, std::vector<double>> heat;
    for (const auto& load : model.heatLoads) {
      auto& values = heat[load.amplitude];
      if (values.empty()) values.assign(nodeTotal, 0.0);
      values[load.node] += load.power;
    }
    for (auto& [amplitude, values] : heat) {
      out.push_back(makeField(withAmplitude(kNodalHeat, amplitude), FieldLocation::Node, 1, std::move(values)));
    }

    for (const auto& condition : model.initialConditions) {
      if (condition.values.size() != nodeTotal * static_cast<std::size_t>(condition.components)) continue;
      out.push_back(makeField(std::string(kInitialPrefix) + condition.quantity, FieldLocation::Node, condition.components, condition.values));
    }

    for (const auto& [name, values] : model.elementAttributes) {
      if (values.size() != elementTotal) continue;
      std::string fieldName = isKnownAttribute(name) ? name : std::string(kAttributePrefix) + name;
      out.push_back(makeField(std::move(fieldName), FieldLocation::Element, 1, values));
    }

    if (!model.beamOrientation.empty() && model.beamOrientation.size() == elementTotal) {
      std::vector<double> orientation(elementTotal * 3);
      for (std::size_t e = 0; e < elementTotal; ++e) {
        for (int a = 0; a < 3; ++a) orientation[e * 3 + a] = model.beamOrientation[e][a];
      }
      out.push_back(makeField(std::string(kBeamOrientation), FieldLocation::Element, 3, std::move(orientation)));
    }

    if (options.nodeSets || options.elementSets) {
      for (const auto& set : model.sets) {
        const bool isNodeSet = set.kind == SetKind::Node;
        if (isNodeSet ? !options.nodeSets : !options.elementSets) continue;
        std::vector<double> membership(isNodeSet ? nodeTotal : elementTotal, 0.0);
        for (const auto member : set.members) {
          if (member < membership.size()) membership[member] = 1.0;
        }
        out.push_back(makeField(std::string(isNodeSet ? kNodeSetPrefix : kElementSetPrefix) + set.name,
          isNodeSet ? FieldLocation::Node : FieldLocation::Element, 1, std::move(membership)));
      }
    }

    if (options.tags) {
      std::vector<double> nodeTags(nodeTotal);
      for (std::size_t n = 0; n < nodeTotal; ++n) nodeTags[n] = static_cast<double>(model.nodes[n].tag);
      out.push_back(makeField(std::string(kNodeTag), FieldLocation::Node, 1, std::move(nodeTags)));

      std::vector<double> elementTags;
      std::vector<double> entityTags;
      elementTags.reserve(elementTotal);
      entityTags.reserve(elementTotal);
      bool anyEntity = false;
      for (const auto& block : model.blocks) {
        for (std::size_t e = 0; e < block.size(); ++e) {
          elementTags.push_back(static_cast<double>(block.tags[e]));
          const int entity = block.entityTags.empty() ? 0 : block.entityTags[e];
          anyEntity = anyEntity || entity != 0;
          entityTags.push_back(static_cast<double>(entity));
        }
      }
      out.push_back(makeField(std::string(kElementTag), FieldLocation::Element, 1, std::move(elementTags)));
      if (anyEntity) out.push_back(makeField(std::string(kEntityTag), FieldLocation::Element, 1, std::move(entityTags)));
    }
    return out;
  }

  std::vector<GlobalArray> encodeModelGlobals(const MeshModel& model) {
    std::vector<GlobalArray> out;
    for (const auto& amplitude : model.amplitudes) {
      GlobalArray array{std::string(kAmplitudePrefix) + amplitude.name, 2, {}};
      for (std::size_t i = 0; i < amplitude.times.size() && i < amplitude.factors.size(); ++i) {
        array.values.push_back(amplitude.times[i]);
        array.values.push_back(amplitude.factors[i]);
      }
      out.push_back(std::move(array));
    }
    if (model.damping) {
      out.push_back(GlobalArray{std::string(kRayleighDamping), 2, {model.damping->rayleighAlpha, model.damping->rayleighBeta}});
      if (!model.damping->modalRatios.empty()) out.push_back(GlobalArray{std::string(kModalDampingRatio), 1, model.damping->modalRatios});
    }
    return out;
  }

  void decodeModelData(MeshModel& model, const CodecOptions& options) {
    const std::size_t nodeTotal = model.nodes.size();
    const std::size_t elementTotal = model.elementCount();
    std::vector<int> constraintIndex(nodeTotal, -1);
    for (std::size_t i = 0; i < model.constraints.size(); ++i) {
      if (model.constraints[i].node < nodeTotal) constraintIndex[model.constraints[i].node] = static_cast<int>(i);
    }

    auto sized = [](const Field& field, const std::size_t entities) {
      const auto* step = field.steps.empty() ? nullptr : &field.steps.back();
      return step && step->size() == entities * static_cast<std::size_t>(field.components);
    };

    // Fixity (current 3-component form, then legacy per-axis scalars).
    if (auto fixity = takeField(model, kFixity, FieldLocation::Node); fixity && fixity->components == 3 && sized(*fixity, nodeTotal)) {
      const auto& values = *lastStep(*fixity);
      for (std::uint32_t n = 0; n < nodeTotal; ++n) {
        const std::array<bool, 3> fixed{values[n * 3] != 0.0, values[n * 3 + 1] != 0.0, values[n * 3 + 2] != 0.0};
        if (fixed[0] || fixed[1] || fixed[2]) constraintFor(model, constraintIndex, n).fixed = fixed;
      }
    }
    constexpr std::array<std::string_view, 3> legacyAxes{"FixityX", "FixityY", "FixityZ"};
    for (int axis = 0; axis < 3; ++axis) {
      if (auto legacy = takeField(model, legacyAxes[axis], FieldLocation::Node); legacy && sized(*legacy, nodeTotal)) {
        const auto& values = *lastStep(*legacy);
        for (std::uint32_t n = 0; n < nodeTotal; ++n) {
          if (values[n * legacy->components] != 0.0) constraintFor(model, constraintIndex, n).fixed[axis] = true;
        }
      }
    }

    // Inclined supports: current 10-component form or legacy rank + 9-component pair.
    auto basisField = takeField(model, kAllowedMotion, FieldLocation::Node);
    auto legacyRank = takeField(model, "AllowedMotionRank", FieldLocation::Node);
    if (basisField && sized(*basisField, nodeTotal) && (basisField->components == 10 || (basisField->components == 9 && legacyRank))) {
      const auto& values = *lastStep(*basisField);
      const std::vector<double>* ranks = legacyRank ? lastStep(*legacyRank) : nullptr;
      const int stride = basisField->components;
      const int offset = stride == 10 ? 1 : 0;
      for (std::uint32_t n = 0; n < nodeTotal; ++n) {
        const double rankValue = stride == 10 ? values[n * 10] : (ranks && n < ranks->size() ? (*ranks)[n] : 3.0);
        const int rank = std::clamp(static_cast<int>(std::lround(rankValue)), 0, 3);
        std::vector<Direction> directions;
        for (int i = 0; i < rank; ++i) {
          const double* d = &values[n * stride + offset + i * 3];
          directions.push_back({d[0], d[1], d[2]});
        }
        const bool fullyFree = rank == 3 && constraintIndex[n] < 0;
        if (fullyFree) continue;
        auto& constraint = constraintFor(model, constraintIndex, n);
        constraint.allowedMotion = std::move(directions);
        // Keep `fixed` consistent: an axis is fixed when it lies outside the free span.
        for (int axis = 0; axis < 3; ++axis) {
          Direction residual{};
          residual[axis] = 1.0;
          for (const auto& basis : constraint.allowedMotion) {
            const double projection = dot(residual, basis);
            for (int a = 0; a < 3; ++a) residual[a] -= projection * basis[a];
          }
          constraint.fixed[axis] = std::sqrt(dot(residual, residual)) > 1e-9;
        }
      }
    } else {
      // Not a recognised constraint encoding: keep the arrays as ordinary fields.
      if (basisField) model.fields.push_back(std::move(*basisField));
      if (legacyRank) model.fields.push_back(std::move(*legacyRank));
    }

    // Rotational fixity.
    if (auto fixityRot = takeField(model, kFixityRotation, FieldLocation::Node); fixityRot && fixityRot->components == 3 && sized(*fixityRot, nodeTotal)) {
      const auto& values = *lastStep(*fixityRot);
      for (std::uint32_t n = 0; n < nodeTotal; ++n) {
        const std::array<bool, 3> fixedRot{values[n * 3] != 0.0, values[n * 3 + 1] != 0.0, values[n * 3 + 2] != 0.0};
        if (fixedRot[0] || fixedRot[1] || fixedRot[2]) constraintFor(model, constraintIndex, n).fixedRotation = fixedRot;
      }
    }

    // Legacy "FixityDirection_*" vectors list fixed directions per node.
    std::vector<std::vector<Direction>> fixedDirections;
    for (auto it = model.fields.begin(); it != model.fields.end();) {
      if (it->location == FieldLocation::Node && it->components == 3 && it->name.starts_with("FixityDirection_") && sized(*it, nodeTotal)) {
        if (fixedDirections.empty()) fixedDirections.resize(nodeTotal);
        const auto& values = it->steps.back();
        for (std::size_t n = 0; n < nodeTotal; ++n) {
          const Direction d{values[n * 3], values[n * 3 + 1], values[n * 3 + 2]};
          if (d[0] != 0.0 || d[1] != 0.0 || d[2] != 0.0) fixedDirections[n].push_back(d);
        }
        it = model.fields.erase(it);
      } else {
        ++it;
      }
    }
    for (std::uint32_t n = 0; n < fixedDirections.size(); ++n) {
      if (fixedDirections[n].empty()) continue;
      auto& constraint = constraintFor(model, constraintIndex, n);
      auto directions = fixedDirections[n];
      for (int axis = 0; axis < 3; ++axis) {
        if (constraint.fixed[axis]) {
          Direction d{};
          d[axis] = 1.0;
          directions.push_back(d);
        }
      }
      constraint.allowedMotion = freeBasisFromFixed(directions);
    }

    // Prescribed displacements, loads, thermal BCs and initial conditions (all node arrays).
    // "NodalForce:<a>" and "NodalMoment:<a>" of one node merge into one NodalLoad per (node, amplitude).
    struct LoadKey {
      std::string amplitude;
      std::uint32_t node;
      bool operator<(const LoadKey& o) const noexcept {
        if (node != o.node) return node < o.node;
        return amplitude < o.amplitude;
      }
    };
    std::map<LoadKey, std::size_t> loadIndex;
    for (std::size_t i = 0; i < model.loads.size(); ++i) loadIndex.try_emplace({model.loads[i].amplitude, model.loads[i].node}, i);
    auto loadFor = [&](const std::uint32_t node, const std::string& amplitude) -> NodalLoad& {
      const auto [entry, inserted] = loadIndex.try_emplace({amplitude, node}, model.loads.size());
      if (inserted) model.loads.push_back(NodalLoad{node, {}, amplitude});
      return model.loads[entry->second];
    };
    for (auto it = model.fields.begin(); it != model.fields.end();) {
      const Field& field = *it;
      bool consumed = false;
      if (field.location == FieldLocation::Node && sized(field, nodeTotal)) {
        const auto& values = field.steps.back();
        if (const auto amplitude = amplitudeOf(field.name, kPrescribedDisplacement); amplitude && field.components == 4) {
          for (std::uint32_t n = 0; n < nodeTotal; ++n) {
            if (values[n * 4] == 0.0) continue;
            auto& constraint = constraintFor(model, constraintIndex, n);
            constraint.prescribed = {values[n * 4 + 1], values[n * 4 + 2], values[n * 4 + 3]};
            constraint.amplitude = *amplitude;
          }
          consumed = true;
        } else if (const auto rotAmplitude = amplitudeOf(field.name, kPrescribedRotation); rotAmplitude && field.components == 4) {
          for (std::uint32_t n = 0; n < nodeTotal; ++n) {
            if (values[n * 4] == 0.0) continue;
            auto& constraint = constraintFor(model, constraintIndex, n);
            constraint.prescribedRotation = {values[n * 4 + 1], values[n * 4 + 2], values[n * 4 + 3]};
            constraint.amplitudeRotation = *rotAmplitude;
          }
          consumed = true;
        } else if (const auto forceAmplitude = amplitudeOf(field.name, kNodalForce); forceAmplitude && field.components == 3) {
          for (std::uint32_t n = 0; n < nodeTotal; ++n) {
            const std::array<double, 3> f{values[n * 3], values[n * 3 + 1], values[n * 3 + 2]};
            if (f[0] != 0.0 || f[1] != 0.0 || f[2] != 0.0) loadFor(n, *forceAmplitude).force = f;
          }
          consumed = true;
        } else if (const auto momentAmplitude = amplitudeOf(field.name, kNodalMoment); momentAmplitude && field.components == 3) {
          for (std::uint32_t n = 0; n < nodeTotal; ++n) {
            const std::array<double, 3> m{values[n * 3], values[n * 3 + 1], values[n * 3 + 2]};
            if (m[0] != 0.0 || m[1] != 0.0 || m[2] != 0.0) loadFor(n, *momentAmplitude).moment = m;
          }
          consumed = true;
        } else if (const auto temperatureAmplitude = amplitudeOf(field.name, kPrescribedTemperature); temperatureAmplitude && field.components == 2) {
          for (std::uint32_t n = 0; n < nodeTotal; ++n) {
            if (values[n * 2] != 0.0) model.temperatureConstraints.push_back(TemperatureConstraint{n, values[n * 2 + 1], *temperatureAmplitude});
          }
          consumed = true;
        } else if (const auto heatAmplitude = amplitudeOf(field.name, kNodalHeat); heatAmplitude && field.components == 1) {
          for (std::uint32_t n = 0; n < nodeTotal; ++n) {
            if (values[n] != 0.0) model.heatLoads.push_back(HeatLoad{n, values[n], *heatAmplitude});
          }
          consumed = true;
        } else if (field.name.size() > kInitialPrefix.size() && field.name.starts_with(kInitialPrefix)) {
          model.initialConditions.push_back(InitialCondition{field.name.substr(kInitialPrefix.size()), field.components, values});
          consumed = true;
        }
      }
      it = consumed ? model.fields.erase(it) : it + 1;
    }

    // Amplitudes and damping (global arrays).
    std::optional<Damping> damping;
    for (auto it = model.globalData.begin(); it != model.globalData.end();) {
      bool consumed = false;
      if (it->name.size() > kAmplitudePrefix.size() && it->name.starts_with(kAmplitudePrefix) && it->components == 2) {
        Amplitude amplitude;
        amplitude.name = it->name.substr(kAmplitudePrefix.size());
        for (std::size_t i = 0; i + 1 < it->values.size(); i += 2) {
          amplitude.times.push_back(it->values[i]);
          amplitude.factors.push_back(it->values[i + 1]);
        }
        model.amplitudes.push_back(std::move(amplitude));
        consumed = true;
      } else if (it->name == kRayleighDamping && it->components == 2 && it->values.size() == 2) {
        if (!damping) damping.emplace();
        damping->rayleighAlpha = it->values[0];
        damping->rayleighBeta = it->values[1];
        consumed = true;
      } else if (it->name == kModalDampingRatio && it->components == 1) {
        if (!damping) damping.emplace();
        damping->modalRatios = it->values;
        consumed = true;
      }
      it = consumed ? model.globalData.erase(it) : it + 1;
    }
    if (damping) model.damping = std::move(damping);

    // Beam orientation (3 components, so not part of the scalar attribute loop below).
    if (auto orientation = takeField(model, kBeamOrientation, FieldLocation::Element); orientation) {
      if (orientation->components == 3 && sized(*orientation, elementTotal)) {
        const auto& values = *lastStep(*orientation);
        model.beamOrientation.resize(elementTotal);
        for (std::size_t e = 0; e < elementTotal; ++e) model.beamOrientation[e] = {values[e * 3], values[e * 3 + 1], values[e * 3 + 2]};
      } else {
        model.fields.push_back(std::move(*orientation)); // not a recognised encoding: keep it as an ordinary field
      }
    }

    // Element attributes, sets and tags.
    std::vector<std::uint64_t> nodeTags;
    std::vector<std::uint64_t> elementTags;
    std::vector<int> entityTags;
    for (auto it = model.fields.begin(); it != model.fields.end();) {
      const Field& field = *it;
      const bool isElement = field.location == FieldLocation::Element;
      const std::size_t entities = isElement ? elementTotal : nodeTotal;
      bool consumed = false;
      if (field.components == 1 && sized(field, entities)) {
        const auto& values = field.steps.back();
        if (isElement && (isKnownAttribute(field.name) || field.name.starts_with(kAttributePrefix))) {
          const std::string name = field.name.starts_with(kAttributePrefix) ? field.name.substr(kAttributePrefix.size()) : field.name;
          model.elementAttributes[name] = values;
          consumed = true;
        } else if ((options.nodeSets && field.name.starts_with(kNodeSetPrefix))
                   || (options.elementSets && field.name.starts_with(kElementSetPrefix))) {
          const bool nodeSet = field.name.starts_with(kNodeSetPrefix);
          if (nodeSet == !isElement) {
            EntitySet set;
            set.name = field.name.substr(nodeSet ? kNodeSetPrefix.size() : kElementSetPrefix.size());
            set.kind = nodeSet ? SetKind::Node : SetKind::Element;
            for (std::uint32_t i = 0; i < values.size(); ++i) {
              if (values[i] != 0.0) set.members.push_back(i);
            }
            if (!nodeSet) {
              for (const auto member : set.members) {
                const auto location = model.locateElement(member);
                set.dimension = std::max(set.dimension, elementInfo(model.blocks[location.block].type).dimension);
              }
            }
            model.sets.push_back(std::move(set));
            consumed = true;
          }
        } else if (options.tags && !isElement && field.name == kNodeTag) {
          nodeTags.assign(values.begin(), values.end());
          consumed = true;
        } else if (options.tags && isElement && field.name == kElementTag) {
          elementTags.assign(values.begin(), values.end());
          consumed = true;
        } else if (options.tags && isElement && field.name == kEntityTag) {
          entityTags.assign(values.begin(), values.end());
          consumed = true;
        }
      }
      it = consumed ? model.fields.erase(it) : it + 1;
    }

    if (!nodeTags.empty()) {
      for (std::size_t n = 0; n < nodeTotal; ++n) model.nodes[n].tag = nodeTags[n];
    }
    if (!elementTags.empty() || !entityTags.empty()) {
      std::size_t global = 0;
      for (auto& block : model.blocks) {
        if (!entityTags.empty()) block.entityTags.resize(block.size(), 0);
        for (std::size_t e = 0; e < block.size(); ++e, ++global) {
          if (!elementTags.empty()) block.tags[e] = elementTags[global];
          if (!entityTags.empty()) block.entityTags[e] = entityTags[global];
        }
      }
    }
  }

} // namespace anaf::IO::detail end
