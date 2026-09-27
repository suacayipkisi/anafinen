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

#include <string>

#ifndef ANAFINEN_VERSION
#define ANAFINEN_VERSION "unknown"
#endif

namespace anaf::GUI {

  // GPLv3 section 0 / 5(d) "Appropriate Legal Notices", shown at startup and in Help > About.
  inline constexpr const char* kCopyrightNotice =
    "Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)";
  inline constexpr const char* kShortLegalNotice =
    "anafinen comes with ABSOLUTELY NO WARRANTY. This is free software, and you are welcome to "
    "redistribute it under the terms of the GNU General Public License v3.0 or later; see Help > About.";

  // Help > About: version, copyright, warranty disclaimer, license text and third-party notices.
  class AboutPanel : public IPanel {
  public:
    AboutPanel() { isOpen = false; }
    void onImGuiRender() override;

  private:
    void loadTexts();

    bool m_loaded{false};
    std::string m_licenseText;
    std::string m_thirdPartyText;
  };

} // namespace anaf::GUI end
