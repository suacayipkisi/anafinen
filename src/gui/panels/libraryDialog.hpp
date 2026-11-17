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

#include <beam/beamTypes/beamLibrary.hpp>
#include <guiMaterials/iPanel.hpp>
#include <truss_1D/trussTypes/trussLibrary.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace anaf::GUI {

  // File > Open Example...: the built-in beam and truss model libraries in one dialog (replaces the
  // editors' "Built-in Models" sections). The library files are only read; a loaded model is a copy.
  //
  //   [ Beam / Frame ][ Truss ]
  //   +- models by category -+  name, category
  //   | Building             |  description (wrapped)
  //   |   Portal frame       |
  //   | Bridge ...           |  [Load Model] [Load Solved]
  class LibraryDialog : public IPanel {
  public:
    LibraryDialog() { isOpen = false; }
    void onImGuiRender() override;
    void open();

    // Imports a model file (FileIoPanel::importFile picks the beam or truss path from its content).
    std::function<void(const std::filesystem::path&)> onLoad;

  private:
    bool m_read{false};
    bool m_focusRequested{false};
    std::filesystem::path m_beamDir;
    std::filesystem::path m_trussDir;
    std::vector<FEM::BEAM::LIBRARY::Entry> m_beamModels;
    std::vector<FEM::TRUSS::LIBRARY::Entry> m_trussModels;
    std::string m_beamError;
    std::string m_trussError;
    int m_beamSelected{0};
    int m_trussSelected{0};

    void read();
  };

} // namespace anaf::GUI end
