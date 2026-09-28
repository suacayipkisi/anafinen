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

#include <cstdint>

#include <glad/gl.h>

#include "glHandle.hpp"

namespace anaf::GUI {

  // Move-only: every GL object is owned by a GlHandle, so copies are rejected at compile time.
  class Framebuffer {
  private:
    // Resolve targets: single-sample, used for ImGui display and entity-ID picking.
    GlFramebuffer m_fbo_;
    GlTexture m_texture_;
    GlTexture m_entity_tex_;

    // MSAA targets: actual render destination, resolved into the above after each frame.
    GlFramebuffer m_msaa_fbo_;
    GlRenderbuffer m_msaa_color_rbo_;
    GlRenderbuffer m_msaa_entity_rbo_;
    GlRenderbuffer m_msaa_depth_rbo_;
    int m_samples_ {4};

    std::uint32_t m_width_ {};
    std::uint32_t m_height_ {};

    // Blits the MSAA attachments into the single-sample resolve targets. The MSAA FBO must
    // not be bound as the draw framebuffer here: radeonsi then blits stale samples (the last
    // blended draws are missing). unbind() guarantees that ordering.
    void resolve() const;

  public:
    Framebuffer(std::uint32_t width, std::uint32_t height) {
      resize(width, height);
    }

    // Rendering happens into the MSAA framebuffer; resolve() (called from unbind) blits it down.
    void bind() const {
      glBindFramebuffer(GL_FRAMEBUFFER, m_msaa_fbo_.get());
      glViewport(0, 0, static_cast<GLsizei>(m_width_), static_cast<GLsizei>(m_height_));
    }

    void unbind() const {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      resolve();
    }

    // Clears the MSAA color, entity-ID and depth attachments. Integer attachments cannot be
    // cleared by glClear, so each attachment is cleared with its own typed call.
    void clear(float r, float g, float b, float a, int entityClearValue = -1) const;

    void resize(std::uint32_t width, std::uint32_t height);

    // Reads the entity ID (attachment 1) at framebuffer pixel (x, y), origin bottom-left.
    // Returns -1 when the pixel is outside the framebuffer or nothing was drawn there.
    int readEntityID(int x, int y) const;

    std::uint32_t getTextureID() const { return m_texture_.get();}
    std::uint32_t getWidth() const { return m_width_;}
    std::uint32_t getHeight() const { return m_height_;}

  };

} // namespace anaf::GUI end
