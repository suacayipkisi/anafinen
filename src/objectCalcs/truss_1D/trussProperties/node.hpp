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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace FEM::TRUSS {

  class Node{
  private:
    std::uint32_t m_nodeID{}; // implemented with assuming definition starting with "0 (zero)"

    std::array<bool, 3> m_isMovable{true, true, true};
    std::array<double, 3> m_Location{};
    std::array<double, 3> m_displacement{};
    std::vector<std::array<double, 3>> m_allowedMotionDirections{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    
  public:
    Node() = default;
    Node(
      const std::uint32_t ID,
      const double locX,
      const double locY,
      const double locZ
    ):
      m_nodeID(ID),
      m_Location({locX, locY, locZ})
    {}


    // isMovable[i] == true means the DOF is free to move.
    inline void setMovable(const std::array<bool, 3> isMovable) {
      m_isMovable = isMovable;
      m_allowedMotionDirections.clear();
      for (std::size_t axis = 0; axis < isMovable.size(); ++axis) {
        if (isMovable[axis]) {
          std::array<double, 3> direction{};
          direction[axis] = 1.0;
          m_allowedMotionDirections.push_back(direction);
        }
      }
    }

    inline void setLocation(const std::array<double, 3> location) {m_Location = location;}
    inline void setDisplacements(std::array<double, 3> displacementOfNode) {m_displacement = displacementOfNode;}
    inline void setAllowedMotionDirections(std::vector<std::array<double, 3>> directions) {
      if (directions.size() > 3) throw std::invalid_argument("A 3D node can have at most three independent motion directions");
      std::vector<std::array<double, 3>> orthonormalBasis;
      for (auto& direction : directions) {
        for (const auto& basis : orthonormalBasis) {
          const double projection = direction[0] * basis[0] + direction[1] * basis[1] + direction[2] * basis[2];
          for (std::size_t axis = 0; axis < 3; ++axis) direction[axis] -= projection * basis[axis];
        }
        const double norm = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (norm <= 1e-12) throw std::invalid_argument("Allowed motion directions must be non-zero and linearly independent");
        for (double& value : direction) value /= norm;
        orthonormalBasis.push_back(direction);
      }
      m_allowedMotionDirections = std::move(orthonormalBasis);
      for (std::size_t axis = 0; axis < m_isMovable.size(); ++axis) {
        std::array<double, 3> residual{};
        residual[axis] = 1.0;
        for (const auto& basis : m_allowedMotionDirections) {
          const double projection = residual[0] * basis[0] + residual[1] * basis[1] + residual[2] * basis[2];
          for (std::size_t component = 0; component < 3; ++component) residual[component] -= projection * basis[component];
        }
        const double residualNorm = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1] + residual[2] * residual[2]);
        m_isMovable[axis] = residualNorm <= 1e-12;
      }
    }
    std::uint32_t getNodeID() const {return m_nodeID;}
    const std::array<double, 3>& getLocation() const {return m_Location;}
    double getLocX() const {return m_Location[0];}
    double getLocY() const {return m_Location[1];}
    double getLocZ() const {return m_Location[2];}

    const std::array<double, 3>& getDisplacement() const {return m_displacement;}
    const std::array<bool, 3>& getMovable() const {return m_isMovable;}
    const std::vector<std::array<double, 3>>& getAllowedMotionDirections() const {return m_allowedMotionDirections;}
    // True when an allowed direction is not a global axis, i.e. getMovable() alone does not
    // describe the support.
    bool hasInclinedSupport() const {
      return std::ranges::any_of(m_allowedMotionDirections, [](const std::array<double, 3>& direction) {
        return std::ranges::count(direction, 0.0) != 2;
      });
    }
  };

} // namespace FEM::TRUSS end
