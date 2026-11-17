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
#include <optional>
#include <string>

namespace anaf::GUI {

  // "Properties" panel of a BeamFrame model, driven by the selection (viewport or Model Tree):
  //
  //   nothing selected   model summary, self weight, Assign to All Elements..., start hints
  //   a node             position, support (presets, translations, rotations), nodal load, results
  //   an element         nodes, length, material, section, formulation, orientation, end releases,
  //                      distributed loads, results
  //
  // Creating nodes and elements are commands (toolbar, Add Node / Add Element windows); built-in
  // models, solving and the analysis settings live in their own windows. Every edit publishes a new
  // snapshot into bridge.activeBeamMesh and drops stale results.
  class BeamModelEditor : public IPanel {
  private:
    static constexpr std::uint32_t noSelection = std::numeric_limits<std::uint32_t>::max();

    // What the panel shows: the latest valid selection, else the model.
    enum class E_Focus : std::uint8_t { Model, Node, Element };

    // Input of one support group (translations or rotations). Both modes end up as a basis
    // of allowed directions (allowedBasis()), the only form the node stores.
    struct SupportInput {
      bool inclined{false};
      std::array<bool, 3> fixed{false, false, false};  // global axes mode
      bool vectorsRestrained{true};                     // inclined mode: vectors = restrained / allowed
      int vectorCount{1};
      std::array<std::array<double, 3>, 3> vectors{{{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}};

      // Allowed directions, or nothing (with error set) for zero / dependent vectors.
      std::optional<FEM::SUPPORT::Basis> allowedBasis(std::string& error) const;
      // Shows a node's basis: global axes when it is one, inclined vectors otherwise.
      void load(const FEM::SUPPORT::Basis& allowed);
      void setGlobal(bool fixAll);
    };

    // Element properties: the selected element's in the panel, the next element's in Add Element.
    struct ElementInput {
      std::uint32_t materialID{0}; // stable IDs, resolved to indices on use
      std::uint32_t sectionID{0};
      int formulation{0};          // FEM::BEAM::E_Formulation
      std::array<double, 3> orientation{0.0, 0.0, 0.0};
      unsigned int releases{0};    // FEM::BEAM::RELEASE bits
      bool customReleases{false};  // the 12 check boxes are shown although the bits match a preset
    };

    E_Focus m_focus{E_Focus::Model};
    std::uint32_t m_loadedNode{noSelection};    // node whose values are in the node inputs
    std::uint32_t m_loadedElement{noSelection}; // element whose values are in m_element

    // Selected node.
    std::array<double, 3> m_nodePosition{0.0, 0.0, 0.0};
    SupportInput m_motion;
    SupportInput m_rotation;
    std::array<double, 3> m_force{0.0, 0.0, 0.0};
    std::array<double, 3> m_moment{0.0, 0.0, 0.0};

    // Selected element.
    ElementInput m_element;
    std::array<double, 3> m_distributed{0.0, 0.0, 0.0};
    int m_distributedFrame{0}; // FEM::BEAM::E_LoadFrame

    // Add Node / Add Element / Assign to All Elements windows.
    bool m_addNodeOpen{false};
    std::array<double, 3> m_newNode{0.0, 0.0, 0.0};
    bool m_addElementOpen{false};
    std::uint32_t m_newNodeA{0};
    std::uint32_t m_newNodeB{1};
    ElementInput m_newElement;
    bool m_assignAllOpen{false};
    std::uint32_t m_wholeMaterialID{0};
    std::uint32_t m_wholeSectionID{0};
    int m_wholeFormulation{0};

    std::string m_status;
    bool m_statusIsError{false};

    void setStatus(std::string message, bool error);
    void syncSelection(std::uint32_t node, std::uint32_t element);
    void renderHeader(std::uint32_t node, std::uint32_t element);
    void renderModel();
    void renderNode(std::uint32_t node, bool withLoads);
    // Inputs of one group (a property grid group titled title); returns its allowed basis or
    // nothing when the input is invalid, also while the group is folded.
    std::optional<FEM::SUPPORT::Basis> renderSupportGroup(const char* id, const char* title, const char* axisNames, SupportInput& input);
    void renderElement(std::uint32_t element, bool withLoads);
    // Material, section, formulation, orientation and end release rows of a property grid.
    void elementRows(ElementInput& input, const char* id);
    void renderAddNodeWindow();
    void renderAddElementWindow();
    void renderAssignAllWindow();

  public:
    ~BeamModelEditor() override = default;

    std::function<void()> onOpenMaterialHandler;
    std::function<void()> onOpenSectionHandler;
    std::function<void()> onOpenLibrary;  // Open Example...
    std::function<void()> onOpenDiagrams; // Beam Diagrams of the selected element

    // Commands (toolbar, menus, Model Tree).
    void openAddNode();
    void openAddElement();
    void openAssignAll();
    void removeAllReleases();
    // Starts the static solve of the active model; false when there is nothing to solve.
    bool solve();
    // A 3D portal frame built in code (HEB 200 columns, IPE 300 girder).
    void loadExample();

    // Back to the default inputs. The model itself is cleared with GuiCalcBridge::resetModel().
    void resetState();
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
