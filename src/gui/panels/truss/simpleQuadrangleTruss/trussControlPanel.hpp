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

#include "../../../guiMaterials/iPanel.hpp"
#include "../../../../truss_1D/trussProperties/appliedForce.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace anaf::GUI {

    class TrussControlPanel : public IPanel {
    private:
        std::uint32_t m_cubeNumX {5};
        std::uint32_t m_cubeNumY {1};
        std::uint32_t m_cubeNumZ {1};
        std::uint32_t m_type{1};
        double m_cubeEdgeLength{1.0};
        double m_crossSectionalArea{80.0};
        std::uint32_t m_forceNodeId{5};
        std::array<double, 3> m_forceVector{0.0, 0.0, 0.0};
        std::vector<FEM::TRUSS::ForceApplied> m_appliedForces;
    public:
        ~TrussControlPanel() override = default;
        std::function<void()> onCalculated;
        void onImGuiRender() override;
        
    };

} // namespace anaf::GUI end
