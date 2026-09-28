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

#include "framebuffer.hpp"
#include <log/anaf_info.hpp>

#include <algorithm>
#include <cstdint>

namespace anaf::GUI {

  void Framebuffer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || (width == m_width_ && height == m_height_ && m_fbo_)) {
      return;
    }

    m_width_ = width;
    m_height_ = height;
    const auto w = static_cast<GLsizei>(m_width_);
    const auto h = static_cast<GLsizei>(m_height_);

    GLint maxSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_samples_ = std::min(4, maxSamples);

    // Assigning new handles deletes the previous objects. Immutable storage
    // (glTextureStorage2D) cannot be resized, so every resize recreates the attachments.

    // Resolve FBO: single-sample textures, sampled by ImGui::Image and by readEntityID().
    m_texture_ = createTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_texture_.get(), 1, GL_RGBA8, w, h);
    glTextureParameteri(m_texture_.get(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_texture_.get(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_texture_.get(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_texture_.get(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_entity_tex_ = createTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_entity_tex_.get(), 1, GL_R32I, w, h);
    glTextureParameteri(m_entity_tex_.get(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_entity_tex_.get(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    m_fbo_ = createFramebuffer();
    glNamedFramebufferTexture(m_fbo_.get(), GL_COLOR_ATTACHMENT0, m_texture_.get(), 0);
    glNamedFramebufferTexture(m_fbo_.get(), GL_COLOR_ATTACHMENT1, m_entity_tex_.get(), 0);

    const GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glNamedFramebufferDrawBuffers(m_fbo_.get(), 2, drawBuffers);

    if (glCheckNamedFramebufferStatus(m_fbo_.get(), GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      anaf::LOG::error("Resolve framebuffer is not complete");
    }

    // MSAA FBO: actual render target, smooths line/edge aliasing via multisampling.
    m_msaa_color_rbo_ = createRenderbuffer();
    glNamedRenderbufferStorageMultisample(m_msaa_color_rbo_.get(), m_samples_, GL_RGBA8, w, h);

    // Entity ID stays multisampled too (all attachments in one FBO must share the sample count);
    // it is resolved with a NEAREST blit below so IDs are never blended/averaged.
    m_msaa_entity_rbo_ = createRenderbuffer();
    glNamedRenderbufferStorageMultisample(m_msaa_entity_rbo_.get(), m_samples_, GL_R32I, w, h);

    m_msaa_depth_rbo_ = createRenderbuffer();
    glNamedRenderbufferStorageMultisample(m_msaa_depth_rbo_.get(), m_samples_, GL_DEPTH24_STENCIL8, w, h);

    m_msaa_fbo_ = createFramebuffer();
    glNamedFramebufferRenderbuffer(m_msaa_fbo_.get(), GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaa_color_rbo_.get());
    glNamedFramebufferRenderbuffer(m_msaa_fbo_.get(), GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, m_msaa_entity_rbo_.get());
    glNamedFramebufferRenderbuffer(m_msaa_fbo_.get(), GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaa_depth_rbo_.get());
    glNamedFramebufferDrawBuffers(m_msaa_fbo_.get(), 2, drawBuffers);

    if (glCheckNamedFramebufferStatus(m_msaa_fbo_.get(), GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      anaf::LOG::error("MSAA framebuffer is not complete");
    }
  }

  void Framebuffer::clear(float r, float g, float b, float a, int entityClearValue) const {
    const GLfloat color[4] = { r, g, b, a };
    glClearNamedFramebufferfv(m_msaa_fbo_.get(), GL_COLOR, 0, color);
    glClearNamedFramebufferiv(m_msaa_fbo_.get(), GL_COLOR, 1, &entityClearValue);
    glClearNamedFramebufferfi(m_msaa_fbo_.get(), GL_DEPTH_STENCIL, 0, 1.0f, 0);
  }

  void Framebuffer::resolve() const {
    const auto w = static_cast<GLint>(m_width_);
    const auto h = static_cast<GLint>(m_height_);

    // Color: box-filtered by the multisample resolve itself; NEAREST is fine since sizes match.
    glNamedFramebufferReadBuffer(m_msaa_fbo_.get(), GL_COLOR_ATTACHMENT0);
    glNamedFramebufferDrawBuffer(m_fbo_.get(), GL_COLOR_ATTACHMENT0);
    glBlitNamedFramebuffer(m_msaa_fbo_.get(), m_fbo_.get(), 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Entity ID: integer format forbids anything but NEAREST, which also avoids blending IDs.
    glNamedFramebufferReadBuffer(m_msaa_fbo_.get(), GL_COLOR_ATTACHMENT1);
    glNamedFramebufferDrawBuffer(m_fbo_.get(), GL_COLOR_ATTACHMENT1);
    glBlitNamedFramebuffer(m_msaa_fbo_.get(), m_fbo_.get(), 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }

  int Framebuffer::readEntityID(int x, int y) const {
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= m_width_ || static_cast<std::uint32_t>(y) >= m_height_) {
      return -1;
    }
    // Reads straight from the resolved texture: no framebuffer or read-buffer binding involved.
    int pixelData = -1;
    glGetTextureSubImage(m_entity_tex_.get(), 0, x, y, 0, 1, 1, 1, GL_RED_INTEGER, GL_INT, sizeof(pixelData), &pixelData);
    return pixelData;
  }

} // namespace anaf::GUI end
