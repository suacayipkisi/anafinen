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

#include "beamSolver.hpp"
#include "beamSolver/deformationUnderConstForce.hpp"
#include "beamStress.hpp"

#include <log/anaf_info.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <vector>

namespace FEM::BEAM {

  namespace {

    bool positive(const double value) { return std::isfinite(value) && value > 0.0; }

    // Why the element cannot be solved, or an empty string. properties is filled for a valid element.
    std::string elementProblem(const BeamElement& element, const std::size_t nodeCount,
                               const std::span<const anaf::MATERIAL::Material> materials,
                               const std::span<const BeamSection> sections, SectionProperties& properties) {
      if (element.node1 >= nodeCount || element.node2 >= nodeCount) {
        return std::format("references node {}, but the model has {} nodes", std::max(element.node1, element.node2), nodeCount);
      }
      if (element.node1 == element.node2) return std::format("starts and ends at node {}", element.node1);
      if (element.materialID >= materials.size()) return "uses a material that is not in the material list";
      if (element.sectionID >= sections.size()) return "uses a section that is not in the section list";
      const auto& material = materials[element.materialID];
      if (!positive(material.getElasticityModulus()) || !positive(material.getShearModulus())) {
        return std::format("material '{}' needs a positive E and G", material.getMaterialType());
      }
      const auto& section = sections[element.sectionID];
      if (const auto valid = validateShape(section.getShape()); !valid) {
        return std::format("section '{}': {}", section.getName(), valid.error());
      }
      properties = computeProperties(section.getShape(), material.getPoisson());
      if (!positive(properties.area)) return "the cross-section area must be positive";
      if (!positive(properties.secondMomentY) || !positive(properties.secondMomentZ)) return "Iy and Iz must be positive";
      if (!positive(properties.torsionConstant)) return "the torsion constant J must be positive";
      if (element.formulation == Formulation::Timoshenko
          && (!positive(properties.shearAreaY) || !positive(properties.shearAreaZ))) {
        return std::format("a Timoshenko element needs positive shear areas Asy and Asz (section '{}')", section.getName());
      }
      if ((element.endReleases & ~RELEASE::allMask) != 0) return std::format("unknown end release bits {:#x}", element.endReleases);
      if (!std::ranges::all_of(element.orientation, [](const double v) { return std::isfinite(v); })) {
        return "the orientation vector is not finite";
      }
      return {};
    }

    // The checked model and the section properties of its elements.
    struct SolverModel {
      MeshData mesh;
      std::vector<SectionProperties> properties;
    };

    std::expected<SolverModel, std::string> buildSolverModel(const MeshData& mesh,
                                                             const std::span<const anaf::MATERIAL::Material> materials,
                                                             const std::span<const BeamSection> sections) {
      const std::size_t nodeCount = mesh.nodes.size();
      if (nodeCount == 0) return std::unexpected("the model has no nodes");
      if (mesh.elements.empty()) return std::unexpected("the model has no beam elements");

      SolverModel solverModel{mesh, std::vector<SectionProperties>(mesh.elements.size())};
      MeshData& model = solverModel.mesh;
      for (std::size_t i = 0; i < nodeCount; ++i) {
        if (model.nodes[i].getNodeID() != i) {
          return std::unexpected(std::format("node at position {} has id {}; node ids must be 0..{} in order",
                                             i, model.nodes[i].getNodeID(), nodeCount - 1));
        }
        model.nodes[i].setDisplacement({0.0, 0.0, 0.0});
        model.nodes[i].setRotation({0.0, 0.0, 0.0});
      }

      std::vector<bool> used(nodeCount, false);
      std::size_t timoshenko = 0;
      for (std::size_t e = 0; e < model.elements.size(); ++e) {
        auto& element = model.elements[e];
        if (const auto problem = elementProblem(element, nodeCount, materials, sections, solverModel.properties[e]); !problem.empty()) {
          return std::unexpected(std::format("element {} (nodes {} - {}): {}", e, element.node1, element.node2, problem));
        }
        element.sectionForces = {};
        element.stress = {};
        used[element.node1] = true;
        used[element.node2] = true;
        if (element.formulation == Formulation::Timoshenko) ++timoshenko;
      }

      for (const auto& load : model.nodalLoads) {
        if (load.node >= nodeCount) return std::unexpected(std::format("a nodal load references missing node {}", load.node));
      }
      for (const auto& load : model.distributedLoads) {
        if (load.element >= model.elements.size()) {
          return std::unexpected(std::format("a distributed load references missing element {}", load.element));
        }
      }

      std::size_t isolated = 0;
      std::size_t supported = 0;
      for (auto& node : model.nodes) {
        if (!used[node.getNodeID()]) {
          node.fixAll();
          ++isolated;
        } else if (node.isSupported()) {
          ++supported;
        }
      }
      if (isolated > 0) anaf::LOG::warn("{} nodes are not connected to any element; they are held fixed", isolated);
      anaf::LOG::info("Beam model: {} nodes, {} elements ({} Timoshenko, {} Euler-Bernoulli), {} supported nodes",
                      nodeCount, model.elements.size(), timoshenko, model.elements.size() - timoshenko, supported);
      return solverModel;
    }

