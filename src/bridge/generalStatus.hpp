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

#include <truss_1D/trussProperties/appliedForce.hpp>
#include <truss_1D/trussProperties/node.hpp>
#include <truss_1D/trussProperties/element.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anaf::BRIDGE {
  enum ObjectType {
    truss_SQPT,
    truss_imported_or_entered
  };

  struct MeshData {

    // truss (1_D element) deformation under constant applied force
    std::vector<FEM::TRUSS::Node> trussNodes;
    std::vector<FEM::TRUSS::TrussElement_1D> trussElements;
    std::vector<FEM::TRUSS::ForceApplied> appliedForces;
    std::atomic<double> deformScale{1.0};

    MeshData() = default;

    MeshData(const MeshData& other)
      : trussNodes(other.trussNodes),
       trussElements(other.trussElements),
       appliedForces(other.appliedForces),
       deformScale(other.deformScale.load()) {}

    MeshData& operator=(const MeshData& other) {
      if (this != &other) {
        trussNodes = other.trussNodes;
        trussElements = other.trussElements;
        appliedForces = other.appliedForces;
        deformScale.store(other.deformScale.load());
      }
      return *this;
    }

    MeshData(MeshData&& other) noexcept
      : trussNodes(std::move(other.trussNodes)),
       trussElements(std::move(other.trussElements)),
       appliedForces(std::move(other.appliedForces)),
       deformScale(other.deformScale.load()) {}

    MeshData& operator=(MeshData&& other) noexcept {
      if (this != &other) {
        trussNodes = std::move(other.trussNodes);
        trussElements = std::move(other.trussElements);
        appliedForces = std::move(other.appliedForces);
        deformScale.store(other.deformScale.load());
      }
      return *this;
    }
    
  };

  struct Gui_Calc_Bridge {
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_isGeneratingPreview{false};
    std::atomic<float> m_progress{0.0f};
    std::atomic<uint64_t> dataVersion{0};
    std::mutex dataMutex;
    std::jthread workerThread;

    std::atomic<ObjectType> m_objectType;
    std::shared_ptr<const MeshData> activeMesh{nullptr};
    std::atomic<bool> m_isValid{false};
    std::atomic<double> m_energyDiff;

    // general access
    std::vector<anaf::MATERIAL::Material> allMaterials;

    std::unordered_map<std::uint32_t, std::array<bool, 3>> fixedDOFsByNode;
    std::uint32_t selectedNodeId{std::numeric_limits<std::uint32_t>::max()};
    bool hasTrussPreview{false};

    
  };

  Gui_Calc_Bridge& buildBridge();

} // namespace anaf::BRIDGE end


