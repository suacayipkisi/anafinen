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

#pragma once

#include <Eigen/Core>
#include <optional>

namespace anaf::NUMERIC {
  
  // WARMING!! Naive Gauss Eliminatination accumulates high round-off errors
  // If your matrices are too small, this method is fine HOWEVER if you are using this method for huge matrices or
  // you are somehow using this method repeatedly (like creating another matrix problems with using result of this 
  // method) round-off errors might be insane.
  // In this case try using LU decomposition or LDLT(cholesky decomposition) etc.
  std::optional<Eigen::VectorXd> naiveGaussElimination(Eigen::MatrixXd& theMatrix_A, Eigen::VectorXd& theVector_b);
  
} // namespace anaf::NUMERIC end
