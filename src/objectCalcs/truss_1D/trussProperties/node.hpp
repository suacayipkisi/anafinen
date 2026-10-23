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

#include <common/supportBasis.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

namespace FEM::TRUSS {

  class Node{
  private:
    std::uint32_t m_nodeID{}; // implemented with assuming definition starting with "0 (zero)"

    std::array<bool, 3> m_isMovable{true, true, true};
    std::array<double, 3> m_location{};
    std::array<double, 3> m_displacement{}; // in modal analysis this is not exactly (meter)
    FEM::SUPPORT::Basis m_allowedMotionDirections{FEM::SUPPORT::Basis::globalAxes()};
    
  public:
    Node() = default;
    Node(
      const std::uint32_t id,
      const double locX,
      const double locY,
      const double locZ
    ):
      m_nodeID(id),
      m_location({locX, locY, locZ})
    {}


    // isMovable[i] == true means the DOF is free to move.
    void setMovable(const std::array<bool, 3> isMovable);

    inline void setLocation(const std::array<double, 3> location) {m_location = location;}
    inline void setDisplacements(std::array<double, 3> displacementOfNode) {m_displacement = displacementOfNode;}
    void setAllowedMotionDirections(const FEM::SUPPORT::Basis& directions);

    // Renumbering only (ids equal positions: meshEdit deleteNodes()).
    void setNodeID(const std::uint32_t id) {m_nodeID = id;}
    std::uint32_t getNodeID() const {return m_nodeID;}
    const std::array<double, 3>& getLocation() const {return m_location;}
    double getLocX() const {return m_location[0];}
    double getLocY() const {return m_location[1];}
    double getLocZ() const {return m_location[2];}
    
    const std::array<double, 3>& getDisplacement() const {return m_displacement;}
    const std::array<bool, 3>& getMovable() const {return m_isMovable;}
    const FEM::SUPPORT::Basis& getAllowedMotionDirections() const {return m_allowedMotionDirections;}
    // True when an allowed direction is not a global axis, i.e. getMovable() alone does not
    // describe the support.
    bool hasInclinedSupport() const;
    // True when the node is restrained in some direction (fewer than three allowed directions).
    // The node is the only place a support is stored: snapshots, the solver and export read it here.
    bool isSupported() const { return m_allowedMotionDirections.size() < 3; }
  };
  

  // Nodes are copied with every snapshot: no heap allocation per node.
  static_assert(std::is_trivially_copyable_v<Node>);

} // namespace FEM::TRUSS end
