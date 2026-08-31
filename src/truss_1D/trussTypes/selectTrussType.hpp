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

#include "../node.hpp"
#include "../element.hpp"
#include "../appliedForce.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace FEM::TRUSS {

    struct SimpleQuadranglePrismTrussInput{
        std::array<std::uint32_t, 3> cubeNum;
        double cubeEdgeLength;
        double area;
        std::uint32_t type;
    };

    enum TrussType{
        simpleQuadranglePrism,
        selfBuild
    };

    class TrussBuild{
    private:
        TrussType m_trussType;
        std::span<const Node> m_nodes;
        std::span<const TrussElement_1D> m_elements;
        std::span<const ForceApplied> m_forces;
    public:
        TrussBuild( TrussType trussType) : m_trussType(trussType) {
            switch (trussType){
                case simpleQuadranglePrism:

                case selfBuild:
            }
        }

    };

} // namespace FEM::TRUSS end
