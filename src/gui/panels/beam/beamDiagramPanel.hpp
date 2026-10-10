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

#include <beam/beamEngine/beamDiagrams.hpp>
#include <beam/beamSection/sectionStress.hpp>
#include <bridge/generalStatus.hpp>
#include <guiMaterials/iPanel.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace anaf::GUI {

  // Diagrams of one solved beam element (ImPlot): internal forces, displacements and
  // stresses along it. Opens docked in the lower half of the Model Tree's dock node.
  class BeamDiagramPanel : public IPanel {
  private:
    static constexpr std::uint32_t noSelection = std::numeric_limits<std::uint32_t>::max();
    static constexpr int sampleCount = 61;

    // Samples of the element shown, rebuilt when the snapshot or the element changes.
    std::shared_ptr<const BRIDGE::BeamMeshData> m_mesh;
    std::uint32_t m_element{noSelection};
    std::vector<FEM::BEAM::SectionState> m_states;
    std::vector<std::optional<FEM::BEAM::SectionStress>> m_stresses;
    std::string m_sectionName;
    std::string m_error;

    int m_quantity{5}; // Mz
    bool m_dockPending{true}; // dock under the Model Tree the next time it is possible
    int m_lastFrame{-10};     // last ImGui frame drawn; a gap means the panel was closed

    void dockUnderModelTree();
    void rebuild(const std::shared_ptr<const BRIDGE::BeamMeshData>& mesh, std::uint32_t element);
    void renderPlot();
    void renderTable();

  public:
    static constexpr const char* windowName = "Beam Diagrams";
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
