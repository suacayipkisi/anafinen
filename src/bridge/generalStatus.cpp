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

#include "generalStatus.hpp"
#include <directory/getExecutableDirectory.hpp>
#include <io/core/pathUtf8.hpp>
#include <log/anaf_info.hpp>
#include <material/materialLibrary.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <string_view>

namespace anaf::BRIDGE {

  std::string_view getObjectTypeName(ObjectType obj) {
    switch (obj) {
      case truss_SQPT:
        return "truss_SQPT";
      case truss_imported_or_entered:
        return "truss_imported_or_entered";
      default:
        return "no_type";
    }
  }

  void Gui_Calc_Bridge::resetModel(const ObjectType type) {
    modelGeneration.fetch_add(1, std::memory_order_acq_rel);
    if (workerThread.joinable()) workerThread.request_stop();
    m_isRunning = false;
    m_isGeneratingPreview = false;
    m_progress = 0.0f;
    {
      std::lock_guard lock(dataMutex);
      activeMesh = nullptr;
      hasTrussPreview = false;
      selectedNodeId = std::numeric_limits<std::uint32_t>::max();
      m_isValid = false;
      m_energyDiff = 0.0;
      m_objectType = type;
    }
    dataVersion.fetch_add(1, std::memory_order_release);
    anaf::LOG::info("Model reset, object type: {}", getObjectTypeName(type));
  }

  void Gui_Calc_Bridge::joinWorker() {
    if (!workerThread.joinable()) return;
    workerThread.request_stop();
    workerThread.join();
  }

  bool Gui_Calc_Bridge::setStaticInfo() {
    const std::filesystem::path subpath = std::filesystem::path("bridge") / "materialProperties.json";
    const std::filesystem::path path = anaf::DIRECTORY::findAssetPath(subpath);
    if (path.empty()) {
      anaf::LOG::error("Material library not found: assets/{}", subpath.generic_string());
      return false;
    }

    auto loaded = anaf::MATERIAL::loadMaterialLibrary(path);
    if (!loaded) {
      anaf::LOG::error("Material library not loaded: {}", loaded.error());
      return false;
    }

    std::lock_guard lock(dataMutex);
    allMaterials = std::move(*loaded);
    m_nextMaterialID = static_cast<std::uint32_t>(allMaterials.size()); // IDs are 0..n-1
    anaf::LOG::info("Loaded {} built-in materials from {}", allMaterials.size(), anaf::IO::pathToUtf8(path));
    return true;
  }

  void Gui_Calc_Bridge::loadUserMaterials(std::filesystem::path path) {
    if (path.empty()) {
      anaf::LOG::warn("No user config directory; user materials will not be saved");
      return;
    }
    m_userMaterialPath = std::move(path);

    auto loaded = anaf::MATERIAL::loadUserMaterialFile(m_userMaterialPath);
    if (!loaded) {
      // Keep the unreadable file for the user instead of overwriting it on the next save.
      auto backup = m_userMaterialPath;
      backup += ".corrupt";
      std::error_code ec;
      std::filesystem::rename(m_userMaterialPath, backup, ec);
      anaf::LOG::error("User materials not loaded: {}. The file was moved to {}", loaded.error(),
                       ec ? std::string("(move failed: ") + ec.message() + ")" : anaf::IO::pathToUtf8(backup));
      return;
    }

    std::size_t added = 0;
    {
      std::lock_guard lock(dataMutex);
      for (const auto& material : *loaded) {
        const auto sameName = [&](const anaf::MATERIAL::Material& other) {
          return anaf::MATERIAL::sameMaterialName(other.getMaterialType(), material.getMaterialType());
        };
        if (std::ranges::any_of(allMaterials, sameName)) {
          anaf::LOG::warn("User material '{}' skipped: a material with that name already exists", material.getMaterialType());
          continue;
        }
        appendUserMaterialLocked(material);
        ++added;
      }
    }
    if (added > 0) anaf::LOG::info("Loaded {} user materials from {}", added, anaf::IO::pathToUtf8(m_userMaterialPath));
  }

