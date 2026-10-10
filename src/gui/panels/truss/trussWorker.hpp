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

// Helpers shared by the truss panels' solver / preview workers (and main() at startup).

#include <bridge/generalStatus.hpp>

#include <Eigen/Core>
#include <omp.h>

#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace anaf::GUI::TRUSS_WORKER {

  // Leaves two cores to the GUI on machines with more than four. OpenMP thread settings are
  // per thread, so every worker std::jthread applies them again.
  inline void configureOpenMPForWorker() {
    const int availableThreads = omp_get_num_procs();
    const int threadCount = availableThreads > 4 ? availableThreads - 2 : availableThreads;
    omp_set_dynamic(0);
    omp_set_num_threads(threadCount);
    Eigen::setNbThreads(threadCount);
  }

  // The model to solve, made on the worker thread (building a large generated grid on the
  // GUI thread would stall the frame loop). An error stops the job with that message.
  using ModelSource = std::function<std::expected<std::shared_ptr<const BRIDGE::MeshData>, std::string>()>;

  // The one solve path of every truss panel: runs FEM::TRUSS::solveStatic() on
  // bridge.workerThread with a copy of the material list taken now (supports and loads are in
  // the snapshot), and publishes
  // the result snapshot unless the model was reset meanwhile (modelGeneration).
  // Call from the GUI thread.
  void startSolve(BRIDGE::GuiCalcBridge& bridge, ModelSource source);

} // namespace anaf::GUI::TRUSS_WORKER end
