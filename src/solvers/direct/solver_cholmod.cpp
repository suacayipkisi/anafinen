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

#include <solvers/solverPortfolio.hpp>

#include <chrono>
#ifdef ANAFINEN_HAS_CHOLMOD
#include <Eigen/CholmodSupport>
#include <algorithm>
#endif

namespace FEM::SOLVER {

#ifdef ANAFINEN_HAS_CHOLMOD
  namespace {
    // Eigen keeps the CHOLMOD factor protected; this reads its pivots.
    class PivotCheckedLLT : public Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>, Eigen::Upper> {
    public:
      // Smallest L_kk^2 / A_ii over the factor (the factor is L L^T of the permuted matrix,
      // stored as dense column-major supernode blocks, as read by Eigen's logDeterminant()).
      double smallestPivotRatio(const Eigen::VectorXd& diagonal) const {
        const cholmod_factor* factor = m_cholmodFactor;
        double smallest = std::numeric_limits<double>::infinity();
        if (factor == nullptr || !factor->is_super || !factor->is_ll) return smallest;
        const auto* x = static_cast<const double*>(factor->x);
        const auto* super = static_cast<const int*>(factor->super);
        const auto* pi = static_cast<const int*>(factor->pi);
        const auto* px = static_cast<const int*>(factor->px);
        const auto* permutation = static_cast<const int*>(factor->Perm);
        for (std::size_t node = 0; node < factor->nsuper; ++node) {
          const int columns = super[node + 1] - super[node];
          const int rows = pi[node + 1] - pi[node];
          for (int column = 0; column < columns; ++column) {
            const int k = super[node] + column;
            const double pivot = x[px[node] + column * (rows + 1)];
            const int original = permutation != nullptr ? permutation[k] : k;
            smallest = std::min(smallest, pivot * pivot / diagonal[original]);
          }
        }
        return smallest;
      }
    };
  } // namespace end
#endif

  // Parameters are unused in builds without CHOLMOD (no SuiteSparse installed, CMAKE_IGNORE_PATH test builds).
  Result solveCholmod(
    [[maybe_unused]] const Eigen::SparseMatrix<double>& upperMatrix,
    [[maybe_unused]] const Eigen::VectorXd& force,
    [[maybe_unused]] Eigen::VectorXd& displacement
  ) {
    Result result{.type = E_Type::Cholmod};
#ifdef ANAFINEN_HAS_CHOLMOD
    const auto start = std::chrono::steady_clock::now();
    PivotCheckedLLT solver;
    solver.cholmod().print = 0; // failures are reported through the result, not printed to stdout
    solver.compute(upperMatrix);
    if (solver.info() != Eigen::Success) {
      // Not positive definite: a zero or negative pivot, the stiffness of a mechanism.
      result.singular = solver.cholmod().status == CHOLMOD_NOT_POSDEF;
      if (result.singular) result.smallestPivotRatio = 0.0;
      result.message = result.singular ? "CHOLMOD: the matrix is not positive definite" : "CHOLMOD factorization failed";
    }
    else {
      // A mechanism whose pivot is rounding noise instead of zero factors without an error.
      result.smallestPivotRatio = solver.smallestPivotRatio(upperMatrix.diagonal());
      result.singular = !(result.smallestPivotRatio > singularPivotRatio);
      if (result.singular) {
        result.message = "CHOLMOD found a rounding level pivot";
      }
      else {
        displacement = solver.solve(force);
        result.converged = solver.info() == Eigen::Success && displacement.allFinite();
        if (!result.converged) result.message = "CHOLMOD solve failed";
      }
    }
    result.available = true;
    result.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
#else
    result.message = "CHOLMOD is not available in this build";
#endif
    return result;
  }

} // namespace FEM::SOLVER end
