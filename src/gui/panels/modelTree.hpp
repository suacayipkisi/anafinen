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
#include <memory>
#include <vector>

namespace anaf::GUI {

  class ModelTree : public IPanel {
  private:
    // Filtered rows of the snapshot shown last, rebuilt only when the snapshot changes; the
    // lists are drawn with ImGuiListClipper, so only the visible rows cost anything.
    std::shared_ptr<const anaf::BRIDGE::MeshData> m_indexedMesh;
    std::vector<std::uint32_t> m_supportedNodes;   // node indices with a support
    std::vector<std::uint32_t> m_overstressedBars; // bar indices with |stress| > yield
    std::shared_ptr<const anaf::BRIDGE::BeamMeshData> m_indexedBeamMesh;
    std::vector<std::uint32_t> m_supportedBeamNodes;
    std::vector<std::uint32_t> m_overstressedBeams; // von Mises > yield


    // Boundary conditions, overstressed bars and nodal displacements of the active snapshot.
    // Every object type publishes the same MeshData, so one tree serves all of them.
    void renderMeshTree(anaf::BRIDGE::Gui_Calc_Bridge& bridge);
    // Supports, loads, element stresses and nodal results of the beam snapshot.
    void renderBeamTree(anaf::BRIDGE::Gui_Calc_Bridge& bridge);
  public:
    void onImGuiRender() override;
  };

} // namespace anaf::GUI end
