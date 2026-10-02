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

#include <functional>
#include <string_view>
#include <vector>

namespace anaf::GUI {

  // Combo order: the value is the row in TrussSelector::m_types.
  enum TrussTypes {
    nodeEntered,
    simpleQuadranglePrism
  };

  class TrussSelector : public IPanel {
  private:
    TrussTypes m_trussType{nodeEntered}; // imported / self-built is offered first
    const std::vector<std::string_view> m_types {"Imported / Self-Built", "Simple Quadrangle"};
  public:
    // Switches the object type (see bindAnalysisFlow): the bridge model and the panels are
    // reset when the type changes, so nothing of the previous model is left behind.
    std::function<void(TrussTypes)> onSelected;

    void onImGuiRender() override;
  };
} // namespace anaf::GUI end

