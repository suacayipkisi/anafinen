// Copyright (c) 2026 Ufuk Deniz Konuk
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
#include "viewportRenderer.hpp"

#include "imgui.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>

namespace anaf::GUI{

    class ViewportPanel : public IPanel {
    private:
        std::shared_ptr<Framebuffer> m_fbo_ ;
        std::unique_ptr<ViewportRenderer> m_renderer_;

        bool m_viewportFocused_ {false};
        bool m_viewportHovered_ {false};

        float m_rotationYaw {0.9f};
        float m_rotationPitch {-0.7f};
        float m_cameraDistance {18.0f};
        glm::vec3 m_target{0.0f, 0.0f, 0.0f};

        bool m_draggingView {false};
        bool m_showNodes {false};
        ImVec2 m_viewportSize{0.0f, 0.0f};

        size_t m_lastNodeCount{0};
        size_t m_lastElementCount{0};
        bool m_meshNeedsUpdate{true};
        uint64_t m_lastRenderedVersion{0};
        float m_lastDeformationScale{1.0f};

        void handleCameraInput();
        void buildSceneBatches();
        void renderOverlay2D(const ImVec2& origin, const ImVec2& size, const glm::mat4& viewProj);

    public:
        explicit ViewportPanel(std::shared_ptr<Framebuffer> fbo);
        ~ViewportPanel() override = default;

        void renderSceneOpenGL();
        void onImGuiRender() override;

        bool isFocused() const { return m_viewportFocused_; }
        bool isHovered() const { return m_viewportHovered_; }
        glm::mat4 getViewProjectionMatrix() const;

    };

} // namespace anaf::GUI end
