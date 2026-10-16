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

  namespace {
    // y = K x for a symmetric K of which only the upper triangle is stored (column-major, as
    // the containers assemble it), without building the full matrix: Block-CG then holds no
    // second copy of K. Column c of the upper triangle is row c of the lower one. Chunk t owns
    // the columns [m_begin[t], m_begin[t + 1]) (balanced by entries) and, for each entry K_rc
    // (r <= c), adds K_rc x_r to y_c (gather) and K_rc x_c to y_r (scatter, r < c). A scatter
    // row inside the chunk is written directly; one below it goes to the chunk's spill buffer,
    // which spans only the rows its columns reach, and is added after a barrier by the chunk
    // owning that row. With node-by-node DOF numbering the spill is a narrow band and the
    // product reads half the bytes of the full one (about 1.7x faster); a randomly numbered
    // mesh widens the spill and scatters across memory (about 1.8x slower), still without the
    // full copy.
    class UpperSymmetricProduct {
    public:
      UpperSymmetricProduct(const Eigen::SparseMatrix<double>& upper, const int chunks) :
        m_upper(upper),
        m_begin(static_cast<std::size_t>(chunks) + 1, upper.cols()),
        m_spillFirst(static_cast<std::size_t>(chunks), 0),
        m_spill(static_cast<std::size_t>(chunks))
      {
        m_begin[0] = 0;
        const double total = static_cast<double>(upper.nonZeros());
        Eigen::Index count = 0;
        std::size_t next = 1;
        for (Eigen::Index col = 0; col < upper.cols() && next < m_spill.size(); ++col) {
          count += upper.innerVector(col).nonZeros();
          while (next < m_spill.size() && static_cast<double>(count) >= total * static_cast<double>(next) / chunks) m_begin[next++] = col + 1;
        }

        #pragma omp parallel for schedule(dynamic, 1)
        for (long long chunk = 0; chunk < chunks; ++chunk) {
          const std::size_t t = static_cast<std::size_t>(chunk);
          Eigen::Index first = m_begin[t];
          for (Eigen::Index col = m_begin[t]; col < m_begin[t + 1]; ++col) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(upper, col); it; ++it) first = std::min(first, it.row());
          }
          m_spillFirst[t] = first;
          m_spill[t].assign(static_cast<std::size_t>(m_begin[t] - first), 0.0);
        }
      }

      std::size_t spillEntries() const {
        std::size_t entries = 0;
        for (const auto& spill : m_spill) entries += spill.size();
        return entries;
      }

      std::size_t chunks() const { return m_spill.size(); }

      // x and y must not alias.
      void multiply(const Eigen::VectorXd& x, Eigen::VectorXd& y) {
        const long long chunkCount = static_cast<long long>(m_spill.size());
        #pragma omp parallel
        {
          const long long threads = omp_get_num_threads();
          for (long long chunk = omp_get_thread_num(); chunk < chunkCount; chunk += threads) {
            const std::size_t t = static_cast<std::size_t>(chunk);
            const Eigen::Index begin = m_begin[t];
            const Eigen::Index end = m_begin[t + 1];
            y.segment(begin, end - begin).setZero();
            std::ranges::fill(m_spill[t], 0.0);
            double* spill = m_spill[t].data();
            for (Eigen::Index col = begin; col < end; ++col) {
              const double xCol = x[col];
              double sum = 0.0;
              for (Eigen::SparseMatrix<double>::InnerIterator it(m_upper, col); it; ++it) {
                const Eigen::Index row = it.row();
                if (row > col) continue; // a stored lower entry is not part of the upper triangle
                const double value = it.value();
                sum += value * x[row];
                if (row == col) continue;
                if (row >= begin) y[row] += value * xCol;
                else spill[row - m_spillFirst[t]] += value * xCol;
              }
              y[col] += sum;
            }
          }

          #pragma omp barrier

          for (long long chunk = omp_get_thread_num(); chunk < chunkCount; chunk += threads) {
            const std::size_t t = static_cast<std::size_t>(chunk);
            for (std::size_t other = t + 1; other < m_spill.size(); ++other) {
              const Eigen::Index from = std::max(m_begin[t], m_spillFirst[other]);
              const Eigen::Index to = std::min(m_begin[t + 1], m_begin[other]);
              for (Eigen::Index row = from; row < to; ++row) y[row] += m_spill[other][static_cast<std::size_t>(row - m_spillFirst[other])];
            }
          }
        }
      }

    private:
      const Eigen::SparseMatrix<double>& m_upper;
      std::vector<Eigen::Index> m_begin;      // chunk column bounds, chunks + 1 entries
      std::vector<Eigen::Index> m_spillFirst; // lowest scatter row of each chunk
      std::vector<std::vector<double>> m_spill;
    };
  } // namespace end

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

    if (upperMatrix.rows() != activeDofs || upperMatrix.cols() != activeDofs) {
      result.message = std::format("Block-CG matrix is {} x {}, expected {} DOFs", upperMatrix.rows(), upperMatrix.cols(), activeDofs);
      return result;
    }

    displacement = Eigen::VectorXd::Zero(activeDofs);
    UpperSymmetricProduct stiffness(upperMatrix, std::max(omp_get_max_threads(), 1));
    anaf::LOG::info("Block-CG: symmetric product on the stored upper triangle ({} entries), {} chunks, spill buffers {:.1f} MB",
                    upperMatrix.nonZeros(), stiffness.chunks(), static_cast<double>(stiffness.spillEntries()) * sizeof(double) / 1e6);
    // Entry (row, col) of K from the upper triangle.
    const auto coefficient = [&](const Eigen::Index row, const Eigen::Index col) {
      return upperMatrix.coeff(std::min(row, col), std::max(row, col));
    };

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
          block(row, col) = coefficient(remapTable[first + slots[row]], remapTable[first + slots[col]]);
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

    Eigen::VectorXd product(activeDofs);
    stiffness.multiply(displacement, product);
    Eigen::VectorXd residual = force - product;
    const double forceNormSquared = force.squaredNorm();
    const double threshold = std::max(
      tolerance * tolerance * forceNormSquared,
      std::numeric_limits<double>::min()
    );
    double residualNormSquared = residual.squaredNorm();
    Eigen::VectorXd direction(activeDofs);
    Eigen::VectorXd preconditioned(activeDofs);
    applyPreconditioner(residual, direction);
    double rho = residual.dot(direction);

    for (Eigen::Index iteration = 0;
         !stopToken.stop_requested()
         && iteration < maxIterations
         && residualNormSquared >= threshold
         && forceNormSquared > 0.0;
         ++iteration) {
      stiffness.multiply(direction, product);
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
