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

#include <array>
#include <cstdint>
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
    void setMovable(const std::array<bool, 3> isMovable);

    inline void setLocation(const std::array<double, 3> location) {m_Location = location;}
    inline void setDisplacements(std::array<double, 3> displacementOfNode) {m_displacement = displacementOfNode;}
    void setAllowedMotionDirections(std::vector<std::array<double, 3>> directions);

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
    bool hasInclinedSupport() const;
  };

} // namespace FEM::TRUSS end
