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

#include <ostream>
#include <string_view>

namespace anaf::CLI {

  namespace SIDE_COMMANDS {

    // Print a text file from assets/global/; false (and a logged error) when it is missing.
    bool help(std::ostream& out);
    bool operations(std::ostream& out);
    bool license(std::ostream& out);
    bool thirdPartyLicenses(std::ostream& out);

    // Prints assets/global/<fileName> without its leading "//" header lines (the SPDX notice
    // of the asset itself).
    bool printAssetText(std::string_view fileName, std::ostream& out);

  } // namespace SIDE_COMMANDS end

} // namespace anaf::CLI end
