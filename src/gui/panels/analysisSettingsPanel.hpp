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
#include <panels/dynamicAnalysisInputs.hpp>

namespace anaf::GUI {

  // Analyze > Analysis Settings... (and the toolbar): the load kind of the active model and, for a
  // dynamic analysis, its inputs (a preview until the dynamic solver exists). Replaces the editors'
  // "Analysis" tabs; changing the load kind keeps the model.
  class AnalysisSettingsPanel : public IPanel {
  public:
    AnalysisSettingsPanel() { isOpen = false; }
    void onImGuiRender() override;
    void open();

  private:
    DynamicAnalysisInputs m_dynamic;
    bool m_focusRequested{false};
  };

} // namespace anaf::GUI end
