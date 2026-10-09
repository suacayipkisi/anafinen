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

#include <guiMaterials/theme.hpp>

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
    using namespace FEM::BEAM;

    constexpr double kMm = 1e-3;
    constexpr double kCm2 = 1e-4;
    constexpr double kCm4 = 1e-8;
    constexpr double kPoisson = 0.3; // shown shear areas only; the solve uses each element's material
    constexpr std::array<const char*, 6> kShapes{"General (values)", "Rectangle", "Circle", "Pipe (CHS)", "Box (RHS / SHS)", "I / H section"};
    constexpr std::array<const char*, 6> kShapeNames{"general", "rectangle", "circle", "pipe", "box", "I"};

    bool contains(std::string_view text, std::string_view part) {
      const auto lower = [](const unsigned char c) { return static_cast<char>(std::tolower(c)); };
      std::string a(text), b(part);
      std::ranges::transform(a, a.begin(), lower);
      std::ranges::transform(b, b.begin(), lower);
      return a.find(b) != std::string::npos;
    }

    // Outline in a canvas of the given size: local z to the right, local y up, i.e. looking
    // from node 1 towards node 2 (z x y points back at the viewer = -x).
    void drawSection(const char* id, const SectionShape& shape, const ImVec2 size) {
      ImGui::BeginChild(id, size, true);
      const auto loops = sectionOutline(shape, 12);
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

    void propertyLines(const SectionShape& shape) {
      const auto p = computeProperties(shape, kPoisson);
      ImGui::Text("A   = %.4g cm^2", p.area / kCm2);
      ImGui::Text("Iy  = %.5g cm^4  (about local y)", p.secondMomentY / kCm4);
      ImGui::Text("Iz  = %.5g cm^4  (about local z)", p.secondMomentZ / kCm4);
      ImGui::Text("J   = %.4g cm^4", p.torsionConstant / kCm4);
      ImGui::Text("Asy = %.4g cm^2, Asz = %.4g cm^2", p.shearAreaY / kCm2, p.shearAreaZ / kCm2);
      ImGui::TextDisabled("Shear areas for v = 0.3; the solve uses each material's v.");
    }

    std::optional<SectionShape> draftShape(const int shape, const double height, const double width, const double diameter,
                                           const double thickness, const double flangeThickness, const double outerRadius,
                                           const double innerRadius, const SectionProperties& general) {
      switch (shape) {
        case 0: return GeneralSection{general};
        case 1: return RectangleSection{height * kMm, width * kMm};
        case 2: return CircleSection{diameter * kMm};
        case 3: return PipeSection{diameter * kMm, thickness * kMm};
        case 4: return BoxSection{height * kMm, width * kMm, thickness * kMm, outerRadius * kMm, innerRadius * kMm};
        case 5: return ISection{height * kMm, width * kMm, thickness * kMm, flangeThickness * kMm, outerRadius * kMm};
        default: return std::nullopt;
      }
    }

    void inputMm(const char* label, double* value) {
      ImGui::SetNextItemWidth(140.0f);
      ImGui::InputDouble(label, value, 0.0, 0.0, "%.4g");
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
        ImGui::TableSetupColumn("A (cm2)");
        ImGui::TableSetupColumn("Iy (cm4)");
        ImGui::TableSetupColumn("Iz (cm4)");
        ImGui::TableSetupColumn("J (cm4)");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
          for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& section = bridge.allSections[rows[static_cast<std::size_t>(row)]];
            const auto p = computeProperties(section.getShape(), kPoisson);
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
            ImGui::TextUnformatted(kShapeNames[section.getShape().index()]);
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", p.area / kCm2);
            ImGui::TableNextColumn();
            ImGui::Text("%.5g", p.secondMomentY / kCm4);
            ImGui::TableNextColumn();
            ImGui::Text("%.5g", p.secondMomentZ / kCm4);
            ImGui::TableNextColumn();
            ImGui::Text("%.4g", p.torsionConstant / kCm4);
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
    std::optional<BeamSection> selected;
    {
      std::lock_guard lock(bridge.dataMutex);
      if (const auto index = bridge.findSectionIndex(m_selectedID)) selected = bridge.allSections[*index];
    }
    ImGui::SeparatorText("Selected");
    if (!selected) {
      ImGui::TextDisabled("Click a section in the table.");
      return;
    }
    ImGui::TextUnformatted(selected->getName().c_str());
    drawSection("SelectedSectionCanvas", selected->getShape(), ImVec2(-FLT_MIN, 180.0f));
    propertyLines(selected->getShape());
  }

  void SectionHandler::renderAddForm() {
    auto& bridge = BRIDGE::buildBridge();
    ImGui::SeparatorText("New Section");
    auto& d = m_draft;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##section_name", "Name", d.name.data(), d.name.size());
    ImGui::SetNextItemWidth(200.0f);
    ImGui::Combo("Shape##section_shape", &d.shape, kShapes.data(), static_cast<int>(kShapes.size()));

    switch (d.shape) {
      case 0:
        ImGui::InputDouble("A [cm^2]", &d.area, 0.0, 0.0, "%.4g");
        ImGui::InputDouble("Iy [cm^4]", &d.secondMomentY, 0.0, 0.0, "%.5g");
        ImGui::InputDouble("Iz [cm^4]", &d.secondMomentZ, 0.0, 0.0, "%.5g");
        ImGui::InputDouble("J [cm^4]", &d.torsionConstant, 0.0, 0.0, "%.4g");
        ImGui::InputDouble("Asy [cm^2]", &d.shearAreaY, 0.0, 0.0, "%.4g");
        ImGui::InputDouble("Asz [cm^2]", &d.shearAreaZ, 0.0, 0.0, "%.4g");
        ImGui::TextDisabled("Shear areas 0 = Euler-Bernoulli only. No stresses without a shape.");
        break;
      case 1:
        inputMm("Height h (local y) [mm]", &d.height);
        inputMm("Width b (local z) [mm]", &d.width);
        break;
      case 2:
        inputMm("Diameter [mm]", &d.diameter);
        break;
      case 3:
        inputMm("Outer diameter [mm]", &d.diameter);
        inputMm("Wall t [mm]", &d.thickness);
        break;
      case 4:
        inputMm("Height h (local y) [mm]", &d.height);
        inputMm("Width b (local z) [mm]", &d.width);
        inputMm("Wall t [mm]", &d.thickness);
        inputMm("Outer corner radius [mm]", &d.outerRadius);
        inputMm("Inner corner radius [mm]", &d.innerRadius);
        break;
      default:
        inputMm("Height h (web, local y) [mm]", &d.height);
        inputMm("Flange width b [mm]", &d.width);
        inputMm("Web tw [mm]", &d.thickness);
        inputMm("Flange tf [mm]", &d.flangeThickness);
        inputMm("Root radius r [mm]", &d.outerRadius);
        break;
    }

    const SectionProperties general{d.area * kCm2, d.secondMomentY * kCm4, d.secondMomentZ * kCm4, d.torsionConstant * kCm4,
                                    d.shearAreaY * kCm2, d.shearAreaZ * kCm2};
    const auto shape = draftShape(d.shape, d.height, d.width, d.diameter, d.thickness, d.flangeThickness, d.outerRadius, d.innerRadius, general);
    const auto valid = shape ? validateShape(*shape) : std::expected<void, std::string>(std::unexpect, "unknown shape");
    if (!valid) {
      ImGui::TextColored(THEME::theme().bad, "%s", valid.error().c_str());
    } else {
      drawSection("DraftSectionCanvas", *shape, ImVec2(-FLT_MIN, 120.0f));
      propertyLines(*shape);
    }
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Add Section", ImVec2(-FLT_MIN, 0.0f))) {
      const auto added = bridge.addUserSection(BeamSection{std::string(d.name.data()), *shape});
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
