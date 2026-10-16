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

#include "trussMeshAdapter.hpp"

#include <log/anaf_info.hpp>
#include <material/materialLibrary.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace FEM::TRUSS::ADAPTER {

  anaf::IO::MeshModel toMeshModel(const FEM::TRUSS::MeshData& mesh, const std::span<const anaf::MATERIAL::Material> materials) {
    anaf::IO::MeshModel model;
    model.title = "anafinen truss";

    std::unordered_map<std::uint32_t, std::uint32_t> indexById;
    model.nodes.reserve(mesh.trussNodes.size());
    for (std::uint32_t i = 0; i < mesh.trussNodes.size(); ++i) {
      const auto& node = mesh.trussNodes[i];
      indexById.emplace(node.getNodeID(), i);
      model.nodes.push_back(anaf::IO::Node{static_cast<std::uint64_t>(node.getNodeID()) + 1, node.getLocation()});
    }
    auto indexOf = [&](const std::uint32_t id) {
      const auto it = indexById.find(id);
      if (it == indexById.end()) throw std::out_of_range(std::format("snapshot references unknown node {}", id));
      return it->second;
    };

    auto& bars = model.blockFor(anaf::IO::E_ElementType::Line2);
    std::vector<double> material, area, stress;
    std::map<std::uint32_t, std::vector<std::uint32_t>> elementsByMaterial;
    for (std::size_t e = 0; e < mesh.trussElements.size(); ++e) {
      const auto& element = mesh.trussElements[e];
      bars.tags.push_back(e + 1);
      bars.entityTags.push_back(0);
      bars.connectivity.push_back(indexOf(element.node1));
      bars.connectivity.push_back(indexOf(element.node2));
      material.push_back(static_cast<double>(element.materialID));
      elementsByMaterial[element.materialID].push_back(static_cast<std::uint32_t>(e));
      area.push_back(element.crossSectionArea);
      stress.push_back(static_cast<double>(element.stress));
    }
    model.elementAttributes[anaf::IO::Attribute::materialId] = std::move(material);
    model.elementAttributes[anaf::IO::Attribute::crossSectionArea] = std::move(area);

    for (auto& [materialIndex, members] : elementsByMaterial) {
      if (materialIndex >= materials.size()) continue; // no name known: only MaterialID is written
      anaf::IO::EntitySet set;
      set.name = std::string(materialSetPrefix) + std::string(materials[materialIndex].getMaterialType());
      set.kind = anaf::IO::E_SetKind::Element;
      set.dimension = 1;
      set.members = std::move(members);
      model.sets.push_back(std::move(set));
    }

    for (std::uint32_t i = 0; i < mesh.trussNodes.size(); ++i) {
      const auto& node = mesh.trussNodes[i];
      if (!node.isSupported()) continue;
      // The axes outside the allowed subspace; an inclined support also stores its basis.
      const auto& movable = node.getMovable();
      auto allowedMotion = node.hasInclinedSupport() ? node.getAllowedMotionDirections().toVector() : std::vector<std::array<double, 3>>{};
      model.constraints.push_back(anaf::IO::NodeConstraint{i, {!movable[0], !movable[1], !movable[2]}, std::move(allowedMotion), {}, {}});
    }
    std::size_t harmonicSkipped = 0;
    for (const auto& force : mesh.appliedForces) {
      const auto it = indexById.find(force.getAppliedNode());
      if (it == indexById.end()) continue;
      if (force.getForceType() == E_ForceType::Harmonic) {
        ++harmonicSkipped; // no frequency-domain load in MeshModel yet
        continue;
      }
      std::string amplitude;
      if (const auto* tabular = force.getTabular()) {
        amplitude = std::format("TabularForce{}", model.amplitudes.size());
        model.amplitudes.push_back(anaf::IO::Amplitude{amplitude, tabular->times, tabular->factors});
      }
      model.loads.push_back(anaf::IO::NodalLoad{it->second, force.getForce(), std::move(amplitude), {}});
    }
    if (harmonicSkipped > 0) anaf::LOG::warn("{} harmonic loads cannot be written to a mesh file yet and were left out", harmonicSkipped);

    if (mesh.hasResults) {
      anaf::IO::Field displacement{anaf::IO::FieldName::displacement, anaf::IO::E_FieldLocation::Node, 3, {0.0}, {}, anaf::IO::E_StepKind::Time, {}};
      std::vector<double> values;
      values.reserve(mesh.trussNodes.size() * 3);
      for (const auto& node : mesh.trussNodes) {
        const auto& d = node.getDisplacement();
        values.insert(values.end(), d.begin(), d.end());
      }
      displacement.steps.push_back(std::move(values));
      model.fields.push_back(std::move(displacement));
      model.fields.push_back(anaf::IO::Field{anaf::IO::FieldName::stress, anaf::IO::E_FieldLocation::Element, 1, {0.0}, {std::move(stress)}, anaf::IO::E_StepKind::Time, {}});
    }
    return model;
  }

  std::expected<ImportedTruss, std::string> toMeshData(const anaf::IO::MeshModel& model,
                                                       const std::span<const anaf::MATERIAL::Material> materials) {
    if (const auto problems = model.validate(); !problems.empty()) return std::unexpected("the model is inconsistent: " + problems.front());
    ImportedTruss result;
    result.mesh = std::make_shared<FEM::TRUSS::MeshData>();
    auto& mesh = *result.mesh;

    const anaf::IO::Field* displacement = model.findField(anaf::IO::FieldName::displacement, anaf::IO::E_FieldLocation::Node);
    if (displacement && (displacement->components != 3 || displacement->steps.empty())) displacement = nullptr;
    const anaf::IO::Field* stress = model.findField(anaf::IO::FieldName::stress, anaf::IO::E_FieldLocation::Element);
    if (stress && (stress->components != 1 || stress->steps.empty())) stress = nullptr;
    mesh.hasResults = displacement || stress;

    mesh.trussNodes.reserve(model.nodes.size());
    for (std::uint32_t i = 0; i < model.nodes.size(); ++i) {
      const auto& p = model.nodes[i].position;
      FEM::TRUSS::Node node(i, p[0], p[1], p[2]);
      if (displacement) {
        const auto& d = displacement->steps.back();
        node.setDisplacements({d[i * 3], d[i * 3 + 1], d[i * 3 + 2]});
      }
      mesh.trussNodes.push_back(std::move(node));
    }

    const auto attribute = [&](const char* name) -> const std::vector<double>* {
      const auto it = model.elementAttributes.find(name);
      return it == model.elementAttributes.end() ? nullptr : &it->second;
    };
    const auto* material = attribute(anaf::IO::Attribute::materialId);
    const auto* area = attribute(anaf::IO::Attribute::crossSectionArea);

    // Material by name wins over the MaterialID index (see materialSetPrefix).
    std::vector<std::optional<std::uint32_t>> materialByName(model.elementCount());
    for (const auto& set : model.sets) {
      if (set.kind != anaf::IO::E_SetKind::Element || !set.name.starts_with(materialSetPrefix)) continue;
      const std::string_view name = std::string_view(set.name).substr(materialSetPrefix.size());
      const auto found = std::ranges::find_if(materials, [&](const anaf::MATERIAL::Material& candidate) {
        return anaf::MATERIAL::sameMaterialName(candidate.getMaterialType(), name);
      });
      std::uint32_t index = 0;
      if (found != materials.end()) {
        index = static_cast<std::uint32_t>(found - materials.begin());
      } else {
        result.notes.push_back(std::format("warning: material '{}' is not in the material list; its {} elements use '{}'",
                                           name, set.members.size(), materials.empty() ? "none" : std::string(materials[0].getMaterialType())));
      }
      for (const auto member : set.members) {
        if (member < materialByName.size()) materialByName[member] = index;
      }
    }
    std::size_t invalidMaterialIds = 0;

    std::size_t bars = 0, splitQuadratic = 0, wireframeElements = 0, points = 0;
    std::set<std::pair<std::uint32_t, std::uint32_t>> wireframeEdges;
    std::size_t global = 0;
    for (const auto& block : model.blocks) {
      const auto& info = anaf::IO::elementInfo(block.type);
      const auto n = static_cast<std::size_t>(info.nodeCount);
      for (std::size_t e = 0; e < block.size(); ++e, ++global) {
        const std::uint32_t* nodes = &block.connectivity[e * n];
        if (info.dimension == 0) {
          ++points;
          continue;
        }
        if (info.dimension == 1) {
          const float s = stress ? static_cast<float>(stress->steps.back()[global]) : 0.0f;
          std::uint32_t materialId = 0;
          if (materialByName[global]) {
            materialId = *materialByName[global];
          } else if (material) {
            const double value = (*material)[global];
            if (value >= 0.0 && value < static_cast<double>(materials.size()) && value == std::floor(value)) {
              materialId = static_cast<std::uint32_t>(value);
            } else {
              ++invalidMaterialIds;
            }
          }
          const bool exceeded = materialId < materials.size() && std::abs(s) > materials[materialId].getYieldTensile();
          for (std::size_t k = 0; k + 1 < info.edges.size(); k += 2) {
            mesh.trussElements.push_back({nodes[info.edges[k]], nodes[info.edges[k + 1]], s, exceeded, materialId,
                                          area ? (*area)[global] : 0.0, false});
          }
          block.type == anaf::IO::E_ElementType::Line2 ? ++bars : ++splitQuadratic;
          continue;
        }
        ++wireframeElements;
        for (std::size_t k = 0; k + 1 < info.edges.size(); k += 2) {
          const auto a = nodes[info.edges[k]];
          const auto b = nodes[info.edges[k + 1]];
          wireframeEdges.emplace(std::min(a, b), std::max(a, b));
        }
      }
    }
    for (const auto& [a, b] : wireframeEdges) mesh.trussElements.push_back({a, b, 0.0f, false, 0u, 0.0, true});

    for (const auto& constraint : model.constraints) {
      auto& node = mesh.trussNodes[constraint.node];
      node.setMovable({!constraint.fixed[0], !constraint.fixed[1], !constraint.fixed[2]});
      if (!constraint.allowedMotion.empty()) {
        try {
          node.setAllowedMotionDirections(constraint.allowedMotion);
        } catch (const std::exception&) {
          result.notes.push_back(std::format("node {}: invalid inclined support basis ignored", constraint.node));
        }
      }
    }
    for (const auto& load : model.loads) {
      if (load.amplitude.empty()) {
        mesh.appliedForces.emplace_back(load.node, load.force);
        continue;
      }
      const anaf::IO::Amplitude* amplitude = model.findAmplitude(load.amplitude);
      if (amplitude == nullptr) {
        result.notes.push_back(std::format("warning: node {}: amplitude '{}' not found; the load is taken as constant", load.node, load.amplitude));
        mesh.appliedForces.emplace_back(load.node, load.force);
        continue;
      }
      try {
        mesh.appliedForces.emplace_back(load.node, load.force, TabularForce{amplitude->times, amplitude->factors});
      } catch (const std::exception& exception) {
        result.notes.push_back(std::format("warning: node {}: amplitude '{}' ignored: {}", load.node, load.amplitude, exception.what()));
        mesh.appliedForces.emplace_back(load.node, load.force);
      }
    }

    result.notes.push_back(std::format("{} nodes, {} bars", model.nodes.size(), bars));
    if (splitQuadratic > 0) result.notes.push_back(std::format("{} quadratic line elements shown as two straight segments", splitQuadratic));
    if (wireframeElements > 0) {
      result.notes.push_back(std::format("{} surface/volume elements shown as wireframe ({} edges); the truss solver uses bars only",
                                         wireframeElements, wireframeEdges.size()));
    }
    if (points > 0) result.notes.push_back(std::format("{} point elements ignored", points));
    if (invalidMaterialIds > 0) {
      result.notes.push_back(std::format("warning: {} elements have a MaterialID outside the material list and use material 0", invalidMaterialIds));
    }
    return result;
  }

} // namespace FEM::TRUSS::ADAPTER end
