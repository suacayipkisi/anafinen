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
#include <common/supportBasis.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>

namespace anaf::GUI {

  // "Properties" panel of a TrussImportedOrEntered model (File > Import or built node by node),
  // driven by the selection (viewport for nodes, Model Tree for nodes and bars):
  //
  //   nothing selected   model summary, Assign to All Bars..., start hints
  //   a node             position, support (global axes or inclined vectors), nodal load, results
  //   a bar              nodes, length, material, area, results
  //
  // Add Node / Add Bar are command windows. Every edit publishes a new snapshot into
  // bridge.activeMesh (node ids stay 0..n-1) and drops stale results.
  class TrussModelEditor : public IPanel {
  private:
    static constexpr std::uint32_t noSelection = std::numeric_limits<std::uint32_t>::max();

    enum class E_Focus : std::uint8_t { Model, Node, Bar };

    E_Focus m_focus{E_Focus::Model};
    std::uint32_t m_loadedNode{noSelection}; // node whose values are in the node inputs
    std::uint32_t m_loadedBar{noSelection};  // bar whose values are in m_materialID / m_areaCm2

    // Selected node.
    std::array<double, 3> m_nodePosition{0.0, 0.0, 0.0};
    std::array<double, 3> m_force{0.0, 0.0, 0.0};
    std::array<bool, 3> m_fixed{false, false, false};
    // Inclined / skewed support: 1..3 direction vectors, read as the restrained directions
    // (support reactions) or as the allowed motion directions; the other set is the complement.
    bool m_supportInclined{false};
    bool m_vectorsRestrained{true};
    int m_supportVectorCount{1};
    std::array<std::array<double, 3>, 3> m_supportVectors{{{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}};

    // Selected bar.
    std::uint32_t m_materialID{0}; // stable Material ID, resolved to an index on use
    double m_areaCm2{80.0};        // entered in cm^2, stored in m^2 like the SQPT panel

    // Add Node / Add Bar / Assign to All Bars windows.
    bool m_addNodeOpen{false};
    std::array<double, 3> m_newNode{0.0, 0.0, 0.0};
    bool m_addBarOpen{false};
    std::uint32_t m_barNodeA{0};
    std::uint32_t m_barNodeB{1};
    std::uint32_t m_newMaterialID{0};
    double m_newAreaCm2{80.0};
    bool m_assignAllOpen{false};
    std::uint32_t m_wholeMaterialID{0};
    double m_wholeAreaCm2{80.0};
    bool m_includeWireframe{false}; // also turn wireframe edges into bars

    std::string m_status;
    bool m_statusIsError{false};

    void setStatus(std::string message, bool error);
    void syncSelection(std::uint32_t node, std::uint32_t bar);
    void renderHeader(std::uint32_t node, std::uint32_t bar);
    void renderModel();
    void renderNode(std::uint32_t node, bool withLoads);
    void renderInclinedSupportInputs();
    void setSupportVectors(const FEM::SUPPORT::Basis& vectors);
    void renderBar(std::uint32_t bar);
    void renderAddNodeWindow();
    void renderAddBarWindow();
    void renderAssignAllWindow();

  public:
    ~TrussModelEditor() override = default;

    std::function<void()> onOpenMaterialHandler;
    std::function<void()> onRequestImport;
    std::function<void()> onOpenLibrary;

    void openAddNode();
    void openAddBar();
    void openAssignAll();
    // Starts the static solve of the active model; false when there is nothing to solve.
    bool solve();

    // Back to the default inputs and no selection. The model itself lives in the bridge and
    // is cleared with GuiCalcBridge::resetModel().
    void resetState();
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
