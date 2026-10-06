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

#include "beamMeshAdapter.hpp"

#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp> // elementSectionProperties, elementLocalLoads
#include <beam/beamEngine/beamStress.hpp>
#include <beam/beamSection/sectionLibrary.hpp>
#include <material/materialLibrary.hpp>
#include <objectCalcs/common/supportBasis.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <utility>

namespace FEM::BEAM::ADAPTER {

  namespace {
    
    template <class... Ts>
    struct Overloaded : Ts... {
      using Ts::operator()...;
    };

    constexpr std::array<const char*, 5> kDimensions{
      anaf::IO::Attribute::SectionDimension1, anaf::IO::Attribute::SectionDimension2, anaf::IO::Attribute::SectionDimension3,
      anaf::IO::Attribute::SectionDimension4, anaf::IO::Attribute::SectionDimension5};
    constexpr std::array<const char*, 3> kLoadGlobal{anaf::IO::Attribute::UniformLoadGlobalX, anaf::IO::Attribute::UniformLoadGlobalY, anaf::IO::Attribute::UniformLoadGlobalZ};
    constexpr std::array<const char*, 3> kLoadLocal{anaf::IO::Attribute::UniformLoadLocalX, anaf::IO::Attribute::UniformLoadLocalY, anaf::IO::Attribute::UniformLoadLocalZ};

    std::array<double, 5> dimensionsOf(const SectionShape& shape) {
      return std::visit(Overloaded{
        [](const GeneralSection&) { return std::array<double, 5>{}; },
        [](const RectangleSection& s) { return std::array<double, 5>{s.height, s.width, 0.0, 0.0, 0.0}; },
        [](const CircleSection& s) { return std::array<double, 5>{s.diameter, 0.0, 0.0, 0.0, 0.0}; },
        [](const PipeSection& s) { return std::array<double, 5>{s.outerDiameter, s.wallThickness, 0.0, 0.0, 0.0}; },
        [](const BoxSection& s) { return std::array<double, 5>{s.height, s.width, s.wallThickness, s.outerCornerRadius, s.innerCornerRadius}; },
        [](const ISection& s) { return std::array<double, 5>{s.height, s.flangeWidth, s.webThickness, s.flangeThickness, s.rootRadius}; },
      }, shape);
    }

    std::optional<SectionShape> shapeFrom(const double code, const std::array<double, 5>& d, const SectionProperties& numbers) {
      switch (static_cast<int>(std::lround(code))) {
        case 0: return GeneralSection{numbers};
        case 1: return RectangleSection{d[0], d[1]};
        case 2: return CircleSection{d[0]};
        case 3: return PipeSection{d[0], d[1]};
        case 4: return BoxSection{d[0], d[1], d[2], d[3], d[4]};
        case 5: return ISection{d[0], d[1], d[2], d[3], d[4]};
        default: return std::nullopt;
      }
    }

    bool close(const double a, const double b) { return std::abs(a - b) <= 1e-9 * std::max({std::abs(a), std::abs(b), 1e-300}); }

    bool sameShape(const SectionShape& a, const SectionShape& b) {
      if (a.index() != b.index()) return false;
      if (const auto* ga = std::get_if<GeneralSection>(&a)) {
        const auto& x = ga->values;
        const auto& y = std::get<GeneralSection>(b).values;
        return close(x.area, y.area) && close(x.secondMomentY, y.secondMomentY) && close(x.secondMomentZ, y.secondMomentZ)
          && close(x.torsionConstant, y.torsionConstant) && close(x.shearAreaY, y.shearAreaY) && close(x.shearAreaZ, y.shearAreaZ);
      }
      const auto da = dimensionsOf(a), db = dimensionsOf(b);
      return std::ranges::equal(da, db, close);
    }

    bool axisFree(const std::vector<std::array<double, 3>>& basis, const std::size_t axis) {
      std::array<double, 3> unit{};
      unit[axis] = 1.0;
      const auto rest = FEM::SUPPORT::componentOutside(unit, basis);
      return std::sqrt(rest[0] * rest[0] + rest[1] * rest[1] + rest[2] * rest[2]) <= 1e-12;
    }

    bool alongGlobalAxes(const std::vector<std::array<double, 3>>& basis) {
      return std::ranges::all_of(basis, [](const std::array<double, 3>& v) { return std::ranges::count(v, 0.0) == 2; });
    }

