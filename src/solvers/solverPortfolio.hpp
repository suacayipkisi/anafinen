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

#include <Eigen/SparseCore>
#include <Eigen/Core>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <stop_token>
#include <vector>

namespace FEM::SOLVER {

  enum class E_Type {
    Cholmod,
    SimplicialLDLT,
    BlockCG
  };

  // A supported structure has a positive definite stiffness. The direct solvers compare every
  // pivot with the diagonal entry of its DOF (d_k / K_kk, the pivot of the unit-diagonal scaled
  // matrix, so translations and rotations compare alike): a ratio at this level is rounding
  // noise left of a zero pivot, i.e. the structure is a mechanism.
  inline constexpr double singularPivotRatio = 1e-10;

  struct Result {
    E_Type type{};
    bool available{false};
    bool converged{false};
    // The factorization met a zero (or rounding level) pivot: the structure is a mechanism.
    // converged is false then.
    bool singular{false};
    // Smallest d_k / K_kk of a direct factorization (infinity when not computed).
    double smallestPivotRatio{std::numeric_limits<double>::infinity()};
    // A singular solve's mechanism mode (null vector of the reduced stiffness, in reduced DOFs,
    // largest scaled component 1) and the reduced DOF moving the most in it; empty / -1 when
    // the mode could not be found.
    Eigen::VectorXd mechanismMode{};
    Eigen::Index mechanismDof{-1};
    Eigen::Index iterations{0};
    double relativeResidual{0.0};
    double elapsedSeconds{0.0};
    std::string message{};
  };

  struct Mechanism {
    Eigen::VectorXd mode; // null vector in reduced DOFs
    Eigen::Index dof{-1};  // DOF with the largest scaled component
    double pivotRatio{0.0};
  };

  // Mechanism mode of a singular stiffness (upper triangle stored): LDL^T of the unit-diagonal
  // scaled matrix with a tiny diagonal shift (so an exactly zero pivot does not stop it), then
  // L^T y = e_k at the smallest pivot k. Empty when every pivot is above singularPivotRatio.
  std::optional<Mechanism> findMechanism(const Eigen::SparseMatrix<double>& upperMatrix);

  Result solveCholmod(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    Eigen::VectorXd& displacement
  );

  Result solveSimplicialLDLT(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    Eigen::VectorXd& displacement
  );

  // Largest node block Block-CG accepts: 3 translations + 3 rotations (beam, shell).
  inline constexpr std::uint32_t maxDofsPerNode = 6;

  // remapTable holds, at dofsPerNode * node + k, the reduced DOF of the k-th DOF slot of that
  // node (-1 when unused); Block-CG inverts one dofsPerNode x dofsPerNode block per node for
  // its Jacobi preconditioner. dofsPerNode is 3 for a truss, 6 for a 3D beam.
  Result solveBlockCG(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    std::uint32_t totalNodes,
    std::uint32_t dofsPerNode,
    const std::vector<std::int32_t>& remapTable,
    std::stop_token stopToken,
    Eigen::VectorXd& displacement
  );

  Result solveSelected(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    std::uint32_t totalNodes,
    std::uint32_t dofsPerNode,
    const std::vector<std::int32_t>& remapTable,
    std::stop_token stopToken,
    Eigen::VectorXd& displacement
  );

  const char* toString(E_Type type) noexcept;

} // namespace FEM::SOLVER end
