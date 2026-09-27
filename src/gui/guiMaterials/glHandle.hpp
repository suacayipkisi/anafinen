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

#include <utility>

#include <glad/gl.h>

namespace anaf::GUI {

  // Move-only owner of a single OpenGL object name. The object is deleted when the
  // handle is destroyed or reset, so the GL context must still be current at that point.
  template <typename Deleter>
  class GlHandle {
  private:
    GLuint m_id {0};

  public:
    GlHandle() = default;
    explicit GlHandle(GLuint id) noexcept : m_id(id) {}

    ~GlHandle() { reset(); }

    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;

    GlHandle(GlHandle&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}

    GlHandle& operator=(GlHandle&& other) noexcept {
      if (this != &other) {
        reset(std::exchange(other.m_id, 0));
      }
      return *this;
    }

    void reset(GLuint id = 0) noexcept {
      if (m_id != 0) {
        Deleter{}(m_id);
      }
      m_id = id;
    }

    [[nodiscard]] GLuint get() const noexcept { return m_id; }
    explicit operator bool() const noexcept { return m_id != 0; }
  };

  namespace detail {
    struct BufferDeleter       { void operator()(GLuint id) const noexcept { glDeleteBuffers(1, &id); } };
    struct VertexArrayDeleter  { void operator()(GLuint id) const noexcept { glDeleteVertexArrays(1, &id); } };
    struct TextureDeleter      { void operator()(GLuint id) const noexcept { glDeleteTextures(1, &id); } };
    struct FramebufferDeleter  { void operator()(GLuint id) const noexcept { glDeleteFramebuffers(1, &id); } };
    struct RenderbufferDeleter { void operator()(GLuint id) const noexcept { glDeleteRenderbuffers(1, &id); } };
    struct ShaderDeleter       { void operator()(GLuint id) const noexcept { glDeleteShader(id); } };
    struct ProgramDeleter      { void operator()(GLuint id) const noexcept { glDeleteProgram(id); } };
  } // namespace detail end

  using GlBuffer       = GlHandle<detail::BufferDeleter>;
  using GlVertexArray  = GlHandle<detail::VertexArrayDeleter>;
  using GlTexture      = GlHandle<detail::TextureDeleter>;
  using GlFramebuffer  = GlHandle<detail::FramebufferDeleter>;
  using GlRenderbuffer = GlHandle<detail::RenderbufferDeleter>;
  using GlShader       = GlHandle<detail::ShaderDeleter>;
  using GlProgram      = GlHandle<detail::ProgramDeleter>;

  // DSA factories (OpenGL 4.5+): glCreate* returns fully initialized objects,
  // so no bind is needed before they can be configured.
  [[nodiscard]] inline GlBuffer createBuffer() {
    GLuint id = 0;
    glCreateBuffers(1, &id);
    return GlBuffer{id};
  }

  [[nodiscard]] inline GlVertexArray createVertexArray() {
    GLuint id = 0;
    glCreateVertexArrays(1, &id);
    return GlVertexArray{id};
  }

  [[nodiscard]] inline GlTexture createTexture(GLenum target) {
    GLuint id = 0;
    glCreateTextures(target, 1, &id);
    return GlTexture{id};
  }

  [[nodiscard]] inline GlFramebuffer createFramebuffer() {
    GLuint id = 0;
    glCreateFramebuffers(1, &id);
    return GlFramebuffer{id};
  }

  [[nodiscard]] inline GlRenderbuffer createRenderbuffer() {
    GLuint id = 0;
    glCreateRenderbuffers(1, &id);
    return GlRenderbuffer{id};
  }

} // namespace anaf::GUI end
