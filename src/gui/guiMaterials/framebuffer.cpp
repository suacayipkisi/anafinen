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

#include <algorithm>
#include <cstdint>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <log/anaf_info.hpp>
#include "framebuffer.hpp"

namespace anaf::GUI {

    void Framebuffer::resize(std::uint32_t width, std::uint32_t height) {
        if (width == 0 || height == 0 || (width == m_width_ && height == m_height_ && m_fbo_id_ != 0)) {
            return;
        }

        m_width_ = width;
        m_height_ = height;

        cleanup();

        GLint maxSamples = 4;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        m_samples_ = std::min(4, maxSamples);

        // Resolve FBO: single-sample textures, sampled by ImGui::Image and by readPixel().
        glGenFramebuffers(1, &m_fbo_id_);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_id_);

        glGenTextures(1, &m_texture_id_);
        glBindTexture(GL_TEXTURE_2D, m_texture_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width_, m_height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture_id_, 0);

        glGenTextures(1, &m_entity_tex_id_);
        glBindTexture(GL_TEXTURE_2D, m_entity_tex_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, m_width_, m_height_, 0, GL_RED_INTEGER, GL_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_entity_tex_id_, 0);

        const GLenum resolveDrawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, resolveDrawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            anaf::LOG::error("ERROR: Resolve framebuffer is not complete!");
        }

        // MSAA FBO: actual render target, smooths line/edge aliasing via multisampling.
        glGenFramebuffers(1, &m_msaa_fbo_id_);
        glBindFramebuffer(GL_FRAMEBUFFER, m_msaa_fbo_id_);

        glGenRenderbuffers(1, &m_msaa_color_rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaa_color_rbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples_, GL_RGBA8, m_width_, m_height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaa_color_rbo_);

        // Entity ID stays multisampled too (all attachments in one FBO must share the sample count);
        // it is resolved with a NEAREST blit below so IDs are never blended/averaged.
        glGenRenderbuffers(1, &m_msaa_entity_rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaa_entity_rbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples_, GL_R32I, m_width_, m_height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, m_msaa_entity_rbo_);

        glGenRenderbuffers(1, &m_msaa_depth_rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaa_depth_rbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples_, GL_DEPTH24_STENCIL8, m_width_, m_height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaa_depth_rbo_);

        const GLenum msaaDrawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, msaaDrawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            anaf::LOG::error("ERROR: MSAA framebuffer is not complete!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Framebuffer::resolve() const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaa_fbo_id_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo_id_);

        // Color: box-filtered by the multisample resolve itself; NEAREST is fine since sizes match.
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(0, 0, m_width_, m_height_, 0, 0, m_width_, m_height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // Entity ID: integer format forbids anything but NEAREST, which also avoids blending IDs.
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glDrawBuffer(GL_COLOR_ATTACHMENT1);
        glBlitFramebuffer(0, 0, m_width_, m_height_, 0, 0, m_width_, m_height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    int Framebuffer::readPixel(std::uint32_t attachmentIndex, int x, int y) const {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= m_width_ || static_cast<std::uint32_t>(y) >= m_height_) {
            return -1;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo_id_);
        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
        int pixelData = -1;
        glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        return pixelData;
    }

} // namespace anaf::GUI end
