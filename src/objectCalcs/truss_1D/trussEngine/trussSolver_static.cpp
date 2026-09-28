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
#include <log/anaf_info.hpp>
#include <bridge/generalStatus.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stop_token>
#include <vector>

namespace FEM::TRUSS::detail {

  void runStaticSolve(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    Truss_1D_Container& container,
    const std::vector<Node>& nodes,
    const std::vector<TrussElement_1D>& elements,
    std::span<const anaf::MATERIAL::Material> materials
  ) {
    if (st.stop_requested()) return;
    container.assembleStiffness(elements, materials);
    if (st.stop_requested()) return;
    bridge.m_progress = 0.50f;
    container.considerWeight(elements, materials);
    if (st.stop_requested()) return;
    bridge.m_progress = 0.550f;
    container.calculateDisplacements(st);
    if (st.stop_requested()) return;
    bridge.m_progress = 0.85f;

    // Node locations stay undeformed: consumers draw location + displacement * deformScale,
    // so moving the nodes here would apply the displacement twice.
    double maxDisp = 0.0;
    for (const auto& node : nodes) {
      for (const double component : node.getDisplacement()) {
        maxDisp = std::max(maxDisp, std::abs(component));
      }
    }

    container.calculateElementForcesAndStress(materials, {0, -9.80665, 0});
    bridge.m_progress = 0.90f;

    double maxStress = 0.0;
    for (const auto& element : elements) {
      maxStress = std::max(maxStress, std::abs(element.getEleStress()));
    }

    container.runValidator(materials);
    bridge.m_progress = 0.95f;
    {
      std::lock_guard lock(bridge.dataMutex);
      bridge.m_isValid = container.getIsCalculationValid();
      bridge.m_energyDiff = container.getEnergyDiff();

      if (container.getIsCalculationValid()) {
        anaf::LOG::success("Solver completed");
        anaf::LOG::setFloatPrecision(10);
        anaf::LOG::success(
          "Calculation is VALID! Energy diff: {}, relative diff: {}",
          container.getEnergyDiff(),
          container.getEnergyRelativeDiff()
        );
        anaf::LOG::setFloatPrecision(6);
        anaf::LOG::info("Max nodal displacement magnitude: {}", maxDisp);
        anaf::LOG::info("Max element stress magnitude [Pa]: {}", maxStress);
        anaf::LOG::info("Work done by external forces: {}", container.getWorkDone_External());
        anaf::LOG::info("Stored elastic deformation energy: {}", container.getElasticDeformationEnergy_Internal());
      } else {
        anaf::LOG::success("Solver completed");
        anaf::LOG::error(
          "Calculation is INVALID! Energy diff: {}, relative diff: {}",
          container.getEnergyDiff(),
          container.getEnergyRelativeDiff()
        );
        anaf::LOG::info("Work done by external forces: {}", container.getWorkDone_External());
        anaf::LOG::info("Stored elastic deformation energy: {}", container.getElasticDeformationEnergy_Internal());
      }
    }
  }

} // namespace FEM::TRUSS::detail end
