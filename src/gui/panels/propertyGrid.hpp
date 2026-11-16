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
//   if (PROPERTY::begin("##material")) {
//     if (PROPERTY::group("Elastic")) {
//       PROPERTY::inputDouble("Elasticity modulus E", &e, "GPa", "%.3f");
//     }
//     PROPERTY::end();
//   }
namespace anaf::GUI::PROPERTY {

  // Opens the grid; call end() only when it returns true. labelWeight is the label column's share
  // of the width left after the unit column.
  bool begin(const char* id, float labelWeight = 0.5f);
  void end();

  // Group row across all columns. Returns whether the group is open (its rows should follow).
  bool group(const char* label, bool defaultOpen = true);

  // Starts a row: the label (tooltip on hover when given), then the value cell with the next item
  // width set to fill it. Give the widget a hidden "##id" label; unit() closes the row with a unit.
  void row(const char* label, const char* tooltip = nullptr);
  void unit(const char* unit);

  // Typed double with its unit; returns true when the value changed.
  bool inputDouble(const char* label, double* value, const char* unit = nullptr, const char* format = "%.3f",
                   const char* tooltip = nullptr);
  bool inputText(const char* label, char* buffer, std::size_t size, const char* tooltip = nullptr);
  // Read-only value.
  void value(const char* label, const std::string& text, const char* unit = nullptr, const char* tooltip = nullptr);

} // namespace anaf::GUI::PROPERTY end
