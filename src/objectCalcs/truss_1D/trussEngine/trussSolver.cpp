// Copyright (c) 2026 Ufuk Deniz Konuk
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
#include <log/anaf_info.hpp>
#include <bridge/generalStatus.hpp>
#include <trussTypes/simpleQuadranglePrismTrussCreate.hpp>
#include <trussProperties/appliedForce.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <stop_token>
#include <string>
#include <vector>

namespace FEM::TRUSS {

    void Truss_SQPT::trussSetAndSetFix_SQPT(
        anaf::BRIDGE::Gui_Calc_Bridge& bridge,
        std::stop_token st
        
    ) {
        m_truss.setTruss();

        auto& nodes = m_truss.getNodes();
        #pragma omp parallel for schedule(static)
        for (auto& node : nodes) {
            const auto it = bridge.fixedDOFsByNode.find(node.getNodeID());
            if (it != bridge.fixedDOFsByNode.end()) {
                node.setMovable({ !it->second[0], !it->second[1], !it->second[2] });
            }
            else {
                node.setMovable({true, true, true});
            }
        }

        std::vector<std::string> fixInfo;
        for (const auto& [nodeId, dofs] : bridge.fixedDOFsByNode) {
            std::string x, y, z;
            std::string node =  std::format("{}", nodeId);
            x = (dofs[0] == true ? "x" : "-");
            y = (dofs[1] == true ? "y" : "-");
            z = (dofs[2] == true ? "z" : "-");
            fixInfo.push_back(node + " " + x + " " + y + " " + z + " / ");
        }
        anaf::LOG::info("Fixed nodes {}", fixInfo);
        bridge.m_progress = 0.20f;
    }

    void Truss_SQPT::trussSetForce_SQRT(
        anaf::BRIDGE::Gui_Calc_Bridge& bridge,
        std::stop_token st,
        std::vector<ForceApplied> force
    ) {
        m_forceVec.assign(m_truss.getNodeNum() * 3, 0.0);

        for (std::size_t i = 0; i < force.size(); ++i) {
            const std::uint32_t nodeId = force[i].getApliedNode();
            const std::array<double, 3> currentNodeForce = force[i].getForce();

            if (nodeId >= m_truss.getNodeNum()) {
                continue;
            }

            anaf::LOG::info("Apply load to node {}: [{}, {}, {}]", nodeId, currentNodeForce[0], currentNodeForce[1], currentNodeForce[2]);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                m_forceVec[3U * nodeId + axis] = currentNodeForce[axis];
            }
        }
        bridge.m_progress = 0.25f;
    }

    void Truss_SQPT::setContainer(
        anaf::BRIDGE::Gui_Calc_Bridge& bridge,
        std::stop_token st
    ) {
        auto& nodes = m_truss.getNodes();
        auto& elements = m_truss.getElements();
        m_container.set(
            m_forceVec,
            std::span<Node>{nodes.data(), nodes.size()},
            std::span<TrussElement_1D>{elements.data(), elements.size()}
        );
        bridge.m_progress = 0.30f;
    }

    void Truss_SQPT::calculate(
        anaf::BRIDGE::Gui_Calc_Bridge& bridge,
        std::stop_token st,
        std::span<anaf::MATERIAL::Material> materials
    ){
        const auto& elements = m_truss.getElements();
        m_container.assembleStiffness(elements, materials);
        bridge.m_progress = 0.50f;
        m_container.considerWeight(elements, materials);
        bridge.m_progress = 0.550f;
        m_container.calculateDisplacements();
        bridge.m_progress = 0.85f;

        double maxDisp = 0.0;
        auto& nodes = m_truss.getNodes();
        for (auto& node : nodes) {
            auto loc = node.getLocation();
            const auto disp = node.getDisplacmenet();
            for (std::size_t axis = 0; axis < 3; ++axis) {
                loc[axis] += disp[axis];
                maxDisp = std::max(maxDisp, std::abs(disp[axis]));
            }
            node.setLocation(loc);
        }

        m_container.calculateElementForcesAndStress(materials, {0, -9.80665, 0});
        bridge.m_progress = 0.90f;

        double maxStress = 0.0;
        for (auto& element : elements) {
            maxStress = std::max(maxStress, element.getEleStress());
        }

        m_container.runValidator(materials);
        bridge.m_progress = 0.95f;
        {
            std::lock_guard lock(bridge.dataMutex);
            bridge.m_isValid = m_container.getIsCalculationValid();
            bridge.m_energyDiff = m_container.getEnergyDiff();

            if (m_container.getIsCalculationValid()) {
                anaf::LOG::success("Solver completed");
                anaf::LOG::setFloatPrecision(10);
                anaf::LOG::success(
                    "Calculation is VALID! Energy diff: {}, relative diff: {}",
                    m_container.getEnergyDiff(),
                    m_container.getEnergyRelativeDiff()
                );
                anaf::LOG::setFloatPrecision(6);
                anaf::LOG::info("Max nodal displacement magnitude: {}", maxDisp);
                anaf::LOG::info("Max element stress: {}", maxStress);
                anaf::LOG::info("Work done by external forces: {}", m_container.getWorkDone_External());
                anaf::LOG::info("Stored elastic deformation energy: {}", m_container.getElasticDeformationEnergy_Internal());
            } else {
                anaf::LOG::success("Solver completed");
                anaf::LOG::error(
                    "Calculation is INVALID! Energy diff: {}, relative diff: {}",
                    m_container.getEnergyDiff(),
                    m_container.getEnergyRelativeDiff()
                );
                anaf::LOG::info("Work done by external forces: {}", m_container.getWorkDone_External());
                anaf::LOG::info("Stored elastic deformation energy: {}", m_container.getElasticDeformationEnergy_Internal());
            }
        }
    }
    
} // namespace FEM::TRUSS end
