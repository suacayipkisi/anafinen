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

#include <guiMaterials/iPanel.hpp>

#include <cstdint>
#include <functional>

namespace anaf::GUI {

  // Commands of the ribbon; gui.cpp routes them to the panels of the active model.
  enum class E_ModelCommand : std::uint8_t {
    Library,          // File > Open Example...
    Import,           // File > Import Mesh / CAD...
    Export,           // File > Export Model...
    AddNode,          // Add Node window of the active editor
    AddElement,       // Add Element / Add Bar window
    AssignAll,        // Assign to All Elements / Bars window
    Materials,        // Material Handler
    Sections,         // Section Handler (beam)
    SelfWeight,       // self weight on / off (beam; trusses always carry it)
    AnalysisSettings, // Analysis Settings window
    Solve,            // run the static solver of the active model
    Diagrams          // Beam Diagrams panel (beam)
  };

  struct ModelCommands {
    std::function<bool(E_ModelCommand)> isAvailable; // empty = always
    std::function<bool(E_ModelCommand)> isActive;    // toggles drawn selected (self weight, diagrams)
    std::function<void(E_ModelCommand)> run;
  };

  // Ribbon under the menu bar (UI polish S6), docked by MainDockSpaceHost as a full-width strip of
  // fixed height: large icon buttons with a label, in captioned groups.
  //
  //   [Examples][Import][Export] | [Node][Element][Assign][Materials][Sections] | [Self Weight] | [Settings][Solve] | [Deform.][Diagrams]
  //            FILE              |                 MODEL                        |    LOADS      |     ANALYSIS      |      RESULTS
  class Ribbon : public IPanel {
  public:
    static constexpr const char* windowName = "Ribbon";
    // Height of the docked strip: button row, captions and the window padding.
    static float windowHeight();

    void onImGuiRender() override;

    ModelCommands commands;

  private:
    // One large button for a command; disabled when unavailable.
    void commandButton(E_ModelCommand command);
    void renderDeformation();
  };

} // namespace anaf::GUI end
