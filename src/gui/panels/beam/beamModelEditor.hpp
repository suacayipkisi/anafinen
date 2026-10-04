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
#include <panels/dynamicAnalysisInputs.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace anaf::GUI {

  // Editor and solver front end for beam_frame: nodes, beam elements (material, section,
  // formulation, orientation, end releases / hinges), supports (6 DOFs), nodal and distributed loads, self weight.
  // Every edit publishes a new snapshot into bridge.activeBeamMesh and drops stale results.
  class BeamModelEditor : public IPanel {
  private:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    // Input of one support group (translations or rotations). Both modes end up as a basis
    // of allowed directions (allowedBasis()), the only form the node stores.
    struct SupportInput {
      bool inclined{false};
      std::array<bool, 3> fixed{false, false, false};  // global axes mode
      bool vectorsRestrained{true};                     // inclined mode: vectors = restrained / allowed
      int vectorCount{1};
      std::array<std::array<double, 3>, 3> vectors{{{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}};

      // Allowed directions, or nothing (with error set) for zero / dependent vectors.
      std::optional<std::vector<std::array<double, 3>>> allowedBasis(std::string& error) const;
      // Shows a node's basis: global axes when it is one, inclined vectors otherwise.
      void load(const std::vector<std::array<double, 3>>& allowed);
      void setGlobal(bool fixAll);
    };

    std::array<double, 3> m_newNode{0.0, 0.0, 0.0};
    std::array<double, 3> m_nodePosition{0.0, 0.0, 0.0};
    SupportInput m_motion;
    SupportInput m_rotation;
    std::array<double, 3> m_force{0.0, 0.0, 0.0};
    std::array<double, 3> m_moment{0.0, 0.0, 0.0};
    std::uint32_t m_loadedNode{kNone};    // node whose values are in the inputs above

    std::uint32_t m_elementNodeA{0};
    std::uint32_t m_elementNodeB{1};
    std::uint32_t m_materialID{0};        // stable IDs, resolved to indices on use
    std::uint32_t m_sectionID{0};
    int m_formulation{0};                 // FEM::BEAM::Formulation
    std::array<double, 3> m_orientation{0.0, 0.0, 0.0};
    unsigned int m_releases{0};           // FEM::BEAM::RELEASE bits
    std::array<double, 3> m_distributed{0.0, 0.0, 0.0};
    int m_distributedFrame{0};            // FEM::BEAM::LoadFrame
    std::uint32_t m_loadedElement{kNone};

    std::uint32_t m_wholeMaterialID{0};
    std::uint32_t m_wholeSectionID{0};
    int m_wholeFormulation{0};

    std::string m_status;
    bool m_statusIsError{false};
    DynamicAnalysisInputs m_dynamic; // shown for LoadKind::dynamic

    // Built-in library (assets/objects/beam/beam3D), read once on first use.
    bool m_libraryRead{false};
    std::filesystem::path m_libraryDir;
    std::vector<FEM::BEAM::LIBRARY::Entry> m_library;
    std::string m_libraryError;
    int m_librarySelected{0};

    void setStatus(std::string message, bool error);
    void syncSelection(std::uint32_t node, std::uint32_t element);
    void renderSummary();
    void renderNodes(std::uint32_t node);
    // withLoads false (dynamic load kind): supports only.
    void renderSupportsAndLoads(std::uint32_t node, bool withLoads);
    // Inputs of one group; returns its allowed basis or nothing when the input is invalid.
    std::optional<std::vector<std::array<double, 3>>> renderSupportGroup(const char* id, const char* axisNames, SupportInput& input);
    void renderElements(std::uint32_t element);
    void renderReleaseInputs();
    void renderElementLoads(std::uint32_t element);
    void renderWholeModel();
    void renderSolve();
    // Load Example Frame / Clear Model, shown for both load kinds.
    void renderModelButtons();
    void loadExample();
    void readLibrary();
    void renderLibrary();

  public:
    ~BeamModelEditor() override = default;

    std::function<void()> onOpenMaterialHandler;
    std::function<void()> onOpenSectionHandler;
    // Imports a built-in model or solved-result file (the file is only read; edits stay in memory).
    std::function<void(const std::filesystem::path&)> onLoadBuiltin;

    // Back to the default inputs. The model itself is cleared with Gui_Calc_Bridge::resetModel().
    void resetState();
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
