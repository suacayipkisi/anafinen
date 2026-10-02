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

#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>

#include "imgui.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace anaf::GUI {

  namespace {
    constexpr double kGiga = 1.0e9;
    constexpr double kMega = 1.0e6;

    void labeledInput(const char* label, const char* id, double* value, const char* format) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(label);
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputDouble(id, value, 0.0, 0.0, format);
    }

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
      const ImVec4 color = m_statusIsError ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.85f, 0.5f, 1.0f);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(color, "%s", m_status.c_str());
      ImGui::PopTextWrapPos();
    }

    ImGui::End();
  }

  void MaterialHandler::renderMaterialTable() {
    BRIDGE::Gui_Calc_Bridge& bridge = BRIDGE::buildBridge();

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
        ImGui::TableSetupColumn("Density (kg/m3)");
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
          ImGui::Text("%.2f", material.getElasticityModulus() / kGiga);
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", material.getShearModulus() / kGiga);
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", material.getBulkModulus() / kGiga);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", material.getYieldTensile() / kMega);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", material.getUltTensile() / kMega);
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
    BRIDGE::Gui_Calc_Bridge& bridge = BRIDGE::buildBridge();

    ImGui::SeparatorText("Add User Material");
    ImGui::TextDisabled("User materials are saved in your user settings folder. The solver uses E for the axial stiffness.");

    if (ImGui::BeginTable("AddMaterialForm", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.45f);
      ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.55f);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Name");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##material_name", m_draft.name.data(), m_draft.name.size());

      labeledInput("Elasticity Modulus E (GPa)", "##material_e", &m_draft.elasticityModulusGPa, "%.3f");
      labeledInput("Shear Modulus G (GPa)", "##material_g", &m_draft.shearModulusGPa, "%.3f");
      labeledInput("Bulk Modulus K (GPa)", "##material_k", &m_draft.bulkModulusGPa, "%.3f");
      labeledInput("Yield Tensile Strength (MPa)", "##material_yield", &m_draft.yieldStrengthMPa, "%.2f");
      labeledInput("Ultimate Tensile Strength (MPa)", "##material_uts", &m_draft.ultimateStrengthMPa, "%.2f");
      labeledInput("Density (kg/m3)", "##material_density", &m_draft.density, "%.2f");
      labeledInput("Poisson's Ratio", "##material_poisson", &m_draft.poissonsRatio, "%.3f");
      labeledInput("Ductility, elongation at break (%)", "##material_ductility", &m_draft.ductilityPercent, "%.1f");

      ImGui::EndTable();
    }

    if (ImGui::Button("Add Material", ImVec2(-1.0f, 0.0f))) {
      std::string name(m_draft.name.data());
      name.erase(0, name.find_first_not_of(" \t"));
      name.erase(name.find_last_not_of(" \t") + 1);

      // Built-in flag and ID are assigned by the bridge.
      const anaf::MATERIAL::Material material{anaf::MATERIAL::MaterialProperties{
        .name = std::move(name),
        .elasticityModulus = m_draft.elasticityModulusGPa * kGiga,
        .shearModulus = m_draft.shearModulusGPa * kGiga,
        .bulkModulus = m_draft.bulkModulusGPa * kGiga,
        .yieldTensileStrength = m_draft.yieldStrengthMPa * kMega,
        .ultimateTensileStrength = m_draft.ultimateStrengthMPa * kMega,
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
