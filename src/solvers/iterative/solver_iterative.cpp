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
#include <log/anaf_info.hpp>

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <Eigen/SparseCore>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <omp.h>

namespace FEM::SOLVER {

  Result solveBlockCG(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    const std::uint32_t totalNodes,
    const std::uint32_t dofsPerNode,
    const std::vector<std::int32_t>& remapTable,
    const std::stop_token stopToken,
    Eigen::VectorXd& displacement
  ) {
    Result result{.type = E_Type::BlockCG};
    const auto start = std::chrono::steady_clock::now();
    const Eigen::Index activeDofs = force.size();
    constexpr Eigen::Index maxIterations = 50'000;
    constexpr Eigen::Index logInterval = 200;
    constexpr double tolerance = 1e-8;

    if (dofsPerNode == 0 || dofsPerNode > maxDofsPerNode) {
      result.message = std::format("Block-CG supports 1 to {} DOFs per node, got {}", maxDofsPerNode, dofsPerNode);
      return result;
    }
    if (remapTable.size() != static_cast<std::size_t>(totalNodes) * dofsPerNode) {
      result.message = std::format(
        "Block-CG remap table has {} entries, expected {} nodes x {} DOFs",
        remapTable.size(), totalNodes, dofsPerNode
      );
      return result;
    }

    Eigen::SparseMatrix<double, Eigen::RowMajor> matrix = upperMatrix.selfadjointView<Eigen::Upper>();
    displacement = Eigen::VectorXd::Zero(activeDofs);

    // One dofsPerNode x dofsPerNode inverse per node, row-major, indexed by DOF slot; the rows
    // and columns of unused slots stay zero. The blocks are principal submatrices of K, so they
    // are positive definite whenever K is: a block whose Cholesky pivot / diagonal (the
    // direct solvers' measure) is at or below singularPivotRatio means K is singular, and its
    // null vector is a mechanism of that node alone. The block size is at most
    // maxDofsPerNode, so the blocks live on the stack.
    const std::size_t blockSize = static_cast<std::size_t>(dofsPerNode) * dofsPerNode;
    std::vector<double> inverses(static_cast<std::size_t>(totalNodes) * blockSize, 0.0);
    std::vector<double> nodePivotRatios(totalNodes, std::numeric_limits<double>::infinity());

    const auto gatherBlock = [&](const std::size_t node, std::array<std::uint32_t, maxDofsPerNode>& slots, int& count) {
      const std::size_t first = node * dofsPerNode;
      count = 0;
      for (std::uint32_t slot = 0; slot < dofsPerNode; ++slot) {
        if (remapTable[first + slot] >= 0) slots[count++] = slot;
      }
      Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor, maxDofsPerNode, maxDofsPerNode> block(count, count);
      for (int row = 0; row < count; ++row) {
        for (int col = 0; col < count; ++col) {
          block(row, col) = matrix.coeff(remapTable[first + slots[row]], remapTable[first + slots[col]]);
        }
      }
      return block;
    };

    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < totalNodes; ++node) {
      std::array<std::uint32_t, maxDofsPerNode> slots{};
      int count = 0;
      const auto block = gatherBlock(static_cast<std::size_t>(node), slots, count);
      if (count == 0) continue;

      const Eigen::LLT<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor, maxDofsPerNode, maxDofsPerNode>> llt(block);
      double pivotRatio = llt.info() == Eigen::Success ? std::numeric_limits<double>::infinity() : 0.0;
      for (int k = 0; k < count && pivotRatio > 0.0; ++k) {
        const double diagonal = block(k, k);
        const double pivot = llt.matrixLLT()(k, k) * llt.matrixLLT()(k, k);
        pivotRatio = diagonal > 0.0 ? std::min(pivotRatio, pivot / diagonal) : 0.0;
      }
      nodePivotRatios[static_cast<std::size_t>(node)] = pivotRatio;
      if (!(pivotRatio > singularPivotRatio)) continue;

      const auto inverse = llt.solve(
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor, maxDofsPerNode, maxDofsPerNode>::Identity(count, count)
      ).eval();
      double* nodeInverse = inverses.data() + static_cast<std::size_t>(node) * blockSize;
      for (int row = 0; row < count; ++row) {
        for (int col = 0; col < count; ++col) {
          nodeInverse[slots[row] * dofsPerNode + slots[col]] = inverse(row, col);
        }
      }
    }