    void calculateStresses(MeshData& model, const std::span<const SectionProperties> properties,
                           const std::span<const anaf::MATERIAL::Material> materials, const std::span<const BeamSection> sections) {
      const auto loads = elementLocalLoads(model.nodes, model.elements, properties, model.distributedLoads, model.gravity, materials);
      const auto elementCount = static_cast<long long>(model.elements.size());
      #pragma omp parallel for schedule(static)
      for (long long index = 0; index < elementCount; ++index) {
        auto& element = model.elements[index];
        const auto& a = model.nodes[element.node1].getLocation();
        const auto& b = model.nodes[element.node2].getLocation();
        const double length = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
        element.stress = elementStress(element, loads[index], length, sections[element.sectionID].getShape(),
                                       materials[element.materialID].getYieldTensile());
      }
    }

    void logResult(const Beam_3D_Container& container, const MeshData& model) {
      double maxDisplacement = 0.0;
      double maxRotation = 0.0;
      for (const auto& node : model.nodes) {
        const auto& d = node.getDisplacement();
        const auto& r = node.getRotation();
        maxDisplacement = std::max(maxDisplacement, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
        maxRotation = std::max(maxRotation, std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]));
      }
      double maxAxial = 0.0;
      double maxMoment = 0.0;
      double maxVonMises = 0.0;
      std::size_t exceeded = 0;
      std::size_t withoutStress = 0;
      for (const auto& element : model.elements) {
        for (std::size_t end = 0; end < 2; ++end) {
          const auto* s = element.sectionForces.data() + 6 * end;
          maxAxial = std::max(maxAxial, std::abs(s[0]));
          maxMoment = std::max(maxMoment, std::hypot(s[4], s[5]));
        }
        if (!element.stress.available) {
          ++withoutStress;
          continue;
        }
        maxVonMises = std::max(maxVonMises, element.stress.maxVonMises);
        if (element.stress.isStressExceeded) ++exceeded;
      }

      anaf::LOG::success("Beam solver completed");
      if (container.getIsCalculationValid()) {
        anaf::LOG::success("Calculation is VALID! Energy diff: {:.3e} J, relative diff: {:.3e}", container.getEnergyDiff(),
                           container.getEnergyRelativeDiff());
      } else {
        anaf::LOG::error("Calculation is INVALID! Energy diff: {:.3e} J, relative diff: {:.3e}", container.getEnergyDiff(),
                         container.getEnergyRelativeDiff());
      }
      anaf::LOG::info("Max nodal displacement magnitude: {:.6g} m, max rotation magnitude: {:.6g} rad", maxDisplacement, maxRotation);
      anaf::LOG::info("Max |axial force|: {:.6g} N, max bending moment magnitude: {:.6g} N m", maxAxial, maxMoment);
      anaf::LOG::info("Max equivalent (von Mises, upper bound) stress: {:.6g} Pa", maxVonMises);
      if (exceeded > 0) anaf::LOG::warn("{} elements exceed the yield strength of their material", exceeded);
      if (withoutStress > 0) anaf::LOG::info("{} elements use a general section (no shape): no stresses", withoutStress);
      anaf::LOG::info("Work done by external forces: {:.6g} J", container.getWorkDone_External());
      anaf::LOG::info("Stored elastic deformation energy: {:.6g} J", container.getElasticDeformationEnergy_Internal());
    }

  } // namespace end

  std::expected<StaticResult, std::string> solveStatic(
    const MeshData& mesh,
    const std::span<const anaf::MATERIAL::Material> materials,
    const std::span<const BeamSection> sections,
    const std::stop_token st,
    const ProgressCallback& progress
  ) {
    const auto report = [&](const float fraction) {
      if (progress) progress(fraction);
    };
    const auto cancelled = [&] { return std::unexpected<std::string>("cancelled"); };

    auto model = buildSolverModel(mesh, materials, sections);
    if (!model) return std::unexpected(model.error());
    if (st.stop_requested()) return cancelled();
    report(0.20f);

    auto solved = std::make_shared<MeshData>(std::move(model->mesh));
    const std::vector<SectionProperties> properties = std::move(model->properties);
    Beam_3D_Container container;
    container.set(solved->nodes, solved->elements, properties);
    if (auto built = container.buildElements(materials); !built) return std::unexpected(built.error());
    if (st.stop_requested()) return cancelled();
    report(0.40f);
    container.applyLoads(solved->nodalLoads, solved->distributedLoads, solved->gravity, materials);
    if (auto dofs = container.buildNodeDofs(); !dofs) return std::unexpected(dofs.error());
    report(0.50f);
    if (auto displaced = container.calculateDisplacements(st); !displaced) {
      if (st.stop_requested()) return cancelled();
      return std::unexpected(displaced.error());
    }
    report(0.85f);
    container.calculateSectionForces();
    calculateStresses(*solved, properties, materials, sections);
    report(0.90f);
    container.runValidator();
    report(0.95f);
    logResult(container, *solved);

    solved->hasResults = true;
    StaticResult result;
    result.mesh = std::move(solved);
    result.energyCheckPassed = container.getIsCalculationValid();
    result.energyDiff = container.getEnergyDiff();
    result.energyRelativeDiff = container.getEnergyRelativeDiff();
    report(1.0f);
    return result;
  }

} // namespace FEM::BEAM end
