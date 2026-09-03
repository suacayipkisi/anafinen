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

#include "../guiMaterials/iPanel.hpp"
#include "../guiMaterials/framebuffer.hpp"
#include "imgui.h"

#include <memory>

namespace anaf::GUI{

    class ViewportPanel : public IPanel {
    private:
        std::shared_ptr<Framebuffer> m_fbo_ ;
        bool m_viewportFocused_ {false};
        bool m_viewportHovered_ {false};
        float m_rotationYaw {0.9f};
        float m_rotationPitch {-0.7f};
        float m_cameraDistance {18.0f};
        float m_targetX {0.0f};
        float m_targetY {0.0f};
        float m_targetZ {0.0f};
        bool m_draggingView {false};
        bool m_showNodes {false};
        ImVec2 m_lastMousePos {0.0f, 0.0f};
    public:
        ViewportPanel(std::shared_ptr<Framebuffer> fbo) : m_fbo_(fbo) {}

        void onImGuiRender() override;

        bool isFocused() const { return m_viewportFocused_; }
        bool isHovered() const { return m_viewportHovered_; }
    };

} // namespace anaf::GUI end
