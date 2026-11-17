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

#include <array>

namespace anaf::GUI {

  // Dynamic analysis inputs of the Analysis Settings window (E_LoadKind::Dynamic). GUI only so far:
  // nothing reads them until the modal solver exists.
  struct DynamicAnalysisInputs {
    int analysisType{0}; // row in dynamicAnalysisTypes
    int modeCount{10};   // lowest modes to extract
    int massMatrix{0};   // row in massMatrixTypes
  };

  inline constexpr std::array<const char*, 3> dynamicAnalysisTypes{"Modal (natural frequencies)", "Harmonic", "Transient"};
  inline constexpr std::array<const char*, 2> massMatrixTypes{"Consistent", "Lumped"};

} // namespace anaf::GUI end
