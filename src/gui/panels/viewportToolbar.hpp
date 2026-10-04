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

#include <cstdint>
#include <memory>

namespace anaf::GUI {

  enum class NodeStyle : std::uint8_t {
    Off,    // no nodes (no node picking, no labels)
    Square, // screen-space squares of constant pixel size
    Sphere  // 3D spheres, thicker than the elements at the node
  };

  enum class SupportStyle : std::uint8_t {
    Off,     // inclined supports only (their plane or line)
    Symbols, // textbook symbols by support type: pin, roller, slider, clamp, hinge axles
    Dof      // CAD style: a cone per restrained translation, a double cone per restrained rotation
  };

  enum class ElementColoring : std::uint8_t {
    Off,         // one color for every element
    Stress,      // truss: |axial stress|; beam: von Mises along the element
    Displacement // displacement magnitude (beam: along the element)
  };

  // What the viewport draws, written by ViewportToolbar and read by ViewportPanel (GUI thread only).
  struct ViewportDisplayOptions {
    bool showGrid{false};   // procedural x-z ground grid
    bool showAxes{false};   // 3D X / Y / Z axis lines (the corner gizmo is always drawn)
    NodeStyle nodeStyle{NodeStyle::Square};             // nodes, labels, picking, displacement colorbar
    bool showForces{true};  // applied force and moment arrows, distributed loads
    SupportStyle supportStyle{SupportStyle::Off};
    ElementColoring coloring{ElementColoring::Stress};  // element colors and their colorbar

    bool showNodes() const { return nodeStyle != NodeStyle::Off; }

    bool changed{false};              // set on every toggle; the viewport rebuilds its batches and clears it
    bool resetCameraRequested{false}; // the viewport fits the camera and clears it
  };

  // Own window, docked by MainDockSpaceHost as a fixed-height strip above the viewport.
  class ViewportToolbar : public IPanel {
  public:
    static constexpr const char* kWindowName = "Viewport Toolbar";
    // Height of the docked strip: one button row plus the window padding.
    static float windowHeight();

    // Shown only while the viewport it belongs to is open.
    ViewportToolbar(std::shared_ptr<ViewportDisplayOptions> options, const IPanel* viewport);
    void onImGuiRender() override;

  private:
    std::shared_ptr<ViewportDisplayOptions> m_options;
    const IPanel* m_viewport;
  };

} // namespace anaf::GUI end
