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

#include "aboutPanel.hpp"

#include <directory/getExecutableDirectory.hpp>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace anaf::GUI {

  namespace {
    // Same search order as the other assets: next to the executable (Windows ZIP), the Linux
    // package location, the source tree (development builds), the working directory.
    std::string readShippedFile(const char* name) {
      std::vector<std::filesystem::path> candidates{
        anaf::DIRECTORY::getExecutableDirectory() / name,
        std::filesystem::path("/usr/share/doc/anafinen") / name,
#ifdef MAIN_DIR
        std::filesystem::path(MAIN_DIR) / name,
#endif
        std::filesystem::path(name),
      };
      for (const auto& path : candidates) {
        std::ifstream file(path);
        if (!file) continue;
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
      }
      return {};
    }
  } // namespace end

  void AboutPanel::loadTexts() {
    m_licenseText = readShippedFile("LICENSE");
    if (m_licenseText.empty()) m_licenseText = "The LICENSE file was not found. Full text: https://www.gnu.org/licenses/gpl-3.0.html";
    m_thirdPartyText = readShippedFile("THIRD_PARTY_LICENSES.md");
    if (m_thirdPartyText.empty()) m_thirdPartyText = "THIRD_PARTY_LICENSES.md was not found.";
    m_loaded = true;
  }

  void AboutPanel::onImGuiRender() {
    constexpr const char* title = "About anafinen";
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y * 0.6f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (!ImGui::BeginPopupModal(title, &open)) {
      isOpen = false;
      return;
    }
    if (!m_loaded) loadTexts();

    ImGui::Text("anafinen %s - Analyze Finite Element Engineering", ANAFINEN_VERSION);
    ImGui::TextUnformatted(kCopyrightNotice);
    ImGui::TextDisabled("Source code: https://github.com/suacayipkisi/anafinen");
    ImGui::Separator();
    ImGui::TextWrapped("This program is free software: you can redistribute it and/or modify it under the terms of the "
                       "GNU General Public License as published by the Free Software Foundation, either version 3 of the "
                       "License, or (at your option) any later version.");
    ImGui::TextWrapped("This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even "
                       "the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General "
                       "Public License for more details.");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##aboutTabs")) {
      for (const auto& [label, text] : {std::pair{"License (GPLv3)", &m_licenseText}, std::pair{"Third-party notices", &m_thirdPartyText}}) {
        if (ImGui::BeginTabItem(label)) {
          ImGui::BeginChild("##text", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
          ImGui::TextUnformatted(text->c_str());
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)) || !open) {
      ImGui::CloseCurrentPopup();
      isOpen = false;
    }
    ImGui::EndPopup();
  }

} // namespace anaf::GUI end
