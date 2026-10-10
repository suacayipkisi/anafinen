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

#include <Eigen/SparseCholesky>

namespace FEM::SOLVER {

  std::optional<Mechanism> findMechanism(const Eigen::SparseMatrix<double>& upperMatrix) {
    const Eigen::Index dofs = upperMatrix.rows();
    if (dofs == 0) return std::nullopt;
    const Eigen::VectorXd diagonal = upperMatrix.diagonal();

    // A DOF without any stiffness is a mechanism on its own.
    for (Eigen::Index dof = 0; dof < dofs; ++dof) {
      if (!(diagonal[dof] > 0.0)) return Mechanism{Eigen::VectorXd::Unit(dofs, dof), dof, 0.0};
    }

    // S K S with S = diag(1 / sqrt(K_ii)) has a unit diagonal; its pivots are d_k / K_kk.
    const Eigen::VectorXd scale = diagonal.cwiseSqrt().cwiseInverse();
    const Eigen::SparseMatrix<double> scaled = scale.asDiagonal() * upperMatrix * scale.asDiagonal();

    // An exactly zero pivot stops the factorization; it is then repeated with a diagonal shift
    // far below singularPivotRatio (it neither hides nor creates a mechanism). Without a need
    // the shift stays off: it bends the mode where DOF stiffnesses are many orders apart.
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Upper> ldlt;
    ldlt.compute(scaled);
    if (ldlt.info() == Eigen::NumericalIssue) {
      ldlt.setShift(1e-3 * singularPivotRatio);
      ldlt.compute(scaled);
    }
    if (ldlt.info() != Eigen::Success) return std::nullopt;

    Eigen::Index pivot = 0;
    const double pivotRatio = ldlt.vectorD().minCoeff(&pivot);
    if (pivotRatio > singularPivotRatio) return std::nullopt;

    // P S K S P^T = L D L^T with d_pivot ~ 0: y = L^-T e_pivot gives L D L^T y = d_pivot L e_pivot ~ 0,
    // so z = P^T y is a null vector of S K S and x = S z one of K.
    Eigen::VectorXd y = Eigen::VectorXd::Unit(dofs, pivot);
    ldlt.matrixU().solveInPlace(y);
    Eigen::VectorXd z = ldlt.permutationPinv() * y;

    Mechanism mechanism;
    mechanism.pivotRatio = pivotRatio;
    const double largest = z.cwiseAbs().maxCoeff(&mechanism.dof);
    z /= largest;
    mechanism.mode = scale.asDiagonal() * z;
    return mechanism;
  }

} // namespace FEM::SOLVER end
