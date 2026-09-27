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

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace anaf::GUI {

  struct FileFilter {
    std::string label;                 // e.g. "Gmsh MSH"
    std::vector<std::string> patterns; // e.g. {"*.msh"}
  };

  // The operating system's own file chooser (Windows common dialog, zenity / kdialog on Linux),
  // opened without blocking the frame loop: poll ready() every frame, then read result().
  class NativeFileDialog {
  public:
    static std::unique_ptr<NativeFileDialog> openFile(const std::string& title, const std::vector<FileFilter>& filters);
    static std::unique_ptr<NativeFileDialog> saveFile(const std::string& title, const std::filesystem::path& defaultPath,
                                                      const std::vector<FileFilter>& filters);
    // False when no native backend exists (e.g. Linux without zenity / kdialog).
    static bool available();

    ~NativeFileDialog();
    NativeFileDialog(const NativeFileDialog&) = delete;
    NativeFileDialog& operator=(const NativeFileDialog&) = delete;

    bool ready();
    // Valid once ready(); std::nullopt when the user cancelled.
    std::optional<std::filesystem::path> result();

  private:
    struct Impl;
    explicit NativeFileDialog(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };

} // namespace anaf::GUI end
