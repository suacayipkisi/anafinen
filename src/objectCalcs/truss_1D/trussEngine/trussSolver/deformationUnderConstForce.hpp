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

#include <trussProperties/element.hpp>
#include <trussProperties/node.hpp>

#include <material/properties.hpp>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <array>
#include <expected>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace FEM::TRUSS {

  class Truss1DContainer{
  private:
    bool m_isCalculationValid{false};
    double m_energyDiff{};
    double m_energyRelativeDiff{};
    double m_workDoneExternal{};
    double m_elasticDeformationEnergyInternal{};

    std::span<double> m_forceVec;
    std::span<Node> m_allNodes;
    std::span<TrussElement1D> m_allElements;

    std::vector<Eigen::Triplet<double>> m_globalStiffnessMatrix;
    std::vector<std::array<double, 3>> m_resultDisplacements;
  public:
    void set(
      std::span<double> forceVec,
      std::span<Node> allNodes,
      std::span<TrussElement1D> allElements
    ) {
      m_forceVec = forceVec;
      m_allNodes = allNodes;
      m_allElements = allElements;
    }

    void assembleStiffness(
      const std::vector<TrussElement1D>& elements,
      std::span<const anaf::MATERIAL::Material> allMaterials
    );

    void considerWeight(
      const std::vector<TrussElement1D>& elements,
      std::span<const anaf::MATERIAL::Material> allMaterials
    );

    // An error (a mechanism names a node and its free direction) when the solve failed or was
    // stopped; the displacements are then zero.
    std::expected<void, std::string> calculateDisplacements(std::stop_token stopToken = {});
    void calculateElementForcesAndStress(
      const std::span<const anaf::MATERIAL::Material> allMaterials, 
      const Eigen::Vector3d gravityVector = {0.0, -9.80665, 0.0}
    );

    void runValidator(const std::span<const anaf::MATERIAL::Material> allMaterials);

    inline bool getIsCalculationValid() const {return m_isCalculationValid;}
    inline double getEnergyDiff() const {return m_energyDiff;}
    inline double getEnergyRelativeDiff() const {return m_energyRelativeDiff;}
    inline double getWorkDoneExternal() const {return m_workDoneExternal;}
    inline double getElasticDeformationEnergyInternal() const {return m_elasticDeformationEnergyInternal;}
  };

} // namespace FEM::TRUSS end
