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

#include "beamWorker.hpp"

#include <beam/beamEngine/beamSolver.hpp>
#include <log/anaf_info.hpp>
#include <panels/truss/trussWorker.hpp> // configureOpenMPForWorker()

#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace anaf::GUI::BEAM_WORKER {

  void startSolve(BRIDGE::GuiCalcBridge& bridge, std::shared_ptr<const BRIDGE::BeamMeshData> mesh) {
    if (!mesh) return;
    bridge.joinWorker(); // a worker that is still finishing clears isRunning on exit
    std::vector<MATERIAL::Material> materials;
    std::vector<FEM::BEAM::BeamSection> sections;
    std::uint64_t generation = 0;
    {
      std::lock_guard lock(bridge.dataMutex);
      materials = bridge.allMaterials;
      sections = bridge.allSections;
      generation = bridge.modelGeneration.load();
    }
    bridge.isRunning = true;
    bridge.progress = 0.0f;

    bridge.workerThread = std::jthread(
      [&bridge, mesh = std::move(mesh), materials = std::move(materials), sections = std::move(sections), generation]
      (std::stop_token st) {
        try {
          TRUSS_WORKER::configureOpenMPForWorker();
          auto solved = FEM::BEAM::solveStatic(*mesh, materials, sections, st, [&bridge](const float fraction) {
            bridge.progress = fraction;
          });
          if (!solved) {
            if (!st.stop_requested()) anaf::LOG::error("Beam solver failed: {}", solved.error());
          } else {
            bool published = false;
            {
              std::lock_guard lock(bridge.dataMutex);
              if (bridge.modelGeneration.load() == generation) { // not reset while solving
                bridge.activeBeamMesh = std::move(solved->mesh);
                bridge.isValid = solved->energyCheckPassed;
                bridge.energyDiff = solved->energyDiff;
                published = true;
              }
            }
            if (published) bridge.dataVersion.fetch_add(1, std::memory_order_release);
          }
        } catch (const std::exception& exception) {
          anaf::LOG::error("Beam solver failed: {}", exception.what());
        }
        bridge.progress = 1.0f;
        bridge.isRunning = false;
      });
  }

} // namespace anaf::GUI::BEAM_WORKER end