  std::uint32_t Gui_Calc_Bridge::appendUserMaterialLocked(const anaf::MATERIAL::Material& material) {
    const std::uint32_t id = m_nextMaterialID++;
    allMaterials.emplace_back(
      false,
      std::string(material.getMaterialType()),
      material.getElasticityModulus(),
      material.getShearModulus(),
      material.getBulkModulus(),
      material.getYieldTensile(),
      material.getUltTensile(),
      material.getYoungModulus(),
      material.getDensity(),
      material.getPoisson(),
      material.getDuctility(),
      id
    );
    return id;
  }

  void Gui_Calc_Bridge::saveUserMaterials() {
    if (m_userMaterialPath.empty()) return;

    std::vector<anaf::MATERIAL::Material> snapshot;
    {
      std::lock_guard lock(dataMutex);
      snapshot = allMaterials;
    }
    if (const auto saved = anaf::MATERIAL::saveUserMaterialFile(m_userMaterialPath, snapshot); !saved) {
      anaf::LOG::warn("User materials not saved (kept for this session): {}", saved.error());
    }
  }

  std::expected<std::uint32_t, std::string> Gui_Calc_Bridge::addUserMaterial(const anaf::MATERIAL::Material& material) {
    if (const auto valid = anaf::MATERIAL::validateMaterial(material); !valid) {
      return std::unexpected(valid.error());
    }

    std::uint32_t id{};
    {
      std::lock_guard lock(dataMutex);
      const auto sameName = [&](const anaf::MATERIAL::Material& other) {
        return anaf::MATERIAL::sameMaterialName(other.getMaterialType(), material.getMaterialType());
      };
      if (std::ranges::any_of(allMaterials, sameName)) {
        return std::unexpected(std::format("a material named '{}' already exists", material.getMaterialType()));
      }
      id = appendUserMaterialLocked(material);
    }
    saveUserMaterials();
    return id;
  }

  std::expected<void, std::string> Gui_Calc_Bridge::removeUserMaterial(const std::uint32_t materialID) {
    bool meshShifted = false;
    {
      std::lock_guard lock(dataMutex);
      const auto index = findMaterialIndex(materialID);
      if (!index) return std::unexpected(std::format("no material with ID {}", materialID));

      const auto& material = allMaterials[*index];
      if (material.getIsBuiltin()) {
        return std::unexpected(std::format("'{}' is a built-in material", material.getMaterialType()));
      }
      // A running worker publishes element indices taken from its own copy of the list.
      if (m_isRunning.load() || m_isGeneratingPreview.load()) {
        return std::unexpected("wait until the running solve / preview has finished");
      }

      const bool usedByMesh = activeMesh && std::ranges::any_of(
        activeMesh->trussElements, [i = *index](const RenderElement& element) { return element.materialID == i; });
      if (usedByMesh) {
        return std::unexpected(std::format("'{}' is used by the current model", material.getMaterialType()));
      }

      allMaterials.erase(allMaterials.begin() + *index);

      // Keep the active mesh pointing at the same materials after the shift.
      meshShifted = activeMesh && std::ranges::any_of(
        activeMesh->trussElements, [i = *index](const RenderElement& element) { return element.materialID > i; });
      if (meshShifted) {
        auto shifted = std::make_shared<MeshData>(*activeMesh);
        for (auto& element : shifted->trussElements) {
          if (element.materialID > *index) --element.materialID;
        }
        activeMesh = std::move(shifted);
      }
    }
    if (meshShifted) dataVersion.fetch_add(1, std::memory_order_release);
    saveUserMaterials();
    return {};
  }

  std::optional<std::uint32_t> Gui_Calc_Bridge::findMaterialIndex(const std::uint32_t materialID) const {
    const auto it = std::ranges::find(allMaterials, materialID, &anaf::MATERIAL::Material::getMaterialID);
    if (it == allMaterials.end()) return std::nullopt;
    return static_cast<std::uint32_t>(it - allMaterials.begin());
  }

  Gui_Calc_Bridge& buildBridge() {
    static Gui_Calc_Bridge bridge{};
    return bridge;
  }

} // namespace anaf::BRIDGE end
