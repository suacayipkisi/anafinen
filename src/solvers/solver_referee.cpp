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

#include "solverPortfolio.hpp"
#include <log/anaf_info.hpp>
#include <platform/systemInfo.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <omp.h>
#include <string>
#include <utility>

namespace FEM::SOLVER {

  const char* toString(const Type type) noexcept {
    switch (type) {
      case Type::Cholmod: return "CHOLMOD sparse Cholesky";
      case Type::SimplicialLDLT: return "Eigen SimplicialLDLT";
      case Type::BlockCG: return "OpenMP Block-CG";
    }
    return "Unknown";
  }

  namespace {
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

    [[maybe_unused]] bool accepted(const Result& result) noexcept { // only called with CHOLMOD
      return result.available && result.converged && std::isfinite(result.relativeResidual)
        && result.relativeResidual <= 1e-7;
    }

    // Adds the mechanism mode to a singular result; false when no mechanism is found (the
    // singular verdict then rests on that one solver).
    bool attachMechanism(const Eigen::SparseMatrix<double>& upperMatrix, Result& result) {
      auto mechanism = findMechanism(upperMatrix);
      if (!mechanism) return false;
      result.mechanismMode = std::move(mechanism->mode);
      result.mechanismDof = mechanism->dof;
      result.converged = false;
      result.message = std::format("the stiffness is singular (a mechanism): {} (smallest pivot / diagonal {:.2e})",
                                   result.message, mechanism->pivotRatio);
      return true;
    }

    void logHardware(const Eigen::Index dofs, const Eigen::Index nonZeros) {
      static const auto system = anaf::PLATFORM::querySystemInfo(); // does not change while running
      const auto memory = anaf::PLATFORM::queryMemory().value_or(anaf::PLATFORM::MemoryStatus{});
      anaf::LOG::info(
        "Hardware: CPU '{}', {} hardware threads, OpenMP {} / {} threads, Eigen {} threads, RAM available / total {:.1f} / {:.1f} GiB",
        system.cpuName, system.threads, omp_get_max_threads(), omp_get_num_procs(), Eigen::nbThreads(),
        memory.availableGiB, memory.totalGiB
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
    const std::uint32_t dofsPerNode,
    const std::vector<std::int32_t>& remapTable,
    const std::stop_token stopToken,
    Eigen::VectorXd& displacement
  ) {
    const Eigen::Index dofs = upperMatrix.rows();
    logHardware(dofs, upperMatrix.nonZeros());

    constexpr Eigen::Index directLimit = 400'000;
    Result result;
    displacement.setZero(dofs); // a failed solve leaves zeros, not uninitialized values

    // Every DOF is restrained: nothing to solve. Debian 13's CHOLMOD rejects an empty matrix
    // ("invalid xtype or dtype" in cholmod_analyze) and the factorization then crashes.
    if (dofs == 0) {
      displacement.resize(0);
      result.type = Type::SimplicialLDLT;
      result.available = true;
      result.converged = true;
      result.message = "no free DOFs";
      anaf::LOG::info("Solver referee: no free DOFs, nothing to solve");
      return result;
    }

    if (dofs <= directLimit) {
    #ifdef ANAFINEN_HAS_CHOLMOD
      anaf::LOG::info("Solver referee selected CHOLMOD: {} DOFs <= {}", dofs, directLimit);
      result = solveCholmod(upperMatrix, force, displacement);
      result.relativeResidual = relativeResidual(upperMatrix, force, displacement);
      // A singular stiffness is the model's fault, not CHOLMOD's: SimplicialLDLT would accept
      // the rounding level pivot of the mechanism and return an arbitrary amount of its mode.
      if (!accepted(result) && !(result.singular && attachMechanism(upperMatrix, result))) {
        anaf::LOG::warn("CHOLMOD was rejected ({}); trying Eigen SimplicialLDLT", result.message);
        result = solveSimplicialLDLT(upperMatrix, force, displacement);
        result.relativeResidual = relativeResidual(upperMatrix, force, displacement);
        if (result.singular) attachMechanism(upperMatrix, result);
      }
    #else
      anaf::LOG::info("Solver referee selected Eigen SimplicialLDLT: {} DOFs <= {}", dofs, directLimit);
      result = solveSimplicialLDLT(upperMatrix, force, displacement);
      result.relativeResidual = relativeResidual(upperMatrix, force, displacement);
      if (result.singular) attachMechanism(upperMatrix, result);
    #endif
    }
    else {
      anaf::LOG::info("Solver referee selected OpenMP Block-CG: {} DOFs > {}", dofs, directLimit);
      result = solveBlockCG(upperMatrix, force, totalNodes, dofsPerNode, remapTable, stopToken, displacement);
      if (result.relativeResidual > 1e-7) {
        result.converged = false;
        result.message = "Block-CG residual is above acceptance threshold";
      }
    }

    anaf::LOG::info(
      "Solver referee result: {}, available {}, converged {}, residual {:.3e}, smallest pivot / diagonal {:.3e}, iterations {}, elapsed {:.3f} seconds{}{}",
      toString(result.type), result.available, result.converged,
      result.relativeResidual, result.smallestPivotRatio, result.iterations, result.elapsedSeconds,
      result.message.empty() ? "" : ", reason: ", result.message
    );
    return result;
  }

} // namespace FEM::SOLVER end
