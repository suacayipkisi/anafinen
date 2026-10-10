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

#include "trussWorker.hpp"

#include <log/anaf_info.hpp>
#include <truss_1D/trussEngine/trussSolver.hpp>

#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace anaf::GUI::TRUSS_WORKER {

  void startSolve(BRIDGE::GuiCalcBridge& bridge, ModelSource source) {
    // Join first: a worker that is still finishing clears isRunning on exit.
    bridge.joinWorker();
    std::vector<MATERIAL::Material> materials;
    std::uint64_t generation = 0;
    {
      std::lock_guard lock(bridge.dataMutex);
      materials = bridge.allMaterials;
      generation = bridge.modelGeneration.load();
    }
    bridge.isRunning = true;
    bridge.progress = 0.0f;

    bridge.workerThread = std::jthread(
      [&bridge, source = std::move(source), materials = std::move(materials), generation]
      (std::stop_token st) {
        try {
          configureOpenMPForWorker();
          const auto model = source();
          if (!model) {
            anaf::LOG::error("Solver not started: {}", model.error());
          } else if (auto solved = FEM::TRUSS::solveStatic(**model, materials, st, [&bridge](const float fraction) {
                       bridge.progress = fraction;
                     }); !solved) {
            if (!st.stop_requested()) anaf::LOG::error("Solver failed: {}", solved.error());
          } else {
            bool published = false;
            {
              std::lock_guard lock(bridge.dataMutex);
              if (bridge.modelGeneration.load() == generation) { // not reset while solving
                bridge.activeMesh = std::move(solved->mesh);
                bridge.isValid = solved->energyCheckPassed;
                bridge.energyDiff = solved->energyDiff;
                published = true;
              }
            }
            if (published) bridge.dataVersion.fetch_add(1, std::memory_order_release);
          }
        } catch (const std::exception& exception) {
          anaf::LOG::error("Solver failed: {}", exception.what());
        }
        bridge.progress = 1.0f;
        bridge.isRunning = false;
      });
  }

} // namespace anaf::GUI::TRUSS_WORKER end
