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

#include "trussSolver.hpp"
#include "../log/anaf_info.h"

#include <cstdint>

void trussCalculator(
    std::uint32_t cubeNumX,
    std::uint32_t cubeNumY,
    std::uint32_t cubeNumZ,
    double elementLength,
    double area,
    std::uint32_t type
) {
    anaf_info("\n\tcube_num_X: {%d} \n\tcube_num_Y: {%d} \n\tcube_num_Z: {%d} \n\telement_length: {%f} \n\telement_crossSectional_area: {%f} \n\tmaterial_trype: {%d}",
        cubeNumX,
        cubeNumY,
        cubeNumZ,
        elementLength,
        area,
        type
    );
}
    