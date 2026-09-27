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

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <guiMaterials/glHandle.hpp>

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


  // Move-only: every GL object is owned by a GlHandle, so copies are rejected at compile time.
  class ViewportRenderer {
  private:
    GlProgram m_program;
    GlVertexArray m_lineVao;
    GlBuffer m_lineVbo;
    GlVertexArray m_glowLineVao;
    GlBuffer m_glowLineVbo;
    GlVertexArray m_pointVao;
    GlBuffer m_pointVbo;
    GLint m_mvpLoc{-1};

    GlProgram m_gridProgram;
    GlVertexArray m_gridVao;
    GlBuffer m_gridVbo;
    GLint m_gridMvpLoc{-1};
    GLint m_gridSpacingLoc{-1};
    GLint m_gridAxisGapLoc{-1};

    GlProgram m_textProgram;
    GlVertexArray m_textVao;
    GlBuffer m_textVbo;

    std::vector<Vertex3D> m_lineBuffer;
    std::vector<Vertex3D> m_glowLineBuffer;
    std::vector<Point3D> m_pointBuffer;
    std::vector<TextVertex> m_textBuffer;

    GLsizei m_lineVertexCount{0};
    GLsizei m_glowLineVertexCount{0};
    GLsizei m_pointVertexCount{0};
    GLsizei m_textVertexCount{0};

    void compileShaders();

    void compileGridShader();

    void compileTextShader();

  public:
    ViewportRenderer();

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

    void renderGrid(const glm::mat4& mvp, float spacing);

    void renderText();
  };

} // namespace anaf::GUI end
