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

        glGenFramebuffers(1, &m_fbo_id_);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_id_);

        // color texture attachment
        glGenTextures(1, &m_texture_id_);
        glBindTexture(GL_TEXTURE_2D, m_texture_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width_, m_height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture_id_, 0);

        // etity ID texture attachment (GL_R32I)
        glGenTextures(1, &m_entity_tex_id_);
        glBindTexture(GL_TEXTURE_2D, m_entity_tex_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, m_width_, m_height_, 0, GL_RED_INTEGER, GL_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_entity_tex_id_, 0);

        // depth and stencil attachment
        glGenRenderbuffers(1, &m_rbo_id_);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rbo_id_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width_, m_height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo_id_);


        // specify both attachments as draw targets
        const GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, drawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            anaf::LOG::error("ERROR: Framebuffer is not complete!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
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
