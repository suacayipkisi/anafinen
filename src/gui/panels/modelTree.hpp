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

#include <bridge/generalStatus.hpp>
#include <guiMaterials/iPanel.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace anaf::GUI {

  // Browser of the active model (right of the viewport): every row selects what it names, so the
  // Properties panel shows it (nodes, elements / bars, supports, loads, results).
  //
  //   v beam_frame
  //     > Nodes (15)          rows select a node
  //     > Elements (18)       rows select an element; right click: Assign to All..., Remove All End Releases
  //     > Materials (1)       used materials with their element count; a row opens the Material Handler
  //     > Sections (2)        used sections with their shape icon; a row opens the Section Handler
  //     > Supports (6)        support type icon per node
  //     > Loads               self weight, nodal loads (select the node), distributed loads (the element)
  //     > Results             largest displacement / stress (select it), members over yield, diagrams
  class ModelTree : public IPanel {
  private:
    // Rows of the snapshot shown last, rebuilt only when the snapshot changes; the long lists are
    // drawn with ImGuiListClipper, so only the visible rows cost anything.
    struct Usage {
      std::string name;
      std::size_t count{0};
      int shape{-1}; // beam section shape index (FEM::BEAM::SectionShape), -1 for materials
    };
    std::shared_ptr<const FEM::TRUSS::MeshData> m_indexedTrussMesh;
    std::shared_ptr<const FEM::BEAM::MeshData> m_indexedBeamMesh;
    std::vector<std::uint32_t> m_supportedNodes;
    std::vector<std::uint32_t> m_overstressed;  // elements / bars over yield
    std::vector<Usage> m_materials;
    std::vector<Usage> m_sections;
    std::uint32_t m_farthestNode{0};
    std::uint32_t m_worstElement{0};
    double m_maxDisplacement{0.0};
    double m_maxStress{0.0};

    void indexTruss(const std::shared_ptr<const FEM::TRUSS::MeshData>& mesh, anaf::BRIDGE::GuiCalcBridge& bridge);
    void indexBeam(const std::shared_ptr<const FEM::BEAM::MeshData>& mesh, anaf::BRIDGE::GuiCalcBridge& bridge);
    void renderTrussTree(anaf::BRIDGE::GuiCalcBridge& bridge, bool editable);
    void renderBeamTree(anaf::BRIDGE::GuiCalcBridge& bridge);

  public:
    std::function<void()> onAssignAll;         // Assign to All Elements / Bars window of the editor
    std::function<void()> onRemoveAllReleases; // beam
    std::function<void()> onOpenMaterials;
    std::function<void()> onOpenSections;
    std::function<void()> onOpenDiagrams;

    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
