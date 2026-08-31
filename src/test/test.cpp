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

#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "../log/anaf_info.hpp"
#include "test.hpp"

namespace anaf::TEST {

    int testEigen(){
        Eigen::Matrix2d K;
        K <<  2.0, -1.0,
            -1.0,  2.0;

        static int eigenStatus {1};

        double det = K.determinant();
        if (det == 3.0) {
            anaf::LOG::success("Eigen math backend verified. Det(K) = {}", det);
            eigenStatus = 0;
        } else {
            anaf::LOG::warn("Eigen determinant mismatch. Result: {}", det);
            eigenStatus = 1;
        }

        return eigenStatus;
    }

} // namespace anaf::TEST end
