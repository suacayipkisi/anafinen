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


#include "libraryDialog.hpp"

#include <directory/getExecutableDirectory.hpp>
#include <guiMaterials/iconAtlas.hpp>
#include <log/anaf_info.hpp>

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <format>
#include <string_view>

namespace anaf::GUI {

  namespace {
    // Reads one library index sorted by category; the error text when it is missing.
    template <typename Entry, typename LoadIndex>
    void readIndex(const std::string_view subdir, const std::string_view indexName, LoadIndex&& loadIndex, std::filesystem::path& dir,
                   std::vector<Entry>& entries, std::string& error) {
      dir = anaf::DIRECTORY::findAssetPath(std::filesystem::path(subdir));
      if (dir.empty()) {
        error = std::format("assets/{} not found", subdir);
        anaf::LOG::warn("Built-in library: {}", error);
        return;
      }
      auto index = loadIndex(dir / std::filesystem::path(indexName));
      if (!index) {
        error = index.error();
        anaf::LOG::warn("Built-in library: {}", error);
        return;
      }
      std::ranges::stable_sort(*index, {}, &Entry::category);
      entries = std::move(*index);
    }

    // Category headers and selectable names; returns true on a double click (load at once).
    template <typename Entry>
    bool modelList(const std::vector<Entry>& entries, int& selected) {
      bool activated = false;
      std::string_view category;
      for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& entry = entries[static_cast<std::size_t>(i)];
        if (entry.category != category) {
          category = entry.category;
          ImGui::SeparatorText(entry.category.c_str());
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(entry.name.c_str(), i == selected, ImGuiSelectableFlags_AllowDoubleClick)) {
          selected = i;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) activated = true;
        }
        ImGui::PopID();
      }
      return activated;
    }

    // Name, category and description of the selected model.
    template <typename Entry>
    void details(const Entry& entry) {
      ImGui::TextUnformatted(entry.name.c_str());
      ImGui::TextDisabled("%s", entry.category.c_str());
      ImGui::Separator();
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(entry.description.c_str());
      ImGui::PopTextWrapPos();
    }
  } // namespace end

  void LibraryDialog::open() {
    isOpen = true;
    m_focusRequested = true;
  }

  void LibraryDialog::read() {
    m_read = true;
    readIndex<FEM::BEAM::LIBRARY::Entry>(FEM::BEAM::LIBRARY::librarySubdir, FEM::BEAM::LIBRARY::indexFileName,
                                         [](const std::filesystem::path& file) { return FEM::BEAM::LIBRARY::loadIndex(file); }, m_beamDir,
                                         m_beamModels, m_beamError);
    readIndex<FEM::TRUSS::LIBRARY::Entry>(FEM::TRUSS::LIBRARY::librarySubdir, FEM::TRUSS::LIBRARY::indexFileName,
                                          [](const std::filesystem::path& file) { return FEM::TRUSS::LIBRARY::loadIndex(file); }, m_trussDir,
                                          m_trussModels, m_trussError);
  }

  void LibraryDialog::onImGuiRender() {
    if (!isOpen) return;
    if (!m_read) read();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fontSize * 46.0f, fontSize * 30.0f), ImGuiCond_FirstUseEver);
    if (m_focusRequested) {
      ImGui::SetNextWindowFocus();
      m_focusRequested = false;
    }
    if (!ImGui::Begin("Open Example", &isOpen, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
      ImGui::End();
      return;
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const auto layout = [&](auto& entries, int& selected, const std::string& error, auto&& loadButtons) {
      if (entries.empty()) {
        ImGui::TextDisabled("%s", error.empty() ? "No built-in models" : error.c_str());
        return;
      }
      selected = std::clamp(selected, 0, static_cast<int>(entries.size()) - 1);
      // Model list left, details right, both ending above the load buttons.
      ImGui::BeginChild("##model_list", ImVec2(ImGui::GetContentRegionAvail().x * 0.42f, -footer), ImGuiChildFlags_Borders);
      const bool activated = modelList(entries, selected);
      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("##model_details", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
      details(entries[static_cast<std::size_t>(selected)]);
      ImGui::EndChild();
      loadButtons(entries[static_cast<std::size_t>(selected)], activated);
    };

    if (ImGui::BeginTabBar("##library_tabs")) {
      if (ImGui::BeginTabItem("Beam / Frame")) {
        layout(m_beamModels, m_beamSelected, m_beamError, [&](const FEM::BEAM::LIBRARY::Entry& entry, const bool activated) {
          const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
          if ((ICONS::iconTextButton(ICONS::E_IconId::ModelFrame, "Load Model", ImVec2(half, 0.0f)) || activated) && onLoad) {
            onLoad(FEM::BEAM::LIBRARY::modelFile(m_beamDir, entry));
            isOpen = false;
          }
          ImGui::SetItemTooltip("The model only: loads, supports, sections; run the solver yourself (double click a name does the same).");
          ImGui::SameLine();
          if (ICONS::iconTextButton(ICONS::E_IconId::ResultStress, "Load Solved", ImVec2(-FLT_MIN, 0.0f)) && onLoad) {
            onLoad(FEM::BEAM::LIBRARY::solvedFile(m_beamDir, entry));
            isOpen = false;
          }
          ImGui::SetItemTooltip("The same model with its results (solved by anaf_beam_library_tool).");
        });
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Truss")) {
        layout(m_trussModels, m_trussSelected, m_trussError, [&](const FEM::TRUSS::LIBRARY::Entry& entry, const bool activated) {
          if ((ICONS::iconTextButton(ICONS::E_IconId::ModelTruss, "Load Model", ImVec2(-FLT_MIN, 0.0f)) || activated) && onLoad) {
            onLoad(FEM::TRUSS::LIBRARY::modelFile(m_trussDir, entry));
            isOpen = false;
          }
          ImGui::SetItemTooltip("Read-only library file: loading makes a copy; save changes with File > Export.");
        });
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
