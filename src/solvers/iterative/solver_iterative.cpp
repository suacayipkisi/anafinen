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

#include <Eigen/LU> // MatrixXd::inverse()
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
    Result result{.type = Type::BlockCG};
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
    // and columns of unused slots stay zero.
    const std::size_t blockSize = static_cast<std::size_t>(dofsPerNode) * dofsPerNode;
    std::vector<double> inverses(static_cast<std::size_t>(totalNodes) * blockSize, 0.0);

    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < totalNodes; ++node) {
      const std::size_t first = static_cast<std::size_t>(node) * dofsPerNode;
      std::array<std::uint32_t, maxDofsPerNode> slots{};
      int count = 0;
      for (std::uint32_t slot = 0; slot < dofsPerNode; ++slot) {
        if (remapTable[first + slot] >= 0) slots[count++] = slot;
      }
      if (count == 0) continue;

      Eigen::MatrixXd block(count, count);
      for (int row = 0; row < count; ++row) {
        for (int col = 0; col < count; ++col) {
          block(row, col) = matrix.coeff(remapTable[first + slots[row]], remapTable[first + slots[col]]);
        }
      }
      const Eigen::MatrixXd inverse = block.inverse();
      double* nodeInverse = inverses.data() + static_cast<std::size_t>(node) * blockSize;
      for (int row = 0; row < count; ++row) {
        for (int col = 0; col < count; ++col) {
          nodeInverse[slots[row] * dofsPerNode + slots[col]] = inverse(row, col);
        }
      }
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
