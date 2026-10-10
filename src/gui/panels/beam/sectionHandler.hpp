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
#include <cstdint>
#include <limits>
#include <string>

namespace anaf::GUI {

  // Lists the beam sections (catalogue read-only, user sections removable), shows the selected
  // one with its outline and properties, and adds user sections from a shape and dimensions.
  class SectionHandler : public IPanel {
  private:
    static constexpr std::uint32_t noSelection = std::numeric_limits<std::uint32_t>::max();

    // Dimensions in the units shown in the panel (mm, cm^2, cm^4).
    struct Draft {
      std::array<char, 128> name{};
      int shape{5}; // index into the shape list: general, rectangle, circle, pipe, box, I
      double height{300.0};
      double width{150.0};
      double diameter{100.0};
      double thickness{8.0};
      double flangeThickness{10.0};
      double outerRadius{0.0};
      double innerRadius{0.0};
      double area{50.0};
      double secondMomentY{500.0};
      double secondMomentZ{5000.0};
      double torsionConstant{20.0};
      double shearAreaY{20.0};
      double shearAreaZ{30.0};
    };

    Draft m_draft{};
    std::array<char, 64> m_filter{};
    std::uint32_t m_selectedID{noSelection};
    std::string m_status;
    bool m_statusIsError{false};

    void renderTable();
    void renderSelected();
    void renderAddForm();
    void setStatus(std::string message, bool isError);

  public:
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
