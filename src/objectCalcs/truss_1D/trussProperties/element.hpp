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

#include <material/properties.hpp>
#include "node.hpp"
#include <log/anaf_info.hpp>

#include <cstdint>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>

namespace FEM::TRUSS {

  // 1D bar element in 3D space: two nodes, axial stiffness only.
  class TrussElement1D{
  private:
    std::uint32_t m_type{}; // material index into the material list
    double m_length{};
    double m_crossSectionArea{};
    double m_elongation{};
    double m_stress{}; // Pa, tension > 0, compression < 0
    std::array<double, 3> m_cosines{}; // double: float products put ~1e-7 relative error into K
    std::array<std::uint32_t, 2> m_nodes{};
  public:
    TrussElement1D() = default;
    TrussElement1D(
      std::uint32_t type,
      double area,
      const std::uint32_t node1,
      const std::uint32_t node2,
      std::span<const Node> allNodes
    ):
      m_type(type),
      m_crossSectionArea(area),
      m_nodes({node1, node2})
    {
      if (area <= 0.0) {
        anaf::LOG::error("Invalid element: area must be positive (got {:.6g}), material {}, nodes [{}, {}]",
            area, type, node1, node2);
        throw std::invalid_argument("Element cross sectional area must be greater than 0");
      }

      if (node1 == node2) {
        anaf::LOG::error("Invalid element: node indices cannot be identical ({} == {})", node1, node2);
        throw std::invalid_argument("An element's nodes cannot be same");
      }

      if (node1 >= allNodes.size() || node2 >= allNodes.size()) {
        anaf::LOG::error("Node index out of range: n1={}, n2={}, total_nodes={}", node1, node2, allNodes.size());
        throw std::out_of_range("Node index is outside the node span");
      }

      const double dx = allNodes[node2].getLocX() - allNodes[node1].getLocX();
      const double dy = allNodes[node2].getLocY() - allNodes[node1].getLocY();
      const double dz = allNodes[node2].getLocZ() - allNodes[node1].getLocZ();
      m_length = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (m_length <= 0.0) {
        throw std::invalid_argument("Element length must be greater than zero");
      }
      m_cosines = {dx / m_length, dy / m_length, dz / m_length};
    }

    // calculated properties of element
    inline void setEleElongation(const double elongation) {m_elongation = elongation;}
    inline void setEleStress(const double stress) {m_stress = stress;}

    inline double getEleLength() const {return m_length;}
    inline double getEleCrossSection() const {return m_crossSectionArea;}
    inline double getEleElongation() const {return m_elongation;}
    inline double getEleStress() const {return m_stress;}
    inline std::uint32_t getEleProperties() const {return m_type;}
    inline const std::array<double, 3>& getEleCosines() const {return m_cosines;}
    inline const std::array<std::uint32_t, 2>& getEleNodes() const {return m_nodes;}

  };

} // namespace FEM::TRUSS end
