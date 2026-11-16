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

#include "materialHandler.hpp"
#include "propertyGrid.hpp"

#include <guiMaterials/theme.hpp>

#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>

#include "imgui.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace anaf::GUI {

  namespace {
    constexpr double giga = 1.0e9;
    constexpr double mega = 1.0e6;
  } // namespace end

  void MaterialHandler::setStatus(std::string message, const bool isError) {
    m_status = std::move(message);
    m_statusIsError = isError;
  }

  void MaterialHandler::onImGuiRender() {
    if (!isOpen) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center(
      viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
      viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
    );
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 700.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Material Handler", &isOpen, ImGuiWindowFlags_NoDocking)) {
      ImGui::End();
      return;
    }

    renderMaterialTable();
    ImGui::Spacing();
    renderAddForm();

    if (!m_status.empty()) {
      ImGui::Spacing();
      const ImVec4 color = m_statusIsError ? THEME::theme().bad : THEME::theme().good;
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(color, "%s", m_status.c_str());
      ImGui::PopTextWrapPos();
    }

    ImGui::End();
  }

  void MaterialHandler::renderMaterialTable() {
    BRIDGE::GuiCalcBridge& bridge = BRIDGE::buildBridge();

    ImGui::SeparatorText("Materials");

    constexpr ImGuiTableFlags tableFlags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_SizingFixedFit;
    const float tableHeight = ImGui::GetTextLineHeightWithSpacing() * 8.0f;

    // Removal locks dataMutex itself, so it runs after the table has released the lock.
    std::optional<std::uint32_t> pendingRemoval;
    {
      std::lock_guard lock(bridge.dataMutex);
      if (bridge.allMaterials.empty()) {
        ImGui::TextDisabled("No materials loaded (see the log for the material library error)");
      }
      else if (ImGui::BeginTable("MaterialsTable", 11, tableFlags, ImVec2(0.0f, tableHeight))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("E (GPa)");
        ImGui::TableSetupColumn("G (GPa)");
        ImGui::TableSetupColumn("K (GPa)");
        ImGui::TableSetupColumn("Yield (MPa)");
        ImGui::TableSetupColumn("Ultimate (MPa)");
        ImGui::TableSetupColumn("Density (kg/m\u00b3)");
        ImGui::TableSetupColumn("Poisson");
        ImGui::TableSetupColumn("Ductility (%)");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();

        for (const auto& material : bridge.allMaterials) {
          ImGui::PushID(static_cast<int>(material.getMaterialID()));
          ImGui::TableNextRow();

          ImGui::TableNextColumn();
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted(material.getMaterialType().data(), material.getMaterialType().data() + material.getMaterialType().size());
          ImGui::TableNextColumn();
          if (material.getIsBuiltin()) ImGui::TextDisabled("Built-in");
          else ImGui::TextUnformatted("User");
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", material.getElasticityModulus() / giga);
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", material.getShearModulus() / giga);
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", material.getBulkModulus() / giga);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", material.getYieldTensile() / mega);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", material.getUltTensile() / mega);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", material.getDensity());
          ImGui::TableNextColumn();
          ImGui::Text("%.3f", material.getPoisson());
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", material.getDuctility() * 100.0);

          ImGui::TableNextColumn();
          if (!material.getIsBuiltin() && ImGui::SmallButton("Delete")) {
            pendingRemoval = material.getMaterialID();
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }

    if (pendingRemoval) {
      if (const auto removed = bridge.removeUserMaterial(*pendingRemoval); removed) {
        setStatus("Material removed.", false);
        anaf::LOG::info("Material {} removed", *pendingRemoval);
      }
      else {
        setStatus("Cannot remove material: " + removed.error(), true);
        anaf::LOG::warn("Cannot remove material: {}", removed.error());
      }
    }
  }

  void MaterialHandler::renderAddForm() {
    BRIDGE::GuiCalcBridge& bridge = BRIDGE::buildBridge();

    ImGui::SeparatorText("Add User Material");
    ImGui::TextDisabled("User materials are saved in your user settings folder. The solver uses E for the axial stiffness.");

    if (PROPERTY::begin("AddMaterialForm", 0.55f)) {
      if (PROPERTY::group("General")) {
        PROPERTY::inputText("Name", m_draft.name.data(), m_draft.name.size(), "Unique name; mesh files refer to the material by it.");
      }
      if (PROPERTY::group("Elastic")) {
        PROPERTY::inputDouble("Elasticity modulus E", &m_draft.elasticityModulusGPa, "GPa", "%.3f");
        PROPERTY::inputDouble("Shear modulus G", &m_draft.shearModulusGPa, "GPa", "%.3f");
        PROPERTY::inputDouble("Bulk modulus K", &m_draft.bulkModulusGPa, "GPa", "%.3f");
        PROPERTY::inputDouble("Poisson's ratio", &m_draft.poissonsRatio, nullptr, "%.3f");
      }
      if (PROPERTY::group("Strength")) {
        PROPERTY::inputDouble("Yield tensile strength", &m_draft.yieldStrengthMPa, "MPa", "%.2f");
        PROPERTY::inputDouble("Ultimate tensile strength", &m_draft.ultimateStrengthMPa, "MPa", "%.2f");
        PROPERTY::inputDouble("Ductility", &m_draft.ductilityPercent, "%", "%.1f", "Elongation at break.");
      }
      if (PROPERTY::group("Physical")) {
        PROPERTY::inputDouble("Density", &m_draft.density, "kg/m\u00b3", "%.2f");
      }
      PROPERTY::end();
    }

    if (ImGui::Button("Add Material", ImVec2(-1.0f, 0.0f))) {
      std::string name(m_draft.name.data());
      name.erase(0, name.find_first_not_of(" \t"));
      name.erase(name.find_last_not_of(" \t") + 1);

      // Built-in flag and ID are assigned by the bridge.
      const anaf::MATERIAL::Material material{anaf::MATERIAL::MaterialProperties{
        .name = std::move(name),
        .elasticityModulus = m_draft.elasticityModulusGPa * giga,
        .shearModulus = m_draft.shearModulusGPa * giga,
        .bulkModulus = m_draft.bulkModulusGPa * giga,
        .yieldTensileStrength = m_draft.yieldStrengthMPa * mega,
        .ultimateTensileStrength = m_draft.ultimateStrengthMPa * mega,
        .density = m_draft.density,
        .poissonsRatio = m_draft.poissonsRatio,
        .ductility = m_draft.ductilityPercent / 100.0,
      }};

      if (const auto added = bridge.addUserMaterial(material); added) {
        setStatus("Material '" + std::string(material.getMaterialType()) + "' added.", false);
        anaf::LOG::info("User material '{}' added (ID {})", material.getMaterialType(), *added);
        m_draft = Draft{};
      }
      else {
        setStatus("Cannot add material: " + added.error(), true);
      }
    }
  }

} // namespace anaf::GUI end
