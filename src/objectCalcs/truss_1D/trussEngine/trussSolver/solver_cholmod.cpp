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
#include <log/anaf_info.hpp>

#include <chrono>
#ifdef ANAFINEN_HAS_CHOLMOD
#include <Eigen/CholmodSupport>
#include <Eigen/SparseCholesky>
#endif

namespace FEM::TRUSS::SOLVER {

  Result solveCholmod(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    Eigen::VectorXd& displacement
  ) {
    Result result{Kind::Cholmod};
#ifdef ANAFINEN_HAS_CHOLMOD
    const auto start = std::chrono::steady_clock::now();
    Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>, Eigen::Upper> solver;
    solver.compute(upperMatrix);
    if (solver.info() != Eigen::Success) {
      result.message = "CHOLMOD factorization failed";
    }
    else {
      displacement = solver.solve(force);
      result.converged = solver.info() == Eigen::Success && displacement.allFinite();
      if (!result.converged) result.message = "CHOLMOD solve failed";
    }
    result.available = true;
    result.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  #else
    result.message = "CHOLMOD is not available in this build";
  #endif
    return result;
  }

} // namespace FEM::TRUSS::SOLVER
