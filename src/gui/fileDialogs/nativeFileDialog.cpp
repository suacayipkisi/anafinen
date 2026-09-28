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

#include "nativeFileDialog.hpp"

// portable-file-dialogs is a large header; it is included in this translation unit only.
#include <portable-file-dialogs.h>

#include <io/core/pathUtf8.hpp> // pfd takes and returns UTF-8 strings

#include <variant>

namespace anaf::GUI {

  namespace {
    std::vector<std::string> toPfdFilters(const std::vector<FileFilter>& filters) {
      std::vector<std::string> flat;
      for (const auto& filter : filters) {
        std::string patterns;
        for (const auto& pattern : filter.patterns) {
          if (!patterns.empty()) patterns += ' ';
          patterns += pattern;
        }
        flat.push_back(filter.label);
        flat.push_back(patterns);
      }
      flat.emplace_back("All Files");
      flat.emplace_back("*");
      return flat;
    }
  } // namespace end

  struct NativeFileDialog::Impl {
    std::variant<std::unique_ptr<pfd::open_file>, std::unique_ptr<pfd::save_file>> dialog;
    std::optional<std::optional<std::filesystem::path>> cached;
  };

  NativeFileDialog::NativeFileDialog(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

  NativeFileDialog::~NativeFileDialog() {
    // Closing the application while the chooser is open must not leave it behind.
    if (!m_impl) return;
    std::visit([](auto& dialog) {
      if (dialog && !dialog->ready(0)) dialog->kill();
    }, m_impl->dialog);
  }

  bool NativeFileDialog::available() {
    return pfd::settings::available();
  }

  std::unique_ptr<NativeFileDialog> NativeFileDialog::openFile(const std::string& title, const std::vector<FileFilter>& filters) {
    auto impl = std::make_unique<Impl>();
    impl->dialog = std::make_unique<pfd::open_file>(title, "", toPfdFilters(filters), pfd::opt::none);
    return std::unique_ptr<NativeFileDialog>(new NativeFileDialog(std::move(impl)));
  }

  std::unique_ptr<NativeFileDialog> NativeFileDialog::saveFile(const std::string& title, const std::filesystem::path& defaultPath,
                                                               const std::vector<FileFilter>& filters) {
    auto impl = std::make_unique<Impl>();
    impl->dialog = std::make_unique<pfd::save_file>(title, anaf::IO::pathToUtf8(defaultPath), toPfdFilters(filters), pfd::opt::force_overwrite);
    return std::unique_ptr<NativeFileDialog>(new NativeFileDialog(std::move(impl)));
  }

  bool NativeFileDialog::ready() {
    if (m_impl->cached) return true;
    return std::visit([](auto& dialog) { return dialog->ready(0); }, m_impl->dialog);
  }

  std::optional<std::filesystem::path> NativeFileDialog::result() {
    if (!m_impl->cached) {
      std::optional<std::filesystem::path> path;
      if (auto* open = std::get_if<std::unique_ptr<pfd::open_file>>(&m_impl->dialog)) {
        const auto files = (*open)->result();
        if (!files.empty() && !files.front().empty()) path = anaf::IO::pathFromUtf8(files.front());
      } else {
        const auto file = std::get<std::unique_ptr<pfd::save_file>>(m_impl->dialog)->result();
        if (!file.empty()) path = anaf::IO::pathFromUtf8(file);
      }
      m_impl->cached = path;
    }
    return *m_impl->cached;
  }

} // namespace anaf::GUI end
