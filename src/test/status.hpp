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

#include "test.hpp"
#include <array>

class AllStatus{
private:
    int m_eigenStaus{1};
    std::array<int, 2> m_graphicsStatus{1, 1};
public:
    AllStatus(
    ):
        m_eigenStaus(testEigen())
    {}

    inline void setEigeneStatus(const int eigenStatus){
        m_eigenStaus = eigenStatus;
    }

    inline void setGraphicsStatus(const std::array<int, 2>& graphicsStatus){
        m_graphicsStatus = graphicsStatus;
    }
};
