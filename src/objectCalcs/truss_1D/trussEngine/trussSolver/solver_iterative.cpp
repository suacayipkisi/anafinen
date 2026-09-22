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

#include <Eigen/Sparse>
#include <Eigen/LU>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <omp.h>

namespace FEM::TRUSS::SOLVER {

  Result solveBlockCG(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    const std::uint32_t totalNodes,
    const std::vector<std::int32_t>& remapTable,
    const std::stop_token stopToken,
    Eigen::VectorXd& displacement
  ) {
    Result result{Kind::BlockCG};
    const auto start = std::chrono::steady_clock::now();
    const Eigen::Index activeDofs = force.size();
    constexpr Eigen::Index maxIterations = 50'000;
    constexpr Eigen::Index logInterval = 200;
    constexpr double tolerance = 1e-8;

    Eigen::SparseMatrix<double, Eigen::RowMajor> matrix = upperMatrix.selfadjointView<Eigen::Upper>();
    displacement = Eigen::VectorXd::Zero(activeDofs);

    struct NodeBlock { std::array<double, 9> inverse{}; };
    std::vector<NodeBlock> blocks(totalNodes);

    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < totalNodes; ++node) {
      std::array<std::int32_t, 3> dofs{
        remapTable[3 * node], remapTable[3 * node + 1], remapTable[3 * node + 2]
      };
      std::array<int, 3> axes{};
      int count = 0;
      for (int axis = 0; axis < 3; ++axis) {
        if (dofs[axis] >= 0) axes[count++] = axis;
      }
      if (count == 0) continue;

      Eigen::MatrixXd block(count, count);
      for (int row = 0; row < count; ++row) {
        for (int col = 0; col < count; ++col) {
          block(row, col) = matrix.coeff(dofs[axes[row]], dofs[axes[col]]);
        }
      }
      const Eigen::MatrixXd inverse = block.inverse();
      for (int row = 0; row < count; ++row) {
        for (int col = 0; col < count; ++col) {
          blocks[node].inverse[axes[row] * 3 + axes[col]] = inverse(row, col);
        }
      }
    }

    auto applyPreconditioner = [&](const Eigen::VectorXd& input, Eigen::VectorXd& output) {
      #pragma omp parallel for schedule(static)
      for (long long node = 0; node < totalNodes; ++node) {
        const auto d0 = remapTable[3 * node];
        const auto d1 = remapTable[3 * node + 1];
        const auto d2 = remapTable[3 * node + 2];
        const double x0 = d0 >= 0 ? input[d0] : 0.0;
        const double x1 = d1 >= 0 ? input[d1] : 0.0;
        const double x2 = d2 >= 0 ? input[d2] : 0.0;
        const auto& inverse = blocks[node].inverse;
        if (d0 >= 0) output[d0] = inverse[0] * x0 + inverse[1] * x1 + inverse[2] * x2;
        if (d1 >= 0) output[d1] = inverse[3] * x0 + inverse[4] * x1 + inverse[5] * x2;
        if (d2 >= 0) output[d2] = inverse[6] * x0 + inverse[7] * x1 + inverse[8] * x2;
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
          "Block-CG progress: {} / {} iterations, estimated error {}, elapsed {} seconds",
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

} // namespace FEM::TRUSS::SOLVER
