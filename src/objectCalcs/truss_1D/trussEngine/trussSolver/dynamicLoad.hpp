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

#include "common/globalConstantsAndFunctions.hpp"
#include "trussEngine/trussSolver/reducedDofs.hpp"
#include "trussProperties/appliedForce.hpp"
#include "trussProperties/element.hpp"
#include "trussProperties/node.hpp"
#include <log/anaf_info.hpp>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <span>
#include <expected>

namespace FEM::TRUSS {

  // nodes stores mode shape values as displacement

  class ModeShape {
  private:
    double m_thisNaturalFrequency;
    std::span<Node> m_allNodes;
  public:
    ModeShape (double naturalFrequency, std::span<Node> allNodes){
      m_thisNaturalFrequency = naturalFrequency;
      m_allNodes = allNodes;
    }

    double getNatrualFrequency() const {return m_thisNaturalFrequency;}
    const std::span<Node> getModeShapeValues() const {return m_allNodes;}
  };

  class TrussDynamicContainer {
  private:
    std::span<ForceApplied> m_forceVec; // must be harmonic
    std::span<Node> m_allNodes;
    std::span<ModeShape> m_allModeShapes;
    std::span<TrussElement1D> m_allElements;

    // One gravity for the self-weight nodal loads and the self-weight stress term (m/s^2).
    std::array<double, 3> m_gravity{CONSTANTS::g_gravity};

    ReducedDofs m_dofs;
    Eigen::SparseMatrix<double> m_reducedStiffness;
    Eigen::SparseMatrix<double> m_reducedMass;
  public:
    void set(
      std::span<ForceApplied> forceVec,
      std::span<Node> allNodes,
      std::span<TrussElement1D> allElements
    ) {
      m_forceVec = forceVec;
      m_allNodes = allNodes;
      m_allElements = allElements;
    }

    // Builds the support reduction of the nodes given to set() and the reduced stiffness.
    void setStiffnessANDMass(
      const std::vector<TrussElement1D>& elements,
      std::span<const anaf::MATERIAL::Material> allMaterials
    );

    inline void setGravity(const std::array<double, 3>& gravity) {m_gravity = gravity;}
    inline const std::array<double, 3>& getGravity() const {return m_gravity;}

    std::expected<void, std::string> calculateModeShapes(std::stop_token stopToken = {}, unsigned int expectedModeShapeNum = 1);
    inline const ModeShape& getModeShape(unsigned int shapeNum) const {return m_allModeShapes[shapeNum];}
  };

}// namespace FEM::TRUSS end
