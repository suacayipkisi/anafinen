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

#include <beam/beamProperties/meshEdit.hpp>
#include <bridge/generalStatus.hpp>
#include <truss_1D/trussProperties/meshEdit.hpp>

#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace anaf::CLI {

  // Values the element commands use when a command does not name them (-set changes them).
  // Materials and sections are kept by their stable ID, so removing another entry does not
  // move the default to a different one.
  struct Defaults {
    std::uint32_t materialID{0};
    std::uint32_t sectionID{0};
    double area{8e-3}; // m^2, truss bars (80 cm^2, the truss editor's default)
    FEM::BEAM::E_Formulation formulation{FEM::BEAM::E_Formulation::EulerBernoulli};
  };

  // State of one CLI run. The model itself lives in the bridge (activeMesh / activeBeamMesh),
  // published the same way as by the GUI editors, so every front end sees the same snapshot.
  struct Session {
    BRIDGE::GuiCalcBridge& bridge;
    std::ostream& out; // command output
    std::ostream& err; // error messages
    Defaults defaults{};
    bool exitRequested{false};
    int scriptDepth{0}; // nesting of -run
    // bridge.dataVersion right after -solve published its result: the energy check in the
    // bridge belongs to the snapshot only while the version is unchanged (an imported solved
    // file has results but no check).
    std::uint64_t solvedVersion{std::numeric_limits<std::uint64_t>::max()};

    Session(BRIDGE::GuiCalcBridge& sessionBridge, std::ostream& outStream, std::ostream& errStream);
  };

  using CommandResult = std::expected<void, std::string>;

  enum class E_ModelKind { None, Truss, Beam };

  // The kind of the active model, from the bridge's object type.
  E_ModelKind modelKind(const Session& session);

  std::shared_ptr<const BRIDGE::MeshData> trussMesh(Session& session);
  std::shared_ptr<const BRIDGE::BeamMeshData> beamMesh(Session& session);

  // Index of a material in bridge.allMaterials: "#<index>" or a name (ASCII case ignored).
  std::expected<std::uint32_t, std::string> findMaterial(Session& session, std::string_view token);
  // Index of a section in bridge.allSections, same rules.
  std::expected<std::uint32_t, std::string> findSection(Session& session, std::string_view token);
  // Index of the default material / section (the first entry when the default was removed).
  std::uint32_t defaultMaterialIndex(Session& session);
  std::uint32_t defaultSectionIndex(Session& session);

  // Runs edit on a copy of the active truss snapshot under dataMutex and publishes the copy
  // with its results dropped (they no longer match the model), like the GUI editors. Fails
  // when the active model is not a truss or when edit fails (nothing is published then).
  template <typename Edit>
  CommandResult editTruss(Session& session, Edit&& edit) {
    if (modelKind(session) != E_ModelKind::Truss) return std::unexpected("no truss model: start one with -new truss");
    auto& bridge = session.bridge;
    if (bridge.isRunning) return std::unexpected("a solve is running");
    {
      std::lock_guard lock(bridge.dataMutex);
      auto mesh = bridge.activeMesh ? std::make_shared<BRIDGE::MeshData>(*bridge.activeMesh) : std::make_shared<BRIDGE::MeshData>();
      if (CommandResult edited = edit(*mesh); !edited) return edited;
      FEM::TRUSS::dropResults(*mesh);
      bridge.activeMesh = std::move(mesh);
      bridge.isValid = false;
    }
    bridge.dataVersion.fetch_add(1, std::memory_order_release);
    return {};
  }

  // The same for the beam / frame model.
  template <typename Edit>
  CommandResult editBeam(Session& session, Edit&& edit) {
    if (modelKind(session) != E_ModelKind::Beam) return std::unexpected("no beam model: start one with -new beam");
    auto& bridge = session.bridge;
    if (bridge.isRunning) return std::unexpected("a solve is running");
    {
      std::lock_guard lock(bridge.dataMutex);
      auto mesh = bridge.activeBeamMesh ? std::make_shared<BRIDGE::BeamMeshData>(*bridge.activeBeamMesh)
                                        : std::make_shared<BRIDGE::BeamMeshData>();
      if (CommandResult edited = edit(*mesh); !edited) return edited;
      FEM::BEAM::dropResults(*mesh);
      bridge.activeBeamMesh = std::move(mesh);
      bridge.isValid = false;
    }
    bridge.dataVersion.fetch_add(1, std::memory_order_release);
    return {};
  }

} // namespace anaf::CLI end
