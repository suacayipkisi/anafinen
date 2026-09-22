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

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>

namespace anaf::GUI {

  struct Vertex3D {
    glm::vec3 position;
    glm::vec4 color;
    int entityID {-1};
  };

  struct Point3D {
    glm::vec3 position;
    glm::vec4 color;
    int entityID {-1};
    float size {10.0f};
  };

  // Screen-space glyph quad vertex; position is already in NDC (-1..1).
  struct TextVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
  };


  class ViewportRenderer {
  private:
    GLuint m_program{0};
    GLuint m_lineVao{0}, m_lineVbo{0};
    GLuint m_glowLineVao{0}, m_glowLineVbo{0};
    GLuint m_pointVao{0}, m_pointVbo{0};
    GLint m_mvpLoc{-1};

    GLuint m_textProgram{0};
    GLuint m_textVao{0}, m_textVbo{0};
    GLint m_textSamplerLoc{-1};

    std::vector<Vertex3D> m_lineBuffer;
    std::vector<Vertex3D> m_glowLineBuffer;
    std::vector<Point3D> m_pointBuffer;
    std::vector<TextVertex> m_textBuffer;

    GLsizei m_lineVertexCount{0};
    GLsizei m_glowLineVertexCount{0};
    GLsizei m_pointVertexCount{0};
    GLsizei m_textVertexCount{0};

    void compileShaders();

    void compileTextShader();

  public:
    ViewportRenderer();

    ~ViewportRenderer() {
      if (m_lineVao) glDeleteVertexArrays(1, &m_lineVao);
      if (m_lineVbo) glDeleteBuffers(1, &m_lineVbo);
      if (m_glowLineVao) glDeleteVertexArrays(1, &m_glowLineVao);
      if (m_glowLineVbo) glDeleteBuffers(1, &m_glowLineVbo);
      if (m_pointVao) glDeleteVertexArrays(1, &m_pointVao);
      if (m_pointVbo) glDeleteBuffers(1, &m_pointVbo);
      if (m_textVao) glDeleteVertexArrays(1, &m_textVao);
      if (m_textVbo) glDeleteBuffers(1, &m_textVbo);
      if (m_program) glDeleteProgram(m_program);
      if (m_textProgram) glDeleteProgram(m_textProgram);
    }

    void addLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color, int entityID = -1);

    // Adds a line to a separate additive-blended pass, drawn thicker and translucent to fake a glow/bloom halo.
    void addGlowLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color);

    void addPoint(const glm::vec3& p, const glm::vec4& color, int entityID, float size = 12.0f);

    // Builds a screen-space glyph quad batch using ImGui's already-loaded font atlas as texture.
    // screenPosPixels/fbWidth/fbHeight are in FBO pixel space with origin top-left.
    void addText(
      const glm::vec2& screenPosPixels, 
      const std::string& text, 
      const glm::vec4& color,
      float fbWidth, 
      float fbHeight, 
      float pixelScale = 1.0f
    );

    void clearBuffers();
    void clearTextBuffer();

    void reserve(size_t lineCount, size_t pointCount);

    void uploadCurrentBuffer();
    void uploadTextBuffer();

    void render(const glm::mat4& mvp);

    void renderText();
  };

} // namespace anaf::GUI end
