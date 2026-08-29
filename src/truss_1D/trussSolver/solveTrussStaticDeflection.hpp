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

#include <Eigen/SparseCore>
#include <span>
#include <vector>
#include <array>

class Truss_1D_Container{
private:
    std::span<const double> m_forceVec;
    std::span<Node> m_allNodes;
    std::span<TrussElement_1D> m_allElements;

    std::vector<Eigen::Triplet<double>> m_globalStiffnessMatrix;
    std::vector<std::array<double, 3>> m_resultDisplacements;
    std::vector<std::array<double, 3>> m_resultForces;
public:
    Truss_1D_Container(
        std::span<const double> forceVec,
        std::span<Node> allNodes,
        std::span<TrussElement_1D> allElements
    ): 
        m_forceVec(forceVec),
        m_allNodes(allNodes),
        m_allElements(allElements)
    {}

    void assembleStiffness(
        const std::vector<TrussElement_1D>& elements,
        std::span<const Material> allMaterials
    );

    void calculateDisplacements();
    void calculateElementForcesAndStress(const std::span<const Material> allMaterials);
};
