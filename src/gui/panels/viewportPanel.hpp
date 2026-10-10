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
#include <guiMaterials/framebuffer.hpp>
#include <bridge/generalStatus.hpp>
#include "beamSceneRenderer.hpp"
#include "viewportRenderer.hpp"
#include "viewportToolbar.hpp"

#include "imgui.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace anaf::GUI{

  struct Truss1DGuiProperties {
    bool meshNeedsUpdate{true};
    std::uint64_t lastRenderedVersion{0};
  };

  class ViewportPanel : public IPanel {
  private:
    static constexpr std::uint32_t noSelection = std::numeric_limits<std::uint32_t>::max();

    std::shared_ptr<Framebuffer> m_fbo ;
    std::unique_ptr<ViewportRenderer> m_renderer;
    std::unique_ptr<BeamSceneRenderer> m_beamRenderer; // beam sections and node spheres

    bool m_viewportHovered {false};

    float m_rotationYaw {0.9f};
    float m_rotationPitch {-0.7f};
    float m_cameraDistance {18.0f};
    glm::vec3 m_target{0.0f, 0.0f, 0.0f};
    glm::vec3 m_sceneCenter{0.0f, 0.0f, 0.0f};
    float m_sceneRadius {10.0f};

    bool m_draggingView {false};
    bool m_fitRequested {false};
    std::shared_ptr<ViewportDisplayOptions> m_display; // toggles set in ViewportToolbar
    ImVec2 m_viewportSize{0.0f, 0.0f};

    Truss1DGuiProperties m_truss1dGuiProp{};

    std::shared_ptr<const anaf::BRIDGE::MeshData> m_currentMesh{nullptr};
    std::shared_ptr<const anaf::BRIDGE::BeamMeshData> m_currentBeamMesh{nullptr};
    double m_deformScale{1.0}; // bridge.deformScale, read together with the snapshot
    double m_cachedMaxStress{0.0};
    double m_cachedMaxDisp{0.0};
    std::uint32_t m_themeRevision{0}; // THEME::themeRevision() the buffers were built with
    std::vector<std::array<ImVec2, 2>> m_legendRects; // legend cards drawn last frame (min, max), block picking
    bool m_legendDragged{false}; // the active legend header moved, so its release is no click
    std::uint32_t m_selectedNode{noSelection};    // bridge selections, compared every frame so a
    std::uint32_t m_selectedElement{noSelection}; // selection made in a panel redraws the highlight
    std::vector<std::pair<std::uint32_t, glm::vec3>> m_nodeLabels; // id, drawn position

    // Beam scene: every element sampled at stations along it (exact displacement field, values
    // for the coloring), rebuilt when the snapshot changes; the drawn shape (positions and
    // rotated section frames) is derived from it for the deformation scale.
    struct BeamDrawElement {
      std::vector<glm::vec3> base;       // undeformed station positions, node 1 to node 2
      std::vector<glm::vec3> offset;     // displacement per station (m, unscaled); empty without results
      std::vector<float> xi;             // station position x / L
      glm::vec3 rotation0{0.0f};         // nodal rotations in local axes (rad, unscaled)
      glm::vec3 rotation1{0.0f};
      std::vector<float> stress;         // von Mises per station (Pa); empty without stresses
      std::vector<float> displacement;   // |u| per station (m); empty without results
      glm::vec3 axisX{0.0f};             // undeformed local axes
      glm::vec3 axisY{0.0f};
      glm::vec3 axisZ{0.0f};
      std::vector<glm::vec3> stations;   // drawn positions (with the deformation scale)
      std::vector<glm::vec3> frameY;     // drawn section axes per station: twist and bending
      std::vector<glm::vec3> frameZ;     // rotation applied, scaled like the displacements
      int fullMesh{-1};                  // the real section
      int simpleMesh{-1};                // box or cylinder of the same size (level of detail)
      int pinMesh{-1};                   // end release pin (a thin cylinder sized from the section)
      int collarMesh{-1};                // end release collar (torsion), a ring around the section
      float halfSize{0.0f};              // largest distance of the outline from the axis (m)
      std::uint16_t releases{0};         // FEM::BEAM::RELEASE bits of the element
    };
    std::vector<BeamDrawElement> m_beamElements;
    int m_lineMesh{-1};
    std::shared_ptr<const anaf::BRIDGE::BeamMeshData> m_stationsMesh; // what m_beamElements was built from
    double m_stationsScale{-1.0}; // deformation scale of the drawn shape
    bool m_lodActive{false};          // many elements: far ones as boxes / cylinders / lines
    glm::mat4 m_lodMatrix{0.0f};      // camera of the last level-of-detail pass
    ImVec2 m_lodViewport{0.0f, 0.0f};

    bool hasModel() const;
    void updateSceneBounds();
    void resetCamera();
    // Orbit camera basis: unit direction from the target to the eye, and the camera up vector.
    // Up follows the pitch, so the camera can orbit over the poles without a pitch limit.
    glm::vec3 orbitDirection() const;
    glm::vec3 orbitUp() const;
    float farPlane() const;
    void handleCameraInput();
    void buildSceneBatches();
    void buildTrussScene();
    void buildBeamStations();
    void applyBeamDeformation(); // drawn positions and frames of m_beamElements for m_deformScale
    void buildBeamScene();
    // Instances of every beam element for the current camera (level of detail by on-screen size).
    void pushBeamInstances(const glm::mat4& mvp);
    void renderOverlay2D(const ImVec2& origin, const ImVec2& size);

  public:
    ViewportPanel(std::shared_ptr<Framebuffer> fbo, std::shared_ptr<ViewportDisplayOptions> display);
    ~ViewportPanel() override = default;

    void renderSceneOpenGL();
    void onImGuiRender() override;

    // Frames the camera on the next mesh that is loaded (e.g. after an import).
    void requestFit() { m_fitRequested = true; }
    glm::mat4 getViewProjectionMatrix() const;

  };

} // namespace anaf::GUI end
