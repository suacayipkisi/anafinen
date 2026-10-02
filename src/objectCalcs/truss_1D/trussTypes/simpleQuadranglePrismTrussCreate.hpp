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

#include <cstdint>
#include <array>
#include <expected>
#include <string>
#include <vector>

namespace FEM::TRUSS {

  class SimpleTruss{
  private:
    std::array<std::uint32_t, 3> m_cubeNum{1, 1, 1}; // unit cubes along x, y, z; none of them may be zero
    std::vector<Node> m_allNodes;
    std::vector<TrussElement_1D> m_allElements;
    std::uint32_t m_type{}; // material index
    double m_cubeEdgeLength{};
    double m_area{};
  public:
    SimpleTruss(
      std::array<std::uint32_t, 3> cubeNum,
      double cubeEdgeLength,
      double area,
      std::uint32_t type
    ): 
      m_cubeNum(cubeNum),
      m_type(type),
      m_cubeEdgeLength(cubeEdgeLength),
      m_area(area)
    {}

    // Checks the parameters first and builds nothing when they are invalid: the element
    // constructor throws on a zero area or length, and an exception thrown inside the OpenMP
    // loops below would call std::terminate instead of reaching the caller.
    std::expected<void, std::string> setTruss();

    std::vector<Node>& getNodes() { return m_allNodes; }
    const std::vector<Node>& getNodes() const { return m_allNodes; }

    std::vector<TrussElement_1D>& getElements() { return m_allElements; }
    const std::vector<TrussElement_1D>& getElements() const { return m_allElements; }

    std::uint32_t getNodeNum() const {return static_cast<std::uint32_t>(m_allNodes.size());}
  };

} // namespace FEM::TRUSS end
