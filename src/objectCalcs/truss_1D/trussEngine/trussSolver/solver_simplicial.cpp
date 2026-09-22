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

#include "solverPortfolio.hpp"

#include <chrono>
#include <Eigen/SparseCholesky>

namespace FEM::TRUSS::SOLVER {

  Result solveSimplicialLDLT(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    Eigen::VectorXd& displacement
  ) {
    Result result{Kind::SimplicialLDLT};
    const auto start = std::chrono::steady_clock::now();
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Upper> solver;
    solver.compute(upperMatrix);
    if (solver.info() != Eigen::Success) {
      result.message = "Eigen SimplicialLDLT factorization failed";
    }
    else {
      displacement = solver.solve(force);
      result.converged = solver.info() == Eigen::Success && displacement.allFinite();
      if (!result.converged) result.message = "Eigen SimplicialLDLT solve failed";
    }
    result.available = true;
    result.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
  }

} // namespace FEM::TRUSS::SOLVER
