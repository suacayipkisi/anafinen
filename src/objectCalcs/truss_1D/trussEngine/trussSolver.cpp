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

#include "trussSolver.hpp"
#include "trussSolver/deformationUnderConstForce.hpp"

#include <log/anaf_info.hpp>
#include <truss_1D/trussProperties/element.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <vector>

namespace FEM::TRUSS {

  namespace {

    // The snapshot turned into solver objects.
    struct SolverModel {
      std::vector<Node> nodes;
      std::vector<TrussElement1D> elements;
      std::vector<std::size_t> renderIndex; // elements[i] comes from mesh.trussElements[renderIndex[i]]
      std::vector<double> force;            // 3 per node, x / y / z
    };

    std::expected<SolverModel, std::string> buildSolverModel(const MeshData& mesh,
                                                             const std::span<const anaf::MATERIAL::Material> materials) {
      SolverModel model;
      const std::size_t nodeCount = mesh.trussNodes.size();
      if (nodeCount == 0) return std::unexpected("the model has no nodes");

      // Element node indices address this vector, so ids must equal positions.
      model.nodes.reserve(nodeCount);
      for (std::size_t i = 0; i < nodeCount; ++i) {
        const auto& source = mesh.trussNodes[i];
        if (source.getNodeID() != i) {
          return std::unexpected(std::format("node at position {} has id {}; node ids must be 0..{} in order",
                                             i, source.getNodeID(), nodeCount - 1));
        }
        model.nodes.emplace_back(static_cast<std::uint32_t>(i), source.getLocX(), source.getLocY(), source.getLocZ());
      }

      std::size_t withoutArea = 0;
      std::size_t unknownMaterial = 0;
      std::vector<bool> usedByBar(nodeCount, false);
      model.elements.reserve(mesh.trussElements.size());
      for (std::size_t e = 0; e < mesh.trussElements.size(); ++e) {
        const auto& element = mesh.trussElements[e];
        if (element.isWireframe) continue;
        if (!(element.crossSectionArea > 0.0)) {
          ++withoutArea;
          continue;
        }
        if (element.materialID >= materials.size()) {
          ++unknownMaterial;
          continue;
        }
        if (element.node1 >= nodeCount || element.node2 >= nodeCount) {
          return std::unexpected(std::format("bar {} references node {}, but the model has {} nodes",
                                             e, std::max(element.node1, element.node2), nodeCount));
        }
        if (element.node1 == element.node2) {
          return std::unexpected(std::format("bar {} starts and ends at node {}", e, element.node1));
        }
        try {
          model.elements.emplace_back(element.materialID, element.crossSectionArea, element.node1, element.node2,
                                      std::span<const Node>(model.nodes));
        } catch (const std::exception& exception) {
          return std::unexpected(std::format("bar {} (nodes {} - {}): {}", e, element.node1, element.node2, exception.what()));
        }
        model.renderIndex.push_back(e);
        usedByBar[element.node1] = true;
        usedByBar[element.node2] = true;
      }

      if (withoutArea > 0) {
        return std::unexpected(std::format("{} bars have no cross-section area; set one in the model editor", withoutArea));
      }
      if (unknownMaterial > 0) {
        return std::unexpected(std::format("{} bars use a material that is not in the material list", unknownMaterial));
      }
      if (model.elements.empty()) {
        return std::unexpected("the model has no bars (surface and volume meshes are shown as wireframe only)");
      }

      // Supports: every node keeps the allowed motion of its snapshot node.
      std::size_t isolated = 0;
      std::size_t supported = 0;
      for (auto& node : model.nodes) {
        const std::uint32_t id = node.getNodeID();
        if (!usedByBar[id]) {
          node.setMovable({false, false, false});
          ++isolated;
          continue;
        }
        const auto& source = mesh.trussNodes[id];
        node.setAllowedMotionDirections(source.getAllowedMotionDirections());
        if (source.isSupported()) ++supported;
      }
      if (isolated > 0) anaf::LOG::warn("{} nodes are not connected to any bar; they are held fixed", isolated);
      anaf::LOG::info("Model: {} nodes, {} bars, {} supported nodes", nodeCount, model.elements.size(), supported);

      model.force.assign(nodeCount * 3, 0.0);
      std::size_t skipped = 0;
      for (const auto& load : mesh.appliedForces) {
        const std::uint32_t nodeId = load.getAppliedNode();
        if (nodeId >= nodeCount) {
          ++skipped;
          continue;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) model.force[3U * nodeId + axis] += load.getForce()[axis];
      }
      if (skipped > 0) anaf::LOG::warn("{} loads reference missing nodes and were skipped", skipped);
      anaf::LOG::info("Applied {} nodal loads", mesh.appliedForces.size() - skipped);
      return model;
    }

