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

#pragma once

#include <Eigen/SparseCore>
#include <Eigen/Core>

#include <cstdint>
#include <string>
#include <stop_token>
#include <vector>

namespace FEM::TRUSS::SOLVER {

  enum class Kind {
    Cholmod,
    SimplicialLDLT,
    BlockCG
  };

  struct Result {
    Kind kind;
    bool available{false};
    bool converged{false};
    Eigen::Index iterations{0};
    double relativeResidual{0.0};
    double elapsedSeconds{0.0};
    std::string message;
  };

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

  Result solveBlockCG(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    std::uint32_t totalNodes,
    const std::vector<std::int32_t>& remapTable,
    std::stop_token stopToken,
    Eigen::VectorXd& displacement
  );

  Result solveSelected(
    const Eigen::SparseMatrix<double>& upperMatrix,
    const Eigen::VectorXd& force,
    std::uint32_t totalNodes,
    const std::vector<std::int32_t>& remapTable,
    std::stop_token stopToken,
    Eigen::VectorXd& displacement
  );

  const char* toString(Kind kind) noexcept;

} // namespace FEM::TRUSS::SOLVER
