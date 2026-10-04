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

namespace FEM::BEAM {

  // Beam node: 6 DOFs (3 translations, 3 rotations). The support is stored as two orthonormal
  // bases in global axes, one for the directions the node may move along and one for the axes
  // it may rotate about (0..3 vectors each, like the truss node). Empty = fixed, three global
  // axes = free; any other basis is an inclined support.
  class Node {
  private:
    std::uint32_t m_nodeID{}; // equals the node's position in MeshData::nodes
    std::array<double, 3> m_location{};
    std::array<double, 3> m_displacement{}; // m
    std::array<double, 3> m_rotation{};     // rad, rotation vector components about the global axes
    std::vector<std::array<double, 3>> m_allowedMotionDirections{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    std::vector<std::array<double, 3>> m_allowedRotationAxes{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

  public:
    Node() = default;
    Node(const std::uint32_t ID, const double locX, const double locY, const double locZ) :
      m_nodeID(ID),
      m_location({locX, locY, locZ})
    {}

    // Gram-Schmidt orthonormalized; throws std::invalid_argument for zero or dependent vectors.
    void setAllowedMotionDirections(std::vector<std::array<double, 3>> directions);
    void setAllowedRotationAxes(std::vector<std::array<double, 3>> axes);
    // Shortcuts for supports along the global axes: true = free in that axis.
    void setMovable(const std::array<bool, 3>& isMovable);
    void setRotatable(const std::array<bool, 3>& isRotatable);
    void fixAll() { m_allowedMotionDirections.clear(); m_allowedRotationAxes.clear(); }

    inline void setLocation(const std::array<double, 3>& location) {m_location = location;}
    inline void setDisplacement(const std::array<double, 3>& displacement) {m_displacement = displacement;}
    inline void setRotation(const std::array<double, 3>& rotation) {m_rotation = rotation;}

    std::uint32_t getNodeID() const {return m_nodeID;}
    const std::array<double, 3>& getLocation() const {return m_location;}
    const std::array<double, 3>& getDisplacement() const {return m_displacement;}
    const std::array<double, 3>& getRotation() const {return m_rotation;}
    const std::vector<std::array<double, 3>>& getAllowedMotionDirections() const {return m_allowedMotionDirections;}
    const std::vector<std::array<double, 3>>& getAllowedRotationAxes() const {return m_allowedRotationAxes;}
    // True when some translation or rotation is restrained.
    bool isSupported() const { return m_allowedMotionDirections.size() < 3 || m_allowedRotationAxes.size() < 3; }
  };

} // namespace FEM::BEAM end
