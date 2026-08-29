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

#include "node.hpp"
#include <array>
#include <span>
class ForceApplied{
private:
    std::span<const Node> m_nodes;
    std::array<double, 3> m_force;
public:
    ForceApplied(std::span<const Node> nodes, std::array<double, 3> force):
        m_nodes(nodes), m_force(force)
    {}

    inline void updateForce(std::array<double, 3> force){m_force = force;}
    inline std::span<const Node> getApliedNode() const {return m_nodes;}
    inline const std::array<double, 3>& getForce() const {return m_force;}
};
