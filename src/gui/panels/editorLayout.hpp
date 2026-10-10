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

#include <guiMaterials/theme.hpp>

#include "imgui.h"

#include <cfloat>
#include <string>

// Layout pieces shared by the editor panels on the left (truss analysis set, truss model
// editor, beam frame editor), so the three look alike:
//
//   summary card      counts and result status, sized to its content
//   tab bar           Model | Supports & Loads | Analysis
//   tab body          scrolls; ends where the footer starts
//   footer            deformation scale, run button, model buttons; always visible
namespace anaf::GUI::LAYOUT {

  inline constexpr float runButtonHeight = 32.0f;
  // Status colors (good / warn / bad / note) come from THEME::theme().

  // Bordered card that is as tall as its content. Always pair with endCard().
  inline void beginCard(const char* id) {
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  }
  inline void endCard() { ImGui::EndChild(); }

  // Four-column grid of "label value" pairs inside a card. Fill with stat(), close with ImGui::EndTable().
  inline bool beginStats(const char* id) {
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("##label1", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("##value1", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("##label2", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("##value2", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    return true;
  }
  inline void stat(const char* label, const std::string& value) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.c_str());
  }

  // Wrapped colored line (edit results, errors).
  inline void wrappedColored(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }

  // Label on the left at a fixed column, the next widget fills the rest of the row. Give the
  // widget a hidden "##id" label.
  inline void field(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
  }

  // Width of one of count equal buttons in a row.
  inline float splitWidth(const int count) {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) / static_cast<float>(count);
  }

  // Full-width accent button for the main action of a panel (run the solver).
  inline bool primaryButton(const char* label) {
    const THEME::ThemePalette& palette = THEME::theme();
    ImGui::PushStyleColor(ImGuiCol_Button, palette.primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.primaryHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.primaryActive);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(label, ImVec2(-FLT_MIN, runButtonHeight));
    ImGui::PopStyleColor(4);
    return pressed;
  }

  // Height of a footer with rows normal widget rows and runButtons primary buttons, separator included.
  inline float footerHeight(const int rows, const int runButtons) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return style.ItemSpacing.y * 2.0f + 1.0f + static_cast<float>(rows) * ImGui::GetFrameHeightWithSpacing() +
           static_cast<float>(runButtons) * (runButtonHeight + style.ItemSpacing.y);
  }

  // Scrolling body of a tab, ending footer pixels above the window bottom. Always pair with endBody().
  inline void beginBody(const char* id, const float footer) {
    ImGui::BeginChild(id, ImVec2(0.0f, -footer));
  }
  inline void endBody() {
    ImGui::EndChild();
    ImGui::Separator();
  }

} // namespace anaf::GUI::LAYOUT end
