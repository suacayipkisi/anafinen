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
#include <truss_1D/trussTypes/trussLibrary.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace anaf::GUI {

  // Editor and solver front end for truss_imported_or_entered: the model comes from
  // File > Import or is built here node by node and bar by bar. Every edit publishes a new
  // snapshot into bridge.activeMesh (node ids stay 0..n-1) and drops stale results.
  class TrussModelEditor : public IPanel {
  private:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    std::array<double, 3> m_newNode{0.0, 0.0, 0.0};
    std::array<double, 3> m_nodePosition{0.0, 0.0, 0.0};
    std::uint32_t m_barNodeA{0};
    std::uint32_t m_barNodeB{1};
    std::uint32_t m_materialID{0};       // stable Material ID, resolved to an index on use
    double m_areaCm2{80.0};              // entered in cm^2, stored in m^2 like the SQPT panel
    std::uint32_t m_selectedBar{kNone};  // index into MeshData::trussElements
    std::array<double, 3> m_force{0.0, 0.0, 0.0};
    std::array<bool, 3> m_fixed{false, false, false};
    std::uint32_t m_loadedNode{kNone};   // node whose values are in the inputs above
    std::string m_status;                // last edit result shown under the summary
    bool m_statusIsError{false};

    // Whole model: one section and material for every bar at once.
    std::uint32_t m_wholeMaterialID{0};
    double m_wholeAreaCm2{80.0};
    bool m_includeWireframe{false};      // also turn wireframe edges into bars

    // Built-in library (assets/objects/truss/truss1D), read once on first use.
    bool m_libraryRead{false};
    std::filesystem::path m_libraryDir;
    std::vector<FEM::TRUSS::LIBRARY::Entry> m_library;
    std::string m_libraryError;
    int m_librarySelected{0};

    void setStatus(std::string message, bool error);
    void syncSelection(std::uint32_t selectedNode);
    void readLibrary();
    void renderLibrary();
    void renderWholeModel();
    void renderSummary();
    void renderNodes(std::uint32_t selectedNode);
    void renderBars();
    void renderSupportsAndLoads(std::uint32_t selectedNode);
    void renderSolve();

  public:
    ~TrussModelEditor() override = default;

    std::function<void()> onOpenMaterialHandler;
    std::function<void()> onRequestImport;
    // Imports a built-in model file (the file is only read; edits stay in memory).
    std::function<void(const std::filesystem::path&)> onLoadBuiltin;

    // Back to the default inputs and no selection. The model itself lives in the bridge and
    // is cleared with Gui_Calc_Bridge::resetModel().
    void resetState();
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