    // Serial scan (MSVC's OpenMP 2.0 has no min reduction).
    const auto worst = std::ranges::min_element(nodePivotRatios);
    if (worst != nodePivotRatios.end()) result.smallestPivotRatio = *worst;
    if (worst != nodePivotRatios.end() && !(*worst > singularPivotRatio)) {
      const std::size_t node = static_cast<std::size_t>(worst - nodePivotRatios.begin());
      std::array<std::uint32_t, maxDofsPerNode> slots{};
      int count = 0;
      const auto block = gatherBlock(node, slots, count);

      // Null vector of the unit-diagonal scaled block, largest component 1. A zero (or
      // negative) diagonal entry k of a positive semidefinite K has a zero row: e_k is the mode.
      Eigen::Matrix<double, Eigen::Dynamic, 1, Eigen::ColMajor, maxDofsPerNode, 1> scaled(count);
      Eigen::Matrix<double, Eigen::Dynamic, 1, Eigen::ColMajor, maxDofsPerNode, 1> scale(count);
      int zeroDiagonal = -1;
      for (int k = 0; k < count; ++k) {
        if (!(block(k, k) > 0.0) && zeroDiagonal < 0) zeroDiagonal = k;
        scale[k] = block(k, k) > 0.0 ? 1.0 / std::sqrt(block(k, k)) : 0.0;
      }
      if (zeroDiagonal >= 0) {
        scaled.setZero();
        scaled[zeroDiagonal] = 1.0;
      }
      else {
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor, maxDofsPerNode, maxDofsPerNode>> eigen(
          scale.asDiagonal() * block * scale.asDiagonal()
        );
        scaled = eigen.eigenvectors().col(0);
      }
      Eigen::Index largest = 0;
      scaled.cwiseAbs().maxCoeff(&largest);
      scaled /= scaled[largest];

      result.singular = true;
      result.mechanismMode = Eigen::VectorXd::Zero(activeDofs);
      for (int k = 0; k < count; ++k) {
        const std::int32_t reduced = remapTable[node * dofsPerNode + slots[k]];
        result.mechanismMode[reduced] = zeroDiagonal >= 0 ? scaled[k] : scaled[k] * scale[k];
      }
      result.mechanismDof = remapTable[node * dofsPerNode + slots[static_cast<std::size_t>(largest)]];
      result.message = std::format(
        "Block-CG: the diagonal block of node {} is singular (pivot / diagonal {:.2e}), the stiffness is singular (a mechanism)",
        node, *worst
      );
      result.available = true;
      result.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      return result;
    }

    auto applyPreconditioner = [&](const Eigen::VectorXd& input, Eigen::VectorXd& output) {
      #pragma omp parallel for schedule(static)
      for (long long node = 0; node < totalNodes; ++node) {
        const std::int32_t* dofs = remapTable.data() + static_cast<std::size_t>(node) * dofsPerNode;
        const double* nodeInverse = inverses.data() + static_cast<std::size_t>(node) * blockSize;
        std::array<double, maxDofsPerNode> local{};
        for (std::uint32_t slot = 0; slot < dofsPerNode; ++slot) {
          if (dofs[slot] >= 0) local[slot] = input[dofs[slot]];
        }
        for (std::uint32_t row = 0; row < dofsPerNode; ++row) {
          if (dofs[row] < 0) continue;
          double sum = 0.0;
          for (std::uint32_t col = 0; col < dofsPerNode; ++col) sum += nodeInverse[row * dofsPerNode + col] * local[col];
          output[dofs[row]] = sum;
        }
      }
    };

    Eigen::VectorXd residual = force - matrix * displacement;
    const double forceNormSquared = force.squaredNorm();
    const double threshold = std::max(
      tolerance * tolerance * forceNormSquared,
      std::numeric_limits<double>::min()
    );
    double residualNormSquared = residual.squaredNorm();
    Eigen::VectorXd direction(activeDofs);
    Eigen::VectorXd preconditioned(activeDofs);
    Eigen::VectorXd product(activeDofs);
    applyPreconditioner(residual, direction);
    double rho = residual.dot(direction);

    for (Eigen::Index iteration = 0;
         !stopToken.stop_requested()
         && iteration < maxIterations
         && residualNormSquared >= threshold
         && forceNormSquared > 0.0;
         ++iteration) {
      product.noalias() = matrix * direction;
      const double denominator = direction.dot(product);
      if (!std::isfinite(denominator) || denominator <= 0.0) {
        result.message = "CG encountered a non-positive curvature";
        break;
      }
      const double alpha = rho / denominator;
      displacement += alpha * direction;
      residual -= alpha * product;
      residualNormSquared = residual.squaredNorm();
      result.iterations = iteration + 1;
      if (residualNormSquared < threshold) break;

      applyPreconditioner(residual, preconditioned);
      const double oldRho = rho;
      rho = residual.dot(preconditioned);
      if (!std::isfinite(rho) || oldRho == 0.0) {
        result.message = "CG preconditioner produced a non-finite direction";
        break;
      }
      direction = preconditioned + (rho / oldRho) * direction;

      if (result.iterations % logInterval == 0) {
        anaf::LOG::info(
          "Block-CG progress: {} / {} iterations, estimated error {:.3e}, elapsed {:.2f} seconds",
          result.iterations, maxIterations,
          std::sqrt(residualNormSquared / forceNormSquared),
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
        );
      }
    }

    if (stopToken.stop_requested()) result.message = "Calculation cancelled by application shutdown";
    result.converged = forceNormSquared == 0.0 || residualNormSquared < threshold;
    if (stopToken.stop_requested()) result.converged = false;
    result.relativeResidual = forceNormSquared == 0.0
      ? 0.0
      : std::sqrt(residualNormSquared / forceNormSquared);
    result.available = true;
    result.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
  }

} // namespace FEM::SOLVER end
