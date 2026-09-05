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

#include <cstdint>
#include <span>
#include <vector>

#include "../../bridge/generalStatus.hpp"
#include "../trussProperties/appliedForce.hpp"
#include "../trussTypes/simpleQuadranglePrismTrussCreate.hpp"
#include "trussSolver/solveTrussStaticDeflection.hpp"

namespace FEM::TRUSS{

    class Truss_SQPT {
    private:
        std::uint32_t m_cubeNumX;
        std::uint32_t m_cubeNumY;
        std::uint32_t m_cubeNumZ;
        double m_elementLength;
        double m_area;
        std::uint32_t m_type;
        std::vector<ForceApplied> m_force;
        std::vector<double> m_forceVec;
        SimpleTruss m_truss;
        Truss_1D_Container m_container;
    public:
        Truss_SQPT(
            anaf::BRIDGE::Gui_Calc_Bridge& bridge,
            std::stop_token st,
            std::uint32_t cubeNumX,
            std::uint32_t cubeNumY,
            std::uint32_t cubeNumZ,
            double elementLength,
            double area,
            std::uint32_t type
        ) :
            m_cubeNumX(cubeNumX),
            m_cubeNumY(cubeNumY),
            m_cubeNumZ(cubeNumZ),
            m_elementLength(elementLength),
            m_area(area * 1e-4),
            m_type(type),
            m_truss({{cubeNumX, cubeNumY, cubeNumZ}, elementLength, area * 1e-4, type})
        {}

        void trussCalculator_SQPT(
            anaf::BRIDGE::Gui_Calc_Bridge& bridge,
            std::stop_token st
        );

        void trussSetForce_SQRT(
            anaf::BRIDGE::Gui_Calc_Bridge& bridge,
            std::stop_token st,
            std::vector<ForceApplied> force
        );

        void setContainer(
            anaf::BRIDGE::Gui_Calc_Bridge& bridge,
            std::stop_token st
        );

        void calculate(
            anaf::BRIDGE::Gui_Calc_Bridge& bridge,
            std::stop_token st,
            std::span<anaf::MATERIAL::Material> materials
        );

        const std::vector<Node>& getNodes() const { return m_truss.getNodes(); }
        const std::vector<TrussElement_1D>& getElements() const { return m_truss.getElements(); }

    };

    // simple quadrangle prism truss
    

} // namespace FEM::TRUSS end
