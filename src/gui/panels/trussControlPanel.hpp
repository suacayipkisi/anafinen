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

#include "../guiMaterials/iPanel.hpp"

#include <array>
#include <cstdint>

namespace anaf::GUI {

    class TrussControlPanel : public IPanel {
    private:
        std::uint32_t m_cubeNumX {5};
        std::uint32_t m_cubeNumY {1};
        std::uint32_t m_cubeNumZ {1};
        std::uint32_t m_type{0};
        double m_cubeEdgeLength{1.0};
        double m_crossSectionalArea{80.0};
    public:
        void onImGuiRender() override;

        inline std::array<std::uint32_t, 3> getTrussSize() const {return {m_cubeNumX, m_cubeNumY, m_cubeNumZ};};
        inline std::uint32_t getType() const {return m_type;}
        inline double geElementLength() const {return m_cubeEdgeLength;}
        inline double getelementArea() const {return m_crossSectionalArea;}
        
    };

} // namespace anaf::GUI end
