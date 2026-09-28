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

#include "material/properties.hpp"
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
    truss_imported_or_entered,
    no_type
  };

  std::string_view getObjectTypeName(ObjectType obj);

  // nodeId -> {fixedX, fixedY, fixedZ}
  using FixedDOFMap = std::unordered_map<std::uint32_t, std::array<bool, 3>>;

  struct RenderElement {
    std::uint32_t node1{};
    std::uint32_t node2{};
    float stress{};               // Pa, tension > 0
    bool isStressExceeded{false};
    std::uint32_t materialID{};   // index into Gui_Calc_Bridge::allMaterials
    double crossSectionArea{};    // m^2
  };

  struct MeshData {

    // truss (1_D element) deformation under constant applied force
    std::vector<FEM::TRUSS::Node> trussNodes;
    std::vector<RenderElement> trussElements;
    std::vector<FEM::TRUSS::ForceApplied> appliedForces;
    std::atomic<double> deformScale{1.0};
    bool hasResults{false}; // displacements / stresses come from a solve (or a result file)

    MeshData() = default;

    MeshData(const MeshData& other)
      : trussNodes(other.trussNodes),
       trussElements(other.trussElements),
       appliedForces(other.appliedForces),
       deformScale(other.deformScale.load()),
       hasResults(other.hasResults) {}

    MeshData& operator=(const MeshData& other) {
      if (this != &other) {
        trussNodes = other.trussNodes;
        trussElements = other.trussElements;
        appliedForces = other.appliedForces;
        deformScale.store(other.deformScale.load());
        hasResults = other.hasResults;
      }
      return *this;
    }

    MeshData(MeshData&& other) noexcept
      : trussNodes(std::move(other.trussNodes)),
       trussElements(std::move(other.trussElements)),
       appliedForces(std::move(other.appliedForces)),
       deformScale(other.deformScale.load()),
       hasResults(other.hasResults) {}

    MeshData& operator=(MeshData&& other) noexcept {
      if (this != &other) {
        trussNodes = std::move(other.trussNodes);
        trussElements = std::move(other.trussElements);
        appliedForces = std::move(other.appliedForces);
        deformScale.store(other.deformScale.load());
        hasResults = other.hasResults;
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

    // Elements refer to a material by its index in this vector (RenderElement::materialID,
    // TrussElement_1D::m_type). Built-ins come first in file order, user materials follow.
    // Material::getMaterialID() is a stable ID that is never reused; use it to keep a
    // selection across removals. Guarded by dataMutex.
    std::vector<anaf::MATERIAL::Material> allMaterials;

    FixedDOFMap fixedDOFsByNode;
    std::uint32_t selectedNodeId{std::numeric_limits<std::uint32_t>::max()};
    bool hasTrussPreview{false};

    // Loads the built-in materials from assets/bridge/materialProperties.json.
    // Call once at startup, after the log is initialized. Returns false on failure.
    bool setStaticInfo();

    // Loads the user materials saved in path and saves every later add / remove there.
    // Call after setStaticInfo(). Without this call user materials are session-only
    // (the tests rely on that to never touch the real user file).
    void loadUserMaterials(std::filesystem::path path);

    // Appends a user material (builtin flag and ID are assigned here) and returns its ID.
    // A failed save is logged; the material stays for this session.
    std::expected<std::uint32_t, std::string> addUserMaterial(const anaf::MATERIAL::Material& material);

    // Removes a user material. Refused for built-ins, while a worker runs, and while the
    // active mesh uses it; indices above it in the active mesh are shifted down.
    std::expected<void, std::string> removeUserMaterial(std::uint32_t materialID);

    // Index of the material with this ID in allMaterials. Caller holds dataMutex.
    std::optional<std::uint32_t> findMaterialIndex(std::uint32_t materialID) const;

  private:
    std::uint32_t m_nextMaterialID{0};
    std::filesystem::path m_userMaterialPath; // empty: user materials are not persisted

    void saveUserMaterials();
    std::uint32_t appendUserMaterialLocked(const anaf::MATERIAL::Material& material);
  };

  Gui_Calc_Bridge& buildBridge();

} // namespace anaf::BRIDGE end


