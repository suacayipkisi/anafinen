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

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <omp.h>
#include <string>
#include <thread>

#if defined(__linux__)
#include <sys/sysinfo.h>
#endif

namespace FEM::TRUSS::SOLVER {

  const char* toString(const Kind kind) noexcept {
    switch (kind) {
      case Kind::Cholmod: return "CHOLMOD sparse Cholesky";
      case Kind::SimplicialLDLT: return "Eigen SimplicialLDLT";
      case Kind::BlockCG: return "OpenMP Block-CG";
    }
    return "Unknown";
  }

  namespace {
    double availableMemoryGiB() noexcept {
    #if defined(__linux__)
      struct sysinfo info{};
      if (sysinfo(&info) == 0) {
        return static_cast<double>(info.freeram) * info.mem_unit / (1024.0 * 1024.0 * 1024.0);
      }
    #endif
      return 0.0;
    }

    double totalMemoryGiB() noexcept {
    #if defined(__linux__)
      struct sysinfo info{};
      if (sysinfo(&info) == 0) {
        return static_cast<double>(info.totalram) * info.mem_unit / (1024.0 * 1024.0 * 1024.0);
      }
    #endif
      return 0.0;
    }

    std::string cpuModel() {
    #if defined(__linux__)
      std::ifstream cpuInfo("/proc/cpuinfo");
      std::string line;
      while (std::getline(cpuInfo, line)) {
        constexpr std::string_view prefix = "model name\t: ";
        if (line.starts_with(prefix)) return line.substr(prefix.size());
      }
    #endif
      return "unknown";
    }

    double relativeResidual(
      const Eigen::SparseMatrix<double>& upperMatrix,
      const Eigen::VectorXd& force,
      const Eigen::VectorXd& displacement
    ) {
      if (displacement.size() != force.size()) return std::numeric_limits<double>::infinity();
      if (force.size() == 0 || force.norm() == 0.0) return 0.0;
      const auto fullMatrix = upperMatrix.selfadjointView<Eigen::Upper>();
      const Eigen::VectorXd residual = force - fullMatrix * displacement;
      return residual.norm() / force.norm();
    }

    bool accepted(const Result& result) noexcept {
      return result.available && result.converged && std::isfinite(result.relativeResidual)
        && result.relativeResidual <= 1e-7;
    }

    void logHardware(const Eigen::Index dofs, const Eigen::Index nonZeros) {
      anaf::LOG::info(
        "Hardware: CPU '{}', OpenMP {}/{} threads, Eigen {} threads, hardware threads {}, RAM free/total {} / {} GiB, GPU backend CPU-only",
        cpuModel(), omp_get_max_threads(), omp_get_num_procs(), Eigen::nbThreads(),
        std::thread::hardware_concurrency(), availableMemoryGiB(), totalMemoryGiB()
      );
      anaf::LOG::info(
        "System for referee: DOFs {}, stored upper nnz {}",
        dofs, nonZeros
      );
    }
  }

  Result solveSelected(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    const std::uint32_t totalNodes,
    const std::vector<std::int32_t>& remapTable,
    const std::stop_token stopToken,
    Eigen::VectorXd& displacement
  ) {
    const Eigen::Index dofs = upperMatrix.rows();
    logHardware(dofs, upperMatrix.nonZeros());

    constexpr Eigen::Index directLimit = 400'000;
    Result result;

    if (dofs <= directLimit) {
    #ifdef ANAFINEN_HAS_CHOLMOD
      anaf::LOG::info("Solver referee selected CHOLMOD: {} DOFs <= {}", dofs, directLimit);
      result = solveCholmod(upperMatrix, force, displacement);
      result.relativeResidual = relativeResidual(upperMatrix, force, displacement);
      if (!accepted(result)) {
        anaf::LOG::warn("CHOLMOD was rejected; trying Eigen SimplicialLDLT");
        result = solveSimplicialLDLT(upperMatrix, force, displacement);
        result.relativeResidual = relativeResidual(upperMatrix, force, displacement);
      }
    #else
      anaf::LOG::info("Solver referee selected Eigen SimplicialLDLT: {} DOFs <= {}", dofs, directLimit);
      result = solveSimplicialLDLT(upperMatrix, force, displacement);
      result.relativeResidual = relativeResidual(upperMatrix, force, displacement);
    #endif
    }
    else {
      anaf::LOG::info("Solver referee selected OpenMP Block-CG: {} DOFs > {}", dofs, directLimit);
      result = solveBlockCG(upperMatrix, force, totalNodes, remapTable, stopToken, displacement);
      if (result.relativeResidual > 1e-7) {
        result.converged = false;
        result.message = "Block-CG residual is above acceptance threshold";
      }
    }

    anaf::LOG::info(
      "Solver referee result: {}, available {}, converged {}, residual {}, iterations {}, elapsed {} seconds{}{}",
      toString(result.kind), result.available, result.converged,
      result.relativeResidual, result.iterations, result.elapsedSeconds,
      result.message.empty() ? "" : ", reason: ", result.message
    );
    return result;
  }

} // namespace FEM::TRUSS::SOLVER
