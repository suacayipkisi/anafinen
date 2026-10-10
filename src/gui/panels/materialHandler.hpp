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

#include <array>
#include <string>

namespace anaf::GUI {

  // Lists the materials; user materials can be added and removed, built-ins are read-only.
  class MaterialHandler : public IPanel {
  private:
    // Form values in the units shown in the panel (GPa, MPa, kg/m^3, %).
    struct Draft {
      std::array<char, 128> name{}; // UTF-8 from ImGui; maxMaterialNameLength is 120 bytes
      double elasticityModulusGPa{};
      double shearModulusGPa{};
      double bulkModulusGPa{};
      double yieldStrengthMPa{};
      double ultimateStrengthMPa{};
      double density{};
      double poissonsRatio{0.3};
      double ductilityPercent{};
    };

    Draft m_draft{};
    std::string m_status;
    bool m_statusIsError{false};

    void renderMaterialTable();
    void renderAddForm();
    void setStatus(std::string message, bool isError);
  public:
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
