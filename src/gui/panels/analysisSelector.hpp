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

#include <bridge/generalStatus.hpp>
#include <guiMaterials/iPanel.hpp>

#include <array>
#include <functional>
#include <string_view>

namespace anaf::GUI {

  // Structure family picked in the Analyze menu; the selector asks for the rest.
  enum class StructureFamily {
    truss,
    beam
  };

  // Combo order: the value is the row in AnalysisSelector::kTrussTypes.
  enum TrussTypes {
    nodeEntered,
    simpleQuadranglePrism
  };

  // "Select Analysis" dialog: truss type (truss only) and load kind (constant / dynamic).
  // Opened by the Analyze menu through open(); the choice is applied in bindAnalysisFlow.
  class AnalysisSelector : public IPanel {
  private:
    static constexpr std::array<std::string_view, 2> kTrussTypes{"Imported / Self-Built", "Simple Quadrangle"};
    static constexpr std::array<std::string_view, 2> kLoadKinds{"Constant Load (Static)", "Dynamic Load (not available yet)"};

    StructureFamily m_family{StructureFamily::truss};
    TrussTypes m_trussType{nodeEntered}; // imported / self-built is offered first
    BRIDGE::LoadKind m_loadKind{BRIDGE::LoadKind::constant};

  public:
    // Type changed: the bridge model and the panels are reset, so nothing of the previous
    // model is left behind. A change of the load kind alone keeps the model.
    std::function<void(TrussTypes, BRIDGE::LoadKind)> onTrussSelected;
    std::function<void(BRIDGE::LoadKind)> onBeamSelected;

    // Shows the dialog for family, preselecting the active load kind.
    void open(StructureFamily family);
    void onImGuiRender() override;
  };
} // namespace anaf::GUI end