    void logResult(const Truss1DContainer& container, const SolverModel& model) {
      // Node locations stay undeformed: consumers draw location + displacement * scale.
      double maxDisp = 0.0;
      for (const auto& node : model.nodes) {
        const auto& d = node.getDisplacement();
        maxDisp = std::max(maxDisp, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
      }
      double maxStress = 0.0;
      for (const auto& element : model.elements) maxStress = std::max(maxStress, std::abs(element.getEleStress()));

      anaf::LOG::success("Solver completed");
      // Exponent notation: the energy differences are round-off sized (1e-15 .. 1e-8 J).
      if (container.getIsCalculationValid()) {
        anaf::LOG::success("Calculation is VALID! Energy diff: {:.3e} J, relative diff: {:.3e}", container.getEnergyDiff(),
                           container.getEnergyRelativeDiff());
      } else {
        anaf::LOG::error("Calculation is INVALID! Energy diff: {:.3e} J, relative diff: {:.3e}", container.getEnergyDiff(),
                         container.getEnergyRelativeDiff());
      }
      anaf::LOG::info("Max nodal displacement magnitude: {:.6g} m", maxDisp);
      anaf::LOG::info("Max element stress magnitude: {:.6g} Pa", maxStress);
      anaf::LOG::info("Work done by external forces: {:.6g} J", container.getWorkDoneExternal());
      anaf::LOG::info("Stored elastic deformation energy: {:.6g} J", container.getElasticDeformationEnergyInternal());
    }

  } // namespace end

  std::expected<StaticResult, std::string> solveStatic(
    const MeshData& mesh,
    const std::span<const anaf::MATERIAL::Material> materials,
    const std::stop_token st,
    const ProgressCallback& progress
  ) {
    const auto report = [&](const float fraction) {
      if (progress) progress(fraction);
    };
    const auto cancelled = [&] { return std::unexpected<std::string>("cancelled"); };

    auto model = buildSolverModel(mesh, materials);
    if (!model) return std::unexpected(model.error());
    if (st.stop_requested()) return cancelled();
    report(0.30f);

    Truss1DContainer container;
    container.set(model->force, model->nodes, model->elements);
    container.assembleStiffness(model->elements, materials);
    if (st.stop_requested()) return cancelled();
    report(0.50f);
    container.considerWeight(model->elements, materials);
    report(0.55f);
    if (auto displaced = container.calculateDisplacements(st); !displaced) {
      if (st.stop_requested()) return cancelled();
      return std::unexpected(displaced.error());
    }
    report(0.85f);
    container.calculateElementForcesAndStress(materials);
    report(0.90f);
    container.runValidator(materials);
    report(0.95f);
    logResult(container, *model);

    StaticResult result;
    result.energyCheckPassed = container.getIsCalculationValid();
    result.energyDiff = container.getEnergyDiff();
    result.energyRelativeDiff = container.getEnergyRelativeDiff();
    result.mesh = std::make_shared<MeshData>(mesh);
    auto& solved = *result.mesh;
    for (std::size_t i = 0; i < model->nodes.size(); ++i) solved.trussNodes[i].setDisplacements(model->nodes[i].getDisplacement());
    for (auto& element : solved.trussElements) {
      element.stress = 0.0f;
      element.isStressExceeded = false;
    }
    for (std::size_t b = 0; b < model->elements.size(); ++b) {
      const auto& bar = model->elements[b];
      auto& target = solved.trussElements[model->renderIndex[b]];
      target.stress = static_cast<float>(bar.getEleStress());
      target.isStressExceeded = std::abs(bar.getEleStress()) > materials[bar.getEleProperties()].getYieldTensile();
    }
    solved.hasResults = true;
    report(1.0f);
    return result;
  }

} // namespace FEM::TRUSS end
