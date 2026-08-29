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

#pragma once

#include "../element.hpp"
#include "../node.hpp"

#include <cstdint>
#include <array>
#include <vector>

class SimpleTruss{
private:
    std::array<std::uint32_t, 3> m_cubeNum{1, 1, 1}; // x y z added unit truss cube, cannot be zero none of them
    std::vector<Node> m_allNodes;
    std::vector<TrussElement_1D> m_allElements;
    std::uint32_t m_type;
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
        m_cubeEdgeLength(cubeEdgeLength),
        m_area(area),
        m_type(type)
    {}

    void setTruss();
};
