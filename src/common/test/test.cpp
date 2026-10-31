// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
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

#include "test.hpp"
#include "log/anaf_info.hpp"
#include "numericalMethods/matrixSolvers/naiveGaussElimination.hpp"

#include <Eigen/Core>
#include <optional>

namespace anaf::TEST {
  
  std::optional<bool> test() {
    
    Eigen::MatrixXd test(3, 3);
    test << 3, -0.1, -0.2,
            0.1, 7.0, -0.3,
            0.3, -0.2, 10.0;
    Eigen::VectorXd testB(3);
    testB << 7.85, -19.3, 71.4;
    
    auto result = NUMERIC::naiveGaussElimination(test, testB);
    anaf::LOG::info("result {}, {}, {}", (*result)(0), (*result)(1), (*result)(2));
    
    return true;
    
    return std::nullopt;
  }
  
} // namespace anaf::TEST end
