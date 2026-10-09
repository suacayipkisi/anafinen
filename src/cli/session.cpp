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

#include "session.hpp"

#include <beam/beamSection/sectionLibrary.hpp>
#include <material/materialLibrary.hpp>

#include "parsing/arguments.hpp"

#include <format>

namespace anaf::CLI {

  Session::Session(BRIDGE::Gui_Calc_Bridge& bridge_, std::ostream& out_, std::ostream& err_) :
    bridge(bridge_),
    out(out_),
    err(err_)
  {
    std::lock_guard lock(bridge.dataMutex);
    if (!bridge.allMaterials.empty()) defaults.materialID = bridge.allMaterials.front().getMaterialID();
    if (!bridge.allSections.empty()) defaults.sectionID = bridge.allSections.front().getSectionID();
    // The frame editor's girder section when the catalogue has it.
    for (const auto& section : bridge.allSections) {
      if (!FEM::BEAM::sameSectionName(section.getName(), "IPE 300")) continue;
      defaults.sectionID = section.getSectionID();
      break;
    }
  }

  ModelKind modelKind(const Session& session) {
    switch (session.bridge.m_objectType.load()) {
      case BRIDGE::truss_SQPT:
      case BRIDGE::truss_imported_or_entered:
        return ModelKind::truss;
      case BRIDGE::beam_frame:
        return ModelKind::beam;
      default:
        return ModelKind::none;
    }
  }

  std::shared_ptr<const BRIDGE::MeshData> trussMesh(Session& session) {
    std::lock_guard lock(session.bridge.dataMutex);
    return session.bridge.activeMesh;
  }

  std::shared_ptr<const BRIDGE::BeamMeshData> beamMesh(Session& session) {
    std::lock_guard lock(session.bridge.dataMutex);
    return session.bridge.activeBeamMesh;
  }

  namespace {

    // "#<index>" or a name (nameOf(entry)); sameName compares names.
    template <typename List, typename NameOf, typename SameName>
    std::expected<std::uint32_t, std::string> findInList(const List& list, const std::string_view token, const std::string_view what,
                                                         NameOf&& nameOf, SameName&& sameName) {
      if (token.starts_with('#')) {
        const auto index = parseIndex(token.substr(1), what);
        if (!index) return std::unexpected(index.error());
        if (*index >= list.size()) return std::unexpected(std::format("{} #{} does not exist ({} in the list)", what, *index, list.size()));
        return *index;
      }
      for (std::uint32_t i = 0; i < list.size(); ++i) {
        if (sameName(nameOf(list[i]), token)) return i;
      }
      return std::unexpected(std::format("unknown {} '{}' (list them with -{}s)", what, token, what));
    }

  } // namespace end

  std::expected<std::uint32_t, std::string> findMaterial(Session& session, const std::string_view token) {
    std::lock_guard lock(session.bridge.dataMutex);
    return findInList(
      session.bridge.allMaterials, token, "material",
      [](const MATERIAL::Material& material) { return material.getMaterialType(); },
      [](const std::string_view a, const std::string_view b) { return MATERIAL::sameMaterialName(a, b); });
  }

  std::expected<std::uint32_t, std::string> findSection(Session& session, const std::string_view token) {
    std::lock_guard lock(session.bridge.dataMutex);
    return findInList(
      session.bridge.allSections, token, "section",
      [](const FEM::BEAM::BeamSection& section) { return std::string_view(section.getName()); },
      [](const std::string_view a, const std::string_view b) { return FEM::BEAM::sameSectionName(a, b); });
  }

  std::uint32_t defaultMaterialIndex(Session& session) {
    std::lock_guard lock(session.bridge.dataMutex);
    return session.bridge.findMaterialIndex(session.defaults.materialID).value_or(0);
  }

  std::uint32_t defaultSectionIndex(Session& session) {
    std::lock_guard lock(session.bridge.dataMutex);
    return session.bridge.findSectionIndex(session.defaults.sectionID).value_or(0);
  }

} // namespace anaf::CLI end
