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

#include "sectionHandler.hpp"

#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>
#include <panels/propertyGrid.hpp>

#include <beam/beamSection/beamSection.hpp>
#include <bridge/generalStatus.hpp>

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace anaf::GUI {

  namespace {

    constexpr double mmToM = 1e-3;
    constexpr double cm2ToM2 = 1e-4;
    constexpr double cm4ToM4 = 1e-8;
    constexpr double poisson = 0.3; // shown shear areas only; the solve uses each element's material
    constexpr std::array<const char*, 6> shapeLabels{"General (values)", "Rectangle", "Circle", "Pipe (CHS)", "Box (RHS / SHS)", "I / H section"};
    constexpr std::array<const char*, 6> shapeNames{"general", "rectangle", "circle", "pipe", "box", "I"};
    constexpr std::array<ICONS::E_IconId, 6> shapeIcons{ICONS::E_IconId::SectionGeneral, ICONS::E_IconId::SectionRectangle,
                                                        ICONS::E_IconId::SectionCircle,  ICONS::E_IconId::SectionPipe,
                                                        ICONS::E_IconId::SectionBox,     ICONS::E_IconId::SectionIShape};

    // Case-insensitive substring test without copies (it runs for every section every frame).
    bool contains(const std::string_view text, const std::string_view part) {
      if (part.empty()) return true;
      const auto sameLetter = [](const unsigned char x, const unsigned char y) { return std::tolower(x) == std::tolower(y); };
      return !std::ranges::search(text, part, sameLetter).empty();
    }

    // Outline in a canvas of the given size: local z to the right, local y up, i.e. looking
    // from node 1 towards node 2 (z x y points back at the viewer = -x).
    void drawSection(const char* id, const FEM::BEAM::SectionShape& shape, const ImVec2 size) {
      ImGui::BeginChild(id, size, true);
      const auto loops = FEM::BEAM::sectionOutline(shape, 12);
      if (loops.empty()) {
        ImGui::TextDisabled("No shape: the properties are entered directly.");
        ImGui::EndChild();
        return;
      }
      double maxY = 0.0, maxZ = 0.0;
      for (const auto& loop : loops) {
        for (const auto& p : loop) {
          maxY = std::max(maxY, std::abs(p[0]));
          maxZ = std::max(maxZ, std::abs(p[1]));
        }
      }
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float margin = 22.0f;
      const ImVec2 centre(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);
      const double scale = std::min((avail.x * 0.5 - margin) / std::max(maxZ, 1e-9), (avail.y * 0.5 - margin) / std::max(maxY, 1e-9));
      const auto toScreen = [&](const std::array<double, 2>& p) {
        return ImVec2(centre.x + static_cast<float>(p[1] * scale), centre.y - static_cast<float>(p[0] * scale));
      };

      ImDrawList* draw = ImGui::GetWindowDrawList();
      const ImU32 axisColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      draw->AddLine(ImVec2(origin.x + 4.0f, centre.y), ImVec2(origin.x + avail.x - 4.0f, centre.y), axisColor);
      draw->AddLine(ImVec2(centre.x, origin.y + 4.0f), ImVec2(centre.x, origin.y + avail.y - 4.0f), axisColor);
      draw->AddText(ImVec2(origin.x + avail.x - 16.0f, centre.y - 18.0f), axisColor, "z");
      draw->AddText(ImVec2(centre.x + 5.0f, origin.y + 2.0f), axisColor, "y");
      const ImU32 lineColor = THEME::toU32(THEME::theme().accent);
      for (const auto& loop : loops) {
        std::vector<ImVec2> points;
        points.reserve(loop.size());
        for (const auto& p : loop) points.push_back(toScreen(p));
        draw->AddPolyline(points.data(), static_cast<int>(points.size()), lineColor, ImDrawFlags_Closed, 2.0f);
      }
      ImGui::Dummy(avail);
      ImGui::SetItemTooltip("Local z to the right, local y up: seen from node 1 towards node 2.");
      ImGui::EndChild();
    }

    // Computed properties of a shape as read-only property grid rows.
    void propertyRows(const char* id, const FEM::BEAM::SectionShape& shape) {
      const auto p = FEM::BEAM::computeProperties(shape, poisson);
      if (!PROPERTY::begin(id, 0.55f)) return;
      if (PROPERTY::group("Properties")) {
        constexpr const char* shearNote = "For \u03bd = 0.3; the solve uses each material's \u03bd.";
        PROPERTY::value("Area A", std::format("{:.4g}", p.area / cm2ToM2), "cm\u00b2");
        PROPERTY::value("Second moment Iy", std::format("{:.5g}", p.secondMomentY / cm4ToM4), "cm\u2074", "About local y.");
        PROPERTY::value("Second moment Iz", std::format("{:.5g}", p.secondMomentZ / cm4ToM4), "cm\u2074", "About local z.");
        PROPERTY::value("Torsion constant J", std::format("{:.4g}", p.torsionConstant / cm4ToM4), "cm\u2074");
        PROPERTY::value("Shear area Asy", std::format("{:.4g}", p.shearAreaY / cm2ToM2), "cm\u00b2", shearNote);
        PROPERTY::value("Shear area Asz", std::format("{:.4g}", p.shearAreaZ / cm2ToM2), "cm\u00b2", shearNote);
      }
      PROPERTY::end();
    }

    std::optional<FEM::BEAM::SectionShape> draftShape(const int shape, const double height, const double width, const double diameter,
                                           const double thickness, const double flangeThickness, const double outerRadius,
                                           const double innerRadius, const FEM::BEAM::SectionProperties& general) {
      switch (shape) {
        case 0: return FEM::BEAM::GeneralSection{general};
        case 1: return FEM::BEAM::RectangleSection{height * mmToM, width * mmToM};
        case 2: return FEM::BEAM::CircleSection{diameter * mmToM};
        case 3: return FEM::BEAM::PipeSection{diameter * mmToM, thickness * mmToM};
        case 4: return FEM::BEAM::BoxSection{height * mmToM, width * mmToM, thickness * mmToM, outerRadius * mmToM, innerRadius * mmToM};
        case 5: return FEM::BEAM::ISection{height * mmToM, width * mmToM, thickness * mmToM, flangeThickness * mmToM, outerRadius * mmToM};
        default: return std::nullopt;
      }
    }
  } // namespace end

  void SectionHandler::setStatus(std::string message, const bool isError) {
    m_status = std::move(message);
    m_statusIsError = isError;
  }

  void SectionHandler::onImGuiRender() {
    if (!isOpen) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 860.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Section Handler", &isOpen, ImGuiWindowFlags_NoDocking)) {
      ImGui::End();
      return;
    }
    renderTable();
    ImGui::Spacing();
    if (ImGui::BeginTable("SectionHandlerLayout", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableNextColumn();
      renderSelected();
      ImGui::TableNextColumn();
      renderAddForm();
      ImGui::EndTable();
    }
    if (!m_status.empty()) {
      const ImVec4 color = m_statusIsError ? THEME::theme().bad : THEME::theme().good;
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(color, "%s", m_status.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::End();
  }

  void SectionHandler::renderTable() {
    auto& bridge = BRIDGE::buildBridge();
    ImGui::SeparatorText("Sections");
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##section_filter", "Filter (e.g. IPE, HEB 2, SHS)", m_filter.data(), m_filter.size());

    std::optional<std::uint32_t> pendingRemoval;
    {
      std::lock_guard lock(bridge.dataMutex);
      if (bridge.allSections.empty()) {
        ImGui::TextDisabled("No sections loaded (see the log for the section catalogue error)");
        return;
      }
      std::vector<std::size_t> rows;
      for (std::size_t i = 0; i < bridge.allSections.size(); ++i) {
        if (contains(bridge.allSections[i].getName(), m_filter.data())) rows.push_back(i);
      }
      constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
      if (ImGui::BeginTable("SectionsTable", 8, flags, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 7.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Shape");
        ImGui::TableSetupColumn("A (cm\u00b2)");
        ImGui::TableSetupColumn("Iy (cm\u2074)");
        ImGui::TableSetupColumn("Iz (cm\u2074)");
        ImGui::TableSetupColumn("J (cm\u2074)");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
          for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& section = bridge.allSections[rows[static_cast<std::size_t>(row)]];
            const auto p = FEM::BEAM::computeProperties(section.getShape(), poisson);
            ImGui::PushID(static_cast<int>(section.getSectionID()));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(section.getName().c_str(), m_selectedID == section.getSectionID(), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
              m_selectedID = section.getSectionID();
            }
            ImGui::TableNextColumn();
            if (section.getIsBuiltin()) ImGui::TextDisabled("Catalogue");
            else ImGui::TextUnformatted("User");
            ImGui::TableNextColumn();
            ICONS::iconText(shapeIcons[section.getShape().index()], shapeNames[section.getShape().index()]);
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", p.area / cm2ToM2);
            ImGui::TableNextColumn();
            ImGui::Text("%.5g", p.secondMomentY / cm4ToM4);
            ImGui::TableNextColumn();
            ImGui::Text("%.5g", p.secondMomentZ / cm4ToM4);
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", p.torsionConstant / cm4ToM4);
            ImGui::TableNextColumn();
            if (!section.getIsBuiltin() && ImGui::SmallButton("Remove")) pendingRemoval = section.getSectionID();
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }
    }
    // removeUserSection locks dataMutex itself.
    if (pendingRemoval) {
      if (const auto removed = bridge.removeUserSection(*pendingRemoval); removed) setStatus("Section removed", false);
      else setStatus("Section not removed: " + removed.error(), true);
    }
  }

  void SectionHandler::renderSelected() {
    auto& bridge = BRIDGE::buildBridge();
    std::optional<FEM::BEAM::BeamSection> selected;
    {
      std::lock_guard lock(bridge.dataMutex);
      if (const auto index = bridge.findSectionIndex(m_selectedID)) selected = bridge.allSections[*index];
    }
    ImGui::SeparatorText("Selected");
    if (!selected) {
      ImGui::TextDisabled("Click a section in the table.");
      return;
    }
    ICONS::iconText(shapeIcons[selected->getShape().index()], selected->getName().c_str());
    drawSection("SelectedSectionCanvas", selected->getShape(), ImVec2(-FLT_MIN, 180.0f));
    propertyRows("SelectedSectionProperties", selected->getShape());
  }

  void SectionHandler::renderAddForm() {
    auto& bridge = BRIDGE::buildBridge();
    ImGui::SeparatorText("New Section");
    auto& d = m_draft;
    if (PROPERTY::begin("NewSectionForm", 0.55f)) {
      if (PROPERTY::group("General")) {
        PROPERTY::inputText("Name", d.name.data(), d.name.size(), "Unique name; mesh files refer to the section by it.");
        PROPERTY::row("Shape");
        if (ImGui::BeginCombo("##section_shape", shapeLabels[static_cast<std::size_t>(d.shape)])) {
          for (std::size_t i = 0; i < shapeLabels.size(); ++i) {
            if (ICONS::iconSelectable(shapeIcons[i], shapeLabels[i], d.shape == static_cast<int>(i))) d.shape = static_cast<int>(i);
          }
          ImGui::EndCombo();
        }
      }
      if (d.shape == 0) {
        if (PROPERTY::group("Values")) {
          PROPERTY::inputDouble("Area A", &d.area, "cm\u00b2", "%.4g");
          PROPERTY::inputDouble("Second moment Iy", &d.secondMomentY, "cm\u2074", "%.5g", "About local y.");
          PROPERTY::inputDouble("Second moment Iz", &d.secondMomentZ, "cm\u2074", "%.5g", "About local z.");
          PROPERTY::inputDouble("Torsion constant J", &d.torsionConstant, "cm\u2074", "%.4g");
          PROPERTY::inputDouble("Shear area Asy", &d.shearAreaY, "cm\u00b2", "%.4g", "0 = Euler-Bernoulli only.");
          PROPERTY::inputDouble("Shear area Asz", &d.shearAreaZ, "cm\u00b2", "%.4g", "0 = Euler-Bernoulli only.");
        }
      }
      else if (PROPERTY::group("Dimensions")) {
        switch (d.shape) {
          case 1:
            PROPERTY::inputDouble("Height h", &d.height, "mm", "%.4g", "Along local y.");
            PROPERTY::inputDouble("Width b", &d.width, "mm", "%.4g", "Along local z.");
            break;
          case 2:
            PROPERTY::inputDouble("Diameter", &d.diameter, "mm", "%.4g");
            break;
          case 3:
            PROPERTY::inputDouble("Outer diameter", &d.diameter, "mm", "%.4g");
            PROPERTY::inputDouble("Wall t", &d.thickness, "mm", "%.4g");
            break;
          case 4:
            PROPERTY::inputDouble("Height h", &d.height, "mm", "%.4g", "Along local y.");
            PROPERTY::inputDouble("Width b", &d.width, "mm", "%.4g", "Along local z.");
            PROPERTY::inputDouble("Wall t", &d.thickness, "mm", "%.4g");
            PROPERTY::inputDouble("Outer corner radius", &d.outerRadius, "mm", "%.4g");
            PROPERTY::inputDouble("Inner corner radius", &d.innerRadius, "mm", "%.4g");
            break;
          default:
            PROPERTY::inputDouble("Height h", &d.height, "mm", "%.4g", "Web height, along local y.");
            PROPERTY::inputDouble("Flange width b", &d.width, "mm", "%.4g");
            PROPERTY::inputDouble("Web tw", &d.thickness, "mm", "%.4g");
            PROPERTY::inputDouble("Flange tf", &d.flangeThickness, "mm", "%.4g");
            PROPERTY::inputDouble("Root radius r", &d.outerRadius, "mm", "%.4g");
            break;
        }
      }
      PROPERTY::end();
    }
    if (d.shape == 0) ImGui::TextDisabled("No stresses without a shape.");

    const FEM::BEAM::SectionProperties general{d.area * cm2ToM2, d.secondMomentY * cm4ToM4, d.secondMomentZ * cm4ToM4, d.torsionConstant * cm4ToM4,
                                    d.shearAreaY * cm2ToM2, d.shearAreaZ * cm2ToM2};
    const auto shape = draftShape(d.shape, d.height, d.width, d.diameter, d.thickness, d.flangeThickness, d.outerRadius, d.innerRadius, general);
    const auto valid = shape ? FEM::BEAM::validateShape(*shape) : std::expected<void, std::string>(std::unexpect, "unknown shape");
    if (!valid) {
      ImGui::TextColored(THEME::theme().bad, "%s", valid.error().c_str());
    } else {
      drawSection("DraftSectionCanvas", *shape, ImVec2(-FLT_MIN, 120.0f));
      propertyRows("DraftSectionProperties", *shape);
    }
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Add Section", ImVec2(-FLT_MIN, 0.0f))) {
      const auto added = bridge.addUserSection(FEM::BEAM::BeamSection{std::string(d.name.data()), *shape});
      if (added) {
        m_selectedID = *added;
        setStatus(std::format("Section '{}' added", d.name.data()), false);
      } else {
        setStatus("Section not added: " + added.error(), true);
      }
    }
    ImGui::EndDisabled();
  }

} // namespace anaf::GUI end
