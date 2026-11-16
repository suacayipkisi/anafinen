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

#include <cstddef>
#include <cstdint>
#include <string>

// Property grid for the editor panels and handlers, in place of collapsing headers and loose
// label / widget rows:
//
//   v Elastic                              group row: grey band, chevron, click folds it
//     Elasticity modulus E   [  210.000 ] GPa
//     Poisson's ratio        [    0.300 ]
//   > Strength                             folded group
//
// Three columns: label | value | unit (the unit column fits its widest text). Thin separators
// between rows, the hovered row is tinted. Physical values are typed, never dragged or slid.
// The fold state of a group lives in ImGui's window storage (per window, per label).
//
// header() is the section level above the grids (in place of ImGui::CollapsingHeader): a
// full-width band with a chevron, outside any table, so buttons and lists can follow it.
//
//   if (PROPERTY::begin("##material")) {
//     if (PROPERTY::group("Elastic")) {
//       PROPERTY::inputDouble("Elasticity modulus E", &e, "GPa", "%.3f");
//     }
//     PROPERTY::end();
//   }
namespace anaf::GUI::PROPERTY {

  // Grids of the editors docked on the left (truss and beam): fixed label and unit columns (font
  // sizes; "Selected node", "N/m"), so labels are not clipped in a narrow dock and the value
  // columns of the grids in one tab line up.
  inline constexpr float editorLabelWidth = 7.4f;
  inline constexpr float editorUnitWidth = 2.4f;

  // Section band across the window, outside a grid. Returns whether the section is open. Text
  // after "##" is not shown ("###" keeps one fold state for changing labels).
  bool header(const char* label, bool defaultOpen = true);

  // Opens the grid; call end() only when it returns true. labelWeight is the label column's share
  // of the width left after the unit column. unitWidth (font sizes) fixes the unit column, so
  // the value columns of consecutive grids line up; 0 fits the widest unit of this grid.
  bool begin(const char* id, float labelWeight = 0.5f, float unitWidth = 0.0f);
  // Editor grid: label column labelWidth font sizes wide, unit column unitWidth.
  bool beginFixed(const char* id, float labelWidth = editorLabelWidth, float unitWidth = editorUnitWidth);
  void end();

  // Group row across all columns. Returns whether the group is open (its rows should follow).
  bool group(const char* label, bool defaultOpen = true);

  // Starts a row: the label (tooltip on hover when given), then the value cell with the next item
  // width set to fill it. Give the widget a hidden "##id" label; unit() closes the row with a unit.
  void row(const char* label, const char* tooltip = nullptr);
  void unit(const char* unit);
  // Square "..." button closing a row, e.g. after a combo: call row(), then reserveBrowse() before
  // the widget so it leaves room, then browseButton(). Returns true when clicked.
  void reserveBrowse();
  bool browseButton(const char* id, const char* tooltip);

  // Typed double with its unit; returns true when the value changed.
  bool inputDouble(const char* label, double* value, const char* unit = nullptr, const char* format = "%.3f",
                   const char* tooltip = nullptr);
  // Typed id (node, element) where none means no selection: shown empty with the hint "none".
  // Returns true when Enter was pressed; value is then the typed id or none for an empty field.
  bool inputId(const char* label, std::uint32_t* value, std::uint32_t none, const char* tooltip = nullptr);
  // Three typed doubles in one row (a vector or a point).
  bool inputVector3(const char* label, double* values, const char* unit = nullptr, const char* format = "%.4g",
                    const char* tooltip = nullptr);
  bool inputText(const char* label, char* buffer, std::size_t size, const char* tooltip = nullptr);
  // Read-only value.
  void value(const char* label, const std::string& text, const char* unit = nullptr, const char* tooltip = nullptr);

} // namespace anaf::GUI::PROPERTY end