    std::vector<std::array<double, 3>> freeAxes(const std::array<bool, 3>& fixed) {
      std::vector<std::array<double, 3>> axes;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (fixed[axis]) continue;
        std::array<double, 3> unit{};
        unit[axis] = 1.0;
        axes.push_back(unit);
      }
      return axes;
    }

    anaf::IO::Field field(const char* name, const anaf::IO::FieldLocation location, const int components, std::vector<double> values) {
      return anaf::IO::Field{name, location, components, {0.0}, {std::move(values)}, anaf::IO::StepKind::Time, {}};
    }
  } // namespace end

  bool isBeamModel(const anaf::IO::MeshModel& model) {
    const auto it = model.elementAttributes.find(anaf::IO::Attribute::ElementFormulation);
    return it != model.elementAttributes.end() && std::ranges::any_of(it->second, [](const double v) { return v >= 0.5; });
  }

  anaf::IO::MeshModel toMeshModel(const MeshData& mesh, const std::span<const anaf::MATERIAL::Material> materials,
                            const std::span<const BeamSection> sections) {
    anaf::IO::MeshModel model;
    model.title = "anafinen beam";
    model.nodes.reserve(mesh.nodes.size());
    for (const auto& node : mesh.nodes) model.nodes.push_back(anaf::IO::Node{static_cast<std::uint64_t>(node.getNodeID()) + 1, node.getLocation()});

    const std::size_t count = mesh.elements.size();
    auto& block = model.blockFor(anaf::IO::ElementType::Line2);
    std::vector<double> material(count), area(count), iy(count), iz(count), torsion(count), asy(count), asz(count), formulation(count), shape(count),
      releases(count);
    std::array<std::vector<double>, 5> dimensions;
    std::array<std::vector<double>, 3> loadGlobal, loadLocal;
    for (auto& v : dimensions) v.assign(count, 0.0);
    for (auto& v : loadGlobal) v.assign(count, 0.0);
    for (auto& v : loadLocal) v.assign(count, 0.0);
    std::map<std::uint32_t, std::vector<std::uint32_t>> byMaterial, bySection;
    model.beamOrientation.reserve(count);

    for (std::size_t e = 0; e < count; ++e) {
      const auto& element = mesh.elements[e];
      block.tags.push_back(e + 1);
      block.entityTags.push_back(0);
      block.connectivity.push_back(element.node1);
      block.connectivity.push_back(element.node2);
      material[e] = static_cast<double>(element.materialID);
      formulation[e] = static_cast<double>(element.formulation == Formulation::Timoshenko ? anaf::IO::ElementFormulation::TimoshenkoBeam
                                                                                          : anaf::IO::ElementFormulation::EulerBernoulliBeam);
      model.beamOrientation.push_back(element.orientation);
      releases[e] = static_cast<double>(element.endReleases);
      byMaterial[element.materialID].push_back(static_cast<std::uint32_t>(e));
      if (element.sectionID < sections.size()) {
        bySection[element.sectionID].push_back(static_cast<std::uint32_t>(e));
        const auto& sectionShape = sections[element.sectionID].getShape();
        const double poisson = element.materialID < materials.size() ? materials[element.materialID].getPoisson() : 0.3;
        const auto p = computeProperties(sectionShape, poisson);
        area[e] = p.area;
        iy[e] = p.secondMomentY;
        iz[e] = p.secondMomentZ;
        torsion[e] = p.torsionConstant;
        asy[e] = p.shearAreaY;
        asz[e] = p.shearAreaZ;
        shape[e] = static_cast<double>(sectionShape.index());
        const auto d = dimensionsOf(sectionShape);
        for (std::size_t k = 0; k < 5; ++k) dimensions[k][e] = d[k];
      }
    }
    for (const auto& load : mesh.distributedLoads) {
      if (load.element >= count) continue;
      auto& target = load.frame == LoadFrame::Local ? loadLocal : loadGlobal;
      for (std::size_t axis = 0; axis < 3; ++axis) target[axis][load.element] += load.value[axis];
    }

    auto& attributes = model.elementAttributes;
    attributes[anaf::IO::Attribute::MaterialId] = std::move(material);
    attributes[anaf::IO::Attribute::CrossSectionArea] = std::move(area);
    attributes[anaf::IO::Attribute::SecondMomentY] = std::move(iy);
    attributes[anaf::IO::Attribute::SecondMomentZ] = std::move(iz);
    attributes[anaf::IO::Attribute::TorsionConstant] = std::move(torsion);
    attributes[anaf::IO::Attribute::ShearAreaY] = std::move(asy);
    attributes[anaf::IO::Attribute::ShearAreaZ] = std::move(asz);
    attributes[anaf::IO::Attribute::ElementFormulation] = std::move(formulation);
    attributes[anaf::IO::Attribute::SectionShape] = std::move(shape);
    if (std::ranges::any_of(releases, [](const double v) { return v != 0.0; })) attributes[anaf::IO::Attribute::EndReleases] = std::move(releases);
    for (std::size_t k = 0; k < 5; ++k) attributes[kDimensions[k]] = std::move(dimensions[k]);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      attributes[kLoadGlobal[axis]] = std::move(loadGlobal[axis]);
      attributes[kLoadLocal[axis]] = std::move(loadLocal[axis]);
    }

    const auto addSet = [&](std::string name, std::vector<std::uint32_t> members) {
      anaf::IO::EntitySet set;
      set.name = std::move(name);
      set.kind = anaf::IO::SetKind::Element;
      set.dimension = 1;
      set.members = std::move(members);
      model.sets.push_back(std::move(set));
    };
    for (auto& [index, members] : byMaterial) {
      if (index < materials.size()) addSet(std::string(kMaterialSetPrefix) + std::string(materials[index].getMaterialType()), std::move(members));
    }
    for (auto& [index, members] : bySection) addSet(std::string(kSectionSetPrefix) + sections[index].getName(), std::move(members));
    model.globalData.push_back(anaf::IO::GlobalArray{anaf::IO::GlobalName::Gravity, 3, {mesh.gravity.begin(), mesh.gravity.end()}});

    for (std::uint32_t i = 0; i < mesh.nodes.size(); ++i) {
      const auto& node = mesh.nodes[i];
      if (!node.isSupported()) continue;
      const auto& motion = node.getAllowedMotionDirections();
      const auto& rotation = node.getAllowedRotationAxes();
      anaf::IO::NodeConstraint constraint;
      constraint.node = i;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        constraint.fixed[axis] = !axisFree(motion, axis);
        constraint.fixedRotation[axis] = !axisFree(rotation, axis);
      }
      if (!alongGlobalAxes(motion)) constraint.allowedMotion = motion;
      if (!alongGlobalAxes(rotation)) {
        model.warnings.push_back(std::format("node {}: the inclined rotation support is written as fixed about every global axis "
                                             "outside its span (files keep rotational fixity per global axis only)", i));
      }
      model.constraints.push_back(std::move(constraint));
    }
    for (const auto& load : mesh.nodalLoads) model.loads.push_back(anaf::IO::NodalLoad{load.node, load.force, {}, load.moment});

    if (mesh.hasResults) {
      std::vector<double> displacement, rotation;
      displacement.reserve(mesh.nodes.size() * 3);
      rotation.reserve(mesh.nodes.size() * 3);
      for (const auto& node : mesh.nodes) {
        displacement.insert(displacement.end(), node.getDisplacement().begin(), node.getDisplacement().end());
        rotation.insert(rotation.end(), node.getRotation().begin(), node.getRotation().end());
      }
      std::vector<double> sectionForces, axial, vonMises;
      sectionForces.reserve(count * 12);
      for (const auto& element : mesh.elements) {
        sectionForces.insert(sectionForces.end(), element.sectionForces.begin(), element.sectionForces.end());
        axial.push_back((element.sectionForces[0] + element.sectionForces[6]) / 2.0);
        vonMises.push_back(element.stress.available ? element.stress.maxVonMises : 0.0);
      }
      model.fields.push_back(field(anaf::IO::FieldName::Displacement, anaf::IO::FieldLocation::Node, 3, std::move(displacement)));
      model.fields.push_back(field(anaf::IO::FieldName::Rotation, anaf::IO::FieldLocation::Node, 3, std::move(rotation)));
      model.fields.push_back(field(anaf::IO::FieldName::BeamSectionForce, anaf::IO::FieldLocation::Element, 12, std::move(sectionForces)));
      model.fields.push_back(field(anaf::IO::FieldName::AxialForce, anaf::IO::FieldLocation::Element, 1, std::move(axial)));
      model.fields.push_back(field(anaf::IO::FieldName::VonMisesStress, anaf::IO::FieldLocation::Element, 1, std::move(vonMises)));
    }
    return model;
  }

  std::expected<ImportedBeam, std::string> toMeshData(const anaf::IO::MeshModel& model, const std::span<const anaf::MATERIAL::Material> materials,
                                                      const std::span<const BeamSection> sections) {
    if (materials.empty()) return std::unexpected("the material list is empty");
    ImportedBeam result;
    result.mesh = std::make_shared<MeshData>();
    auto& mesh = *result.mesh;

    for (std::uint32_t i = 0; i < model.nodes.size(); ++i) {
      const auto& p = model.nodes[i].position;
      mesh.nodes.emplace_back(i, p[0], p[1], p[2]);
    }

    const auto attribute = [&](const char* name) -> const std::vector<double>* {
      const auto it = model.elementAttributes.find(name);
      return it == model.elementAttributes.end() ? nullptr : &it->second;
    };
    const auto value = [](const std::vector<double>* values, const std::size_t global) { return values ? (*values)[global] : 0.0; };
    const auto* materialIds = attribute(anaf::IO::Attribute::MaterialId);
    const auto* formulations = attribute(anaf::IO::Attribute::ElementFormulation);
    const auto* shapes = attribute(anaf::IO::Attribute::SectionShape);
    const auto* releases = attribute(anaf::IO::Attribute::EndReleases);
    const std::array<const std::vector<double>*, 6> numbers{
      attribute(anaf::IO::Attribute::CrossSectionArea), attribute(anaf::IO::Attribute::SecondMomentY), attribute(anaf::IO::Attribute::SecondMomentZ),
      attribute(anaf::IO::Attribute::TorsionConstant), attribute(anaf::IO::Attribute::ShearAreaY), attribute(anaf::IO::Attribute::ShearAreaZ)};
    std::array<const std::vector<double>*, 5> dimensions{};
    for (std::size_t k = 0; k < 5; ++k) dimensions[k] = attribute(kDimensions[k]);

    // Names from the element sets (materials and sections travel by name).
    std::vector<std::optional<std::uint32_t>> materialByName(model.elementCount());
    std::vector<std::string> sectionName(model.elementCount());
    for (const auto& set : model.sets) {
      if (set.kind != anaf::IO::SetKind::Element) continue;
      if (set.name.starts_with(kMaterialSetPrefix)) {
        const std::string_view name = std::string_view(set.name).substr(kMaterialSetPrefix.size());
        const auto found = std::ranges::find_if(materials, [&](const anaf::MATERIAL::Material& candidate) {
          return anaf::MATERIAL::sameMaterialName(candidate.getMaterialType(), name);
        });
        std::uint32_t index = 0;
        if (found != materials.end()) {
          index = static_cast<std::uint32_t>(found - materials.begin());
        } else {
          result.notes.push_back(std::format("warning: material '{}' is not in the material list; its {} elements use '{}'",
                                             name, set.members.size(), materials[0].getMaterialType()));
        }
        for (const auto member : set.members) {
          if (member < materialByName.size()) materialByName[member] = index;
        }
      } else if (set.name.starts_with(kSectionSetPrefix)) {
        for (const auto member : set.members) {
          if (member < sectionName.size()) sectionName[member] = set.name.substr(kSectionSetPrefix.size());
        }
      }
    }

    // Section lookup: a known name with the same shape reuses the list entry; anything else
    // becomes a new section (one per distinct name and shape) with a name unique in both lists.
    const auto nameTaken = [&](const std::string& name) {
      const auto same = [&](const BeamSection& s) { return sameSectionName(s.getName(), name); };
      return std::ranges::any_of(sections, same) || std::ranges::any_of(result.newSections, same);
    };
    std::size_t unnamed = 0;
    std::vector<std::string> requestedName; // the file's name of each new section ("" = unnamed)
    const auto resolveSection = [&](const std::string& name, const SectionShape& shape) -> std::uint32_t {
      if (!name.empty()) {
        for (std::uint32_t i = 0; i < sections.size(); ++i) {
          if (sameSectionName(sections[i].getName(), name) && sameShape(sections[i].getShape(), shape)) return i;
        }
      }
      for (std::uint32_t i = 0; i < result.newSections.size(); ++i) {
        if (requestedName[i] == name && sameShape(result.newSections[i].getShape(), shape)) {
          return static_cast<std::uint32_t>(sections.size() + i);
        }
      }
      std::string unique;
      if (name.empty()) {
        do unique = std::format("Imported section {}", ++unnamed); while (nameTaken(unique));
      } else {
        unique = name;
        for (int k = 1; nameTaken(unique); ++k) unique = k == 1 ? name + " (imported)" : std::format("{} (imported {})", name, k);
        if (unique != name) result.notes.push_back(std::format("section '{}' differs from the one in the list; imported as '{}'", name, unique));
      }
      result.newSections.emplace_back(unique, shape);
      requestedName.push_back(name);
      return static_cast<std::uint32_t>(sections.size() + result.newSections.size() - 1);
    };

    std::vector<std::uint32_t> beamOfGlobal(model.elementCount(), static_cast<std::uint32_t>(-1));
    std::size_t bars = 0, skipped = 0, invalidMaterialIds = 0, invalidReleases = 0;
    std::size_t global = 0;
    for (const auto& block : model.blocks) {
      const auto n = static_cast<std::size_t>(anaf::IO::elementInfo(block.type).nodeCount);
      for (std::size_t e = 0; e < block.size(); ++e, ++global) {
        if (block.type != anaf::IO::ElementType::Line2) {
          ++skipped;
          continue;
        }
        BeamElement element;
        element.node1 = block.connectivity[e * n];
        element.node2 = block.connectivity[e * n + 1];
        const double code = value(formulations, global);
        if (code >= 1.5) element.formulation = Formulation::Timoshenko;
        else if (code < 0.5) ++bars;

        if (materialByName[global]) {
          element.materialID = *materialByName[global];
        } else if (materialIds) {
          const double id = (*materialIds)[global];
          if (id >= 0.0 && id < static_cast<double>(materials.size()) && id == std::floor(id)) element.materialID = static_cast<std::uint32_t>(id);
          else ++invalidMaterialIds;
        }

        const SectionProperties given{value(numbers[0], global), value(numbers[1], global), value(numbers[2], global),
                                      value(numbers[3], global), value(numbers[4], global), value(numbers[5], global)};
        std::array<double, 5> d{};
        for (std::size_t k = 0; k < 5; ++k) d[k] = value(dimensions[k], global);
        const auto shape = shapes ? shapeFrom((*shapes)[global], d, given) : std::optional<SectionShape>(GeneralSection{given});
        if (!shape) return std::unexpected(std::format("element {}: unknown section shape code {}", global, value(shapes, global)));
        if (const auto valid = validateShape(*shape); !valid) {
          return std::unexpected(std::format("element {}: no usable section ({}); a beam needs its shape or A, Iy, Iz and J", global, valid.error()));
        }
        element.sectionID = resolveSection(sectionName[global], *shape);
        if (global < model.beamOrientation.size()) element.orientation = model.beamOrientation[global];
        if (const double bits = value(releases, global); bits != 0.0) {
          if (bits > 0.0 && bits <= static_cast<double>(RELEASE::allMask) && bits == std::floor(bits)) element.endReleases = static_cast<std::uint16_t>(bits);
          else ++invalidReleases;
        }

        beamOfGlobal[global] = static_cast<std::uint32_t>(mesh.elements.size());
        std::array<double, 3> qGlobal{}, qLocal{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
          qGlobal[axis] = value(attribute(kLoadGlobal[axis]), global);
          qLocal[axis] = value(attribute(kLoadLocal[axis]), global);
        }
        const auto index = static_cast<std::uint32_t>(mesh.elements.size());
        if (qGlobal != std::array<double, 3>{}) mesh.distributedLoads.push_back({index, qGlobal, LoadFrame::Global});
        if (qLocal != std::array<double, 3>{}) mesh.distributedLoads.push_back({index, qLocal, LoadFrame::Local});
        mesh.elements.push_back(element);
      }
    }
    if (mesh.elements.empty()) return std::unexpected("the file has no line elements (Line2) to use as beams");

    std::size_t prescribed = 0;
    for (const auto& constraint : model.constraints) {
      auto& node = mesh.nodes[constraint.node];
      try {
        node.setAllowedMotionDirections(constraint.allowedMotion.empty() ? freeAxes(constraint.fixed) : constraint.allowedMotion);
      } catch (const std::exception&) {
        node.setAllowedMotionDirections(freeAxes(constraint.fixed));
        result.notes.push_back(std::format("warning: node {}: invalid inclined support basis, global axes used", constraint.node));
      }
      node.setAllowedRotationAxes(freeAxes(constraint.fixedRotation));
      if (constraint.prescribed != std::array<double, 3>{} || constraint.prescribedRotation != std::array<double, 3>{}) ++prescribed;
    }
    std::size_t withAmplitude = 0;
    for (const auto& load : model.loads) {
      mesh.nodalLoads.push_back({load.node, load.force, load.moment});
      if (!load.amplitude.empty()) ++withAmplitude;
    }
    if (const auto* gravity = model.findGlobal(anaf::IO::GlobalName::Gravity); gravity && gravity->components == 3 && gravity->values.size() == 3) {
      mesh.gravity = {gravity->values[0], gravity->values[1], gravity->values[2]};
    } else {
      result.notes.push_back("no gravity in the file: self weight is on (0, -9.80665, 0)");
    }

    // Results; the stresses are computed again from the section forces.
    const auto* displacement = model.findField(anaf::IO::FieldName::Displacement, anaf::IO::FieldLocation::Node);
    const auto* rotation = model.findField(anaf::IO::FieldName::Rotation, anaf::IO::FieldLocation::Node);
    const auto* sectionForce = model.findField(anaf::IO::FieldName::BeamSectionForce, anaf::IO::FieldLocation::Element);
    if (displacement && displacement->components == 3 && !displacement->steps.empty() && sectionForce && sectionForce->components == 12
        && !sectionForce->steps.empty()) {
      const auto& d = displacement->steps.back();
      for (std::uint32_t i = 0; i < mesh.nodes.size(); ++i) {
        mesh.nodes[i].setDisplacement({d[i * 3], d[i * 3 + 1], d[i * 3 + 2]});
        if (rotation && rotation->components == 3 && !rotation->steps.empty()) {
          const auto& r = rotation->steps.back();
          mesh.nodes[i].setRotation({r[i * 3], r[i * 3 + 1], r[i * 3 + 2]});
        }
      }
      const auto& s = sectionForce->steps.back();
      for (std::size_t g = 0; g < beamOfGlobal.size(); ++g) {
        if (beamOfGlobal[g] == static_cast<std::uint32_t>(-1)) continue;
        auto& forces = mesh.elements[beamOfGlobal[g]].sectionForces;
        for (std::size_t k = 0; k < 12; ++k) forces[k] = s[g * 12 + k];
      }
      mesh.hasResults = true;
      std::vector<BeamSection> all(sections.begin(), sections.end());
      all.insert(all.end(), result.newSections.begin(), result.newSections.end());
      try {
        const auto properties = elementSectionProperties(mesh.elements, all, materials);
        const auto loads = elementLocalLoads(mesh.nodes, mesh.elements, properties, mesh.distributedLoads, mesh.gravity, materials);
        for (std::size_t e = 0; e < mesh.elements.size(); ++e) {
          auto& element = mesh.elements[e];
          const auto& a = mesh.nodes[element.node1].getLocation();
          const auto& b = mesh.nodes[element.node2].getLocation();
          const double length = std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
          element.stress = elementStress(element, loads[e], length, all[element.sectionID].getShape(),
                                         materials[element.materialID].getYieldTensile());
        }
      } catch (const std::exception& exception) {
        result.notes.push_back(std::format("warning: stresses not computed: {}", exception.what()));
      }
    }

    result.notes.push_back(std::format("{} nodes, {} beam elements", mesh.nodes.size(), mesh.elements.size()));
    if (bars > 0) result.notes.push_back(std::format("{} bar elements imported as Euler-Bernoulli beams", bars));
    if (skipped > 0) result.notes.push_back(std::format("warning: {} elements that are not Line2 were skipped (the beam solver uses Line2)", skipped));
    if (!result.newSections.empty()) result.notes.push_back(std::format("{} sections added from the file", result.newSections.size()));
    if (invalidMaterialIds > 0) result.notes.push_back(std::format("warning: {} elements have a MaterialID outside the list and use material 0", invalidMaterialIds));
    if (invalidReleases > 0) result.notes.push_back(std::format("warning: {} elements have an invalid EndReleases value (ignored: rigid ends)", invalidReleases));
    if (const auto hinged = std::ranges::count_if(mesh.elements, [](const BeamElement& e) { return e.endReleases != 0; }); hinged > 0) {
      result.notes.push_back(std::format("{} elements with end releases (hinges)", hinged));
    }
    if (prescribed > 0) result.notes.push_back(std::format("warning: prescribed displacements / rotations on {} nodes are ignored (held at zero)", prescribed));
    if (withAmplitude > 0) result.notes.push_back(std::format("warning: {} loads have an amplitude; static solve uses the reference value", withAmplitude));
    return result;
  }

} // namespace FEM::BEAM::ADAPTER end
