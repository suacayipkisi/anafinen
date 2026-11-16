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

#include "propertyGrid.hpp"

#include <guiMaterials/imGuiLayer.hpp>
#include <guiMaterials/theme.hpp>

#include "imgui.h"
#include "imgui_internal.h" // TableGetHoveredRow (not public yet)

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace anaf::GUI::PROPERTY {

  namespace {
    // Labels (rows and group titles) start after the chevron, in font-size units.
    constexpr float labelIndent = 1.15f;

    // Hover tint of rows and bands: grey in themes with neutral hovers, else a faint accent.
    ImU32 hoverTint() {
      const THEME::ThemePalette& palette = THEME::theme();
      return palette.style.neutralHover ? THEME::toU32(palette.text, 0.06f) : THEME::toU32(palette.accent, 0.10f);
    }

    // Tints the current row when the mouse was over it in the previous frame (ImGui keeps the
    // hovered row one frame late; invisible at frame rate).
    void tintHoveredRow() {
      if (ImGui::TableGetHoveredRow() != ImGui::TableGetRowIndex()) return;
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, hoverTint());
    }

    // Chevron centered at center, size pixels wide: pointing right (folded) or down (open).
    void drawChevron(ImDrawList* drawList, const ImVec2 center, const float size, const bool open, const ImU32 color) {
      const float h = size * 0.5f;
      const float q = size * 0.25f;
      const float thickness = std::max(1.0f, size * 0.16f);
      if (open) {
        const ImVec2 points[3]{ImVec2(center.x - h, center.y - q), ImVec2(center.x, center.y + q), ImVec2(center.x + h, center.y - q)};
        drawList->AddPolyline(points, 3, color, ImDrawFlags_None, thickness);
      }
      else {
        const ImVec2 points[3]{ImVec2(center.x - q, center.y - h), ImVec2(center.x + q, center.y), ImVec2(center.x - q, center.y + h)};
        drawList->AddPolyline(points, 3, color, ImDrawFlags_None, thickness);
      }
    }

    // Band label: the text before "##", vertically centered in the band; colors follow the
    // disabled alpha like ImGui's own widgets.
    void drawBandLabel(const ImVec2 min, const float height, const char* label, const bool open, ImFont* font) {
      const THEME::ThemePalette& palette = THEME::theme();
      const float fontSize = ImGui::GetFontSize();
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      const float centerY = std::floor(min.y + height * 0.5f) + 0.5f;
      drawChevron(drawList, ImVec2(std::floor(min.x + fontSize * 0.45f) + 0.5f, centerY), std::round(fontSize * 0.5f), open,
                  ImGui::GetColorU32(palette.textDim));
      drawList->AddText(font, fontSize, ImVec2(min.x + fontSize * labelIndent, min.y + (height - fontSize) * 0.5f),
                        ImGui::GetColorU32(palette.text), label, ImGui::FindRenderedTextEnd(label));
    }

  } // namespace end

  bool header(const char* label, const bool defaultOpen) {
    const THEME::ThemePalette& palette = THEME::theme();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(label);
    bool open = storage->GetBool(id, defaultOpen);

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 1.0f), ImGui::GetFrameHeight());
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##header", size)) {
      open = !open;
      storage->SetBool(id, open);
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    // One step darker than the group rows inside the grids: sections above groups.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(THEME::toU32(palette.text, palette.light ? 0.10f : 0.12f)));
    if (hovered) drawList->AddRectFilled(min, max, ImGui::GetColorU32(hoverTint()));
    drawBandLabel(min, size.y, label, open, ImGuiLayer::g_fontUiStrong ? ImGuiLayer::g_fontUiStrong : ImGui::GetFont());
    return open;
  }

  bool begin(const char* id, const float labelWeight, const float unitWidth) {
    const THEME::ThemePalette& palette = THEME::theme();
    // Row separators softer than ImGui's table borders; the color is taken in BeginTable.
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, THEME::mix(palette.panel, palette.border, 0.55f));
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    const bool visible = ImGui::BeginTable(id, 3, flags);
    ImGui::PopStyleColor();
    if (!visible) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, labelWeight);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelWeight);
    // Without a width the fixed column fits the widest unit.
    ImGui::TableSetupColumn("##unit", ImGuiTableColumnFlags_WidthFixed, unitWidth > 0.0f ? unitWidth * ImGui::GetFontSize() : 0.0f);
    return true;
  }

  bool beginFixed(const char* id, const float labelWidth, const float unitWidth) {
    const THEME::ThemePalette& palette = THEME::theme();
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, THEME::mix(palette.panel, palette.border, 0.55f));
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    const bool visible = ImGui::BeginTable(id, 3, flags);
    ImGui::PopStyleColor();
    if (!visible) return false;
    const float fontSize = ImGui::GetFontSize();
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth * fontSize);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##unit", ImGuiTableColumnFlags_WidthFixed, unitWidth * fontSize);
    return true;
  }

  void end() { ImGui::EndTable(); }

  bool group(const char* label, const bool defaultOpen) {
    const THEME::ThemePalette& palette = THEME::theme();
    ImGui::TableNextRow();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, THEME::toU32(palette.text, palette.light ? 0.07f : 0.08f));
    tintHoveredRow();
    ImGui::TableSetColumnIndex(0);

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(label);
    bool open = storage->GetBool(id, defaultOpen);

    // Click area over the whole row; the band and the hover tint are the row backgrounds above.
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Selectable("##group", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, ImGui::GetFrameHeight()))) {
      open = !open;
      storage->SetBool(id, open);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    drawBandLabel(ImGui::GetItemRectMin(), ImGui::GetItemRectSize().y, label, open, ImGui::GetFont());
    return open;
  }

  void row(const char* label, const char* tooltip) {
    ImGui::TableNextRow();
    tintHoveredRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize() * labelIndent);
    const bool clipped = ImGui::CalcTextSize(label).x > ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(label);
    // A clipped label shows in full on hover.
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    else if (clipped) ImGui::SetItemTooltip("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
  }

  void unit(const char* unit) {
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", unit);
  }

  void reserveBrowse() { ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x)); }

  bool browseButton(const char* id, const char* tooltip) {
    ImGui::SameLine();
    ImGui::PushID(id);
    const bool clicked = ImGui::Button("...", ImVec2(ImGui::GetFrameHeight(), 0.0f));
    ImGui::PopID();
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
  }

  bool inputId(const char* label, std::uint32_t* value, const std::uint32_t none, const char* tooltip) {
    row(label, tooltip);
    // Rebuilt every frame from the value; ImGui edits its own copy while the field is active.
    char text[16]{};
    if (*value != none) std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(*value));
    ImGui::PushID(label);
    const bool entered = ImGui::InputTextWithHint("##value", "none", text, sizeof(text),
                                                  ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopID();
    if (!entered) return false;
    std::uint32_t typed = none;
    const char* end = text + std::char_traits<char>::length(text);
    if (const auto [ptr, error] = std::from_chars(text, end, typed); error != std::errc{} || ptr != end) typed = none;
    *value = typed;
    return true;
  }

  bool inputDouble(const char* label, double* value, const char* unit, const char* format, const char* tooltip) {
    row(label, tooltip);
    ImGui::PushID(label);
    const bool changed = ImGui::InputDouble("##value", value, 0.0, 0.0, format);
    ImGui::PopID();
    if (unit) PROPERTY::unit(unit);
    return changed;
  }

  bool inputVector3(const char* label, double* values, const char* unit, const char* format, const char* tooltip) {
    row(label, tooltip);
    ImGui::PushID(label);
    const bool changed = ImGui::InputScalarN("##value", ImGuiDataType_Double, values, 3, nullptr, nullptr, format);
    ImGui::PopID();
    if (unit) PROPERTY::unit(unit);
    return changed;
  }

  bool inputText(const char* label, char* buffer, const std::size_t size, const char* tooltip) {
    row(label, tooltip);
    ImGui::PushID(label);
    const bool changed = ImGui::InputText("##value", buffer, size);
    ImGui::PopID();
    return changed;
  }

  void value(const char* label, const std::string& text, const char* unit, const char* tooltip) {
    row(label, tooltip);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.c_str());
    if (unit) PROPERTY::unit(unit);
  }

} // namespace anaf::GUI::PROPERTY end
