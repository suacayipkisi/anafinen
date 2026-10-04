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

#include <beam/beamProperties/meshData.hpp>
#include <beam/beamSection/beamSection.hpp>
#include <material/properties.hpp>
#include <truss_1D/trussProperties/meshData.hpp>

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace anaf::BRIDGE {
  enum ObjectType {
    truss_SQPT,
    truss_imported_or_entered,
    beam_frame,
    no_type
  };

  std::string_view getObjectTypeName(ObjectType obj);

  // The model types live in the FEM core (anaf_core), so a CLI can use them without the GUI bridge.
  using RenderElement = FEM::TRUSS::RenderElement;
  using MeshData = FEM::TRUSS::MeshData;
  using BeamMeshData = FEM::BEAM::MeshData;

  struct Gui_Calc_Bridge {
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_isGeneratingPreview{false};
    std::atomic<float> m_progress{0.0f};
    std::atomic<std::uint64_t> dataVersion{0};
    std::mutex dataMutex;
    std::jthread workerThread;

    std::atomic<ObjectType> m_objectType{no_type};
    // Bumped by resetModel(). A worker takes it before it starts and publishes its snapshot
    // only if it is unchanged (checked under dataMutex), so a solve or preview that was still
    // running when the model was reset never brings the old model back.
    std::atomic<std::uint64_t> modelGeneration{0};
    std::shared_ptr<const MeshData> activeMesh{nullptr};
    // The beam / frame model (object type beam_frame), published like activeMesh: immutable
    // snapshots swapped under dataMutex, dataVersion bumped. Only one of the two is set.
    std::shared_ptr<const BeamMeshData> activeBeamMesh{nullptr};
    std::atomic<bool> m_isValid{false};
    std::atomic<double> m_energyDiff{0.0};
    // View setting, not part of the model: the viewport draws location + displacement * deformScale.
    // Written by the truss panels (then dataVersion is bumped so the scene is rebuilt).
    std::atomic<double> deformScale{1.0};

    // Elements refer to a material by its index in this vector (RenderElement::materialID,
    // TrussElement_1D::m_type). Built-ins come first in file order, user materials follow.
    // Material::getMaterialID() is a stable ID that is never reused; use it to keep a
    // selection across removals. Guarded by dataMutex.
    std::vector<anaf::MATERIAL::Material> allMaterials;

    // Beam sections, the same scheme as allMaterials: BeamElement::sectionID is an index into
    // this vector; the catalogue comes first, user sections follow; BeamSection::getSectionID()
    // is a stable ID. Guarded by dataMutex.
    std::vector<FEM::BEAM::BeamSection> allSections;

    std::uint32_t selectedNodeId{std::numeric_limits<std::uint32_t>::max()};
    // Beam element shared by the beam editor and the diagram panel. Guarded by dataMutex.
    std::uint32_t selectedElementId{std::numeric_limits<std::uint32_t>::max()};

    // Drops the whole model (snapshot with its supports and loads, selection, solve status)
    // and switches to type.
    // A running worker is asked to stop and can no longer publish (see modelGeneration).
    // Call from the GUI thread; panels reset their own inputs separately.
    void resetModel(ObjectType type);

    // Stops and joins the previous worker. Call before setting m_isRunning /
    // m_isGeneratingPreview for a new job: a worker that is still finishing clears them on exit.
    void joinWorker();

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

    // Loads the section catalogue (assets/bridge/sectionCatalog.json). Returns false on failure.
    bool loadSectionCatalog();
    // Loads the user sections saved in path and saves every later add / remove there.
    void loadUserSections(std::filesystem::path path);
    // Appends a user section (validated, unique name) and returns its ID.
    std::expected<std::uint32_t, std::string> addUserSection(const FEM::BEAM::BeamSection& section);
    // Removes a user section. Refused for catalogue sections, while a worker runs and while the
    // active beam model uses it; indices above it in the beam model are shifted down.
    std::expected<void, std::string> removeUserSection(std::uint32_t sectionID);
    // Index of the section with this ID in allSections. Caller holds dataMutex.
    std::optional<std::uint32_t> findSectionIndex(std::uint32_t sectionID) const;

  private:
    std::uint32_t m_nextMaterialID{0};
    std::filesystem::path m_userMaterialPath; // empty: user materials are not persisted
    std::uint32_t m_nextSectionID{0};
    std::filesystem::path m_userSectionPath;  // empty: user sections are not persisted

    void saveUserMaterials();
    std::uint32_t appendUserMaterialLocked(const anaf::MATERIAL::Material& material);
    void saveUserSections();
    std::uint32_t appendUserSectionLocked(const FEM::BEAM::BeamSection& section);
  };

  Gui_Calc_Bridge& buildBridge();

} // namespace anaf::BRIDGE end


