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

#include "viewportRenderer.hpp"

#include <glad/gl.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

#include <string>
#include <vector>
#include <cstddef>

namespace anaf::GUI {

  void ViewportRenderer::compileShaders() {
    const char* vertexShaderSource = R"(
      #version 460 core
      layout (location = 0) in vec3 aPos;
      layout (location = 1) in vec4 aColor;
      layout (location = 2) in int aEntityID;
      layout (location = 3) in float aPointSize;

      uniform mat4 u_MVP;

      out vec4 vColor;
      flat out int vEntityID;

      void main() {
        vColor = aColor;
        vEntityID = aEntityID;
        gl_PointSize = (aPointSize > 0.0) ? aPointSize : 1.0;
        gl_Position = u_MVP * vec4(aPos, 1.0);
      }
    )";

    const char* fragmentShaderSource = R"(
      #version 460 core
      layout (location = 0) out vec4 FragColor;
      layout (location = 1) out int EntityID;

      in vec4 vColor;
      flat in int vEntityID;

      void main() {
        FragColor = vColor;
        EntityID = vEntityID;
      }
    )";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fs);

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_mvpLoc = glGetUniformLocation(m_program, "u_MVP");
  }

  void ViewportRenderer::compileGridShader() {
    const char* vertexShaderSource = R"(
      #version 460 core
      layout (location = 0) in vec3 aPos;

      uniform mat4 u_MVP;

      out vec3 vWorldPosition;

      void main() {
        vWorldPosition = aPos;
        gl_Position = u_MVP * vec4(aPos, 1.0);
      }
    )";

    const char* fragmentShaderSource = R"(
      #version 460 core
      layout (location = 0) out vec4 FragColor;
      layout (location = 1) out int EntityID;

      in vec3 vWorldPosition;

      uniform float u_GridSpacing;
      uniform float u_AxisGap;

      void main() {
        if (abs(vWorldPosition.x) < u_AxisGap || abs(vWorldPosition.z) < u_AxisGap) discard;

        vec2 gridPosition = vWorldPosition.xz / u_GridSpacing;
        vec2 distanceToLine = abs(fract(gridPosition - 0.5) - 0.5);
        vec2 lineWidth = max(fwidth(gridPosition), vec2(0.001));
        float lineDistance = min(distanceToLine.x / lineWidth.x, distanceToLine.y / lineWidth.y);
        float lineAlpha = 1.0 - smoothstep(0.0, 1.0, lineDistance);

        if (lineAlpha <= 0.0) discard;
        FragColor = vec4(0.62, 0.70, 0.78, lineAlpha * 0.11);
        EntityID = -1;
      }
    )";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fs);

    m_gridProgram = glCreateProgram();
    glAttachShader(m_gridProgram, vs);
    glAttachShader(m_gridProgram, fs);
    glLinkProgram(m_gridProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_gridMvpLoc = glGetUniformLocation(m_gridProgram, "u_MVP");
    m_gridSpacingLoc = glGetUniformLocation(m_gridProgram, "u_GridSpacing");
    m_gridAxisGapLoc = glGetUniformLocation(m_gridProgram, "u_AxisGap");
  }

  void ViewportRenderer::compileTextShader() {
    const char* vertexShaderSource = R"(
      #version 460 core
      layout (location = 0) in vec2 aPos;
      layout (location = 1) in vec2 aUV;
      layout (location = 2) in vec4 aColor;

      out vec2 vUV;
      out vec4 vColor;

      void main() {
        vUV = aUV;
        vColor = aColor;
        gl_Position = vec4(aPos, 0.0, 1.0);
      }
    )";

    const char* fragmentShaderSource = R"(
      #version 460 core
      layout (location = 0) out vec4 FragColor;
      layout (location = 1) out int EntityID;

      in vec2 vUV;
      in vec4 vColor;

      uniform sampler2D u_FontTex;

      void main() {
        float alpha = texture(u_FontTex, vUV).r;
        if (alpha < 0.01) discard;
        FragColor = vec4(vColor.rgb, vColor.a * alpha);
        EntityID = -1;
      }
    )";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fs);

    m_textProgram = glCreateProgram();
    glAttachShader(m_textProgram, vs);
    glAttachShader(m_textProgram, fs);
    glLinkProgram(m_textProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_textSamplerLoc = glGetUniformLocation(m_textProgram, "u_FontTex");
  }

  ViewportRenderer::ViewportRenderer() {
    compileShaders();
    compileGridShader();
    compileTextShader();

    constexpr float gridReach = 8000.0f;
    const glm::vec3 gridVertices[] = {
      {-gridReach, 0.001f, -gridReach},
      { gridReach, 0.001f, -gridReach},
      { gridReach, 0.001f,  gridReach},
      {-gridReach, 0.001f,  gridReach}
    };

    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);
    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(gridVertices), gridVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

    // Line Buffers
    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);

    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex3D), (void*)offsetof(Vertex3D, entityID));

    // Glow Line Buffers (additive-blended halo pass, same vertex layout as regular lines)
    glGenVertexArrays(1, &m_glowLineVao);
    glGenBuffers(1, &m_glowLineVbo);

    glBindVertexArray(m_glowLineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_glowLineVbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex3D), (void*)offsetof(Vertex3D, entityID));

    // Point Buffers
    glGenVertexArrays(1, &m_pointVao);
    glGenBuffers(1, &m_pointVbo);

    glBindVertexArray(m_pointVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_pointVbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Point3D), (void*)offsetof(Point3D, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Point3D), (void*)offsetof(Point3D, color));

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_INT, sizeof(Point3D), (void*)offsetof(Point3D, entityID));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Point3D), (void*)offsetof(Point3D, size));

    // Text (glyph quad) Buffers
    glGenVertexArrays(1, &m_textVao);
    glGenBuffers(1, &m_textVbo);

    glBindVertexArray(m_textVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, uv));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, color));

    glBindVertexArray(0);
  }

  void ViewportRenderer::addLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color, int entityID) {
    m_lineBuffer.push_back({p1, color, entityID});
    m_lineBuffer.push_back({p2, color, entityID});
  }

  void ViewportRenderer::addGlowLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
    m_glowLineBuffer.push_back({p1, color, -1});
    m_glowLineBuffer.push_back({p2, color, -1});
  }

  void ViewportRenderer::addPoint(const glm::vec3& p, const glm::vec4& color, int entityID, float size) {
    m_pointBuffer.push_back({p, color, entityID, size});
  }

  void ViewportRenderer::addText(
    const glm::vec2& screenPosPixels, 
    const std::string& text, 
    const glm::vec4& color,
    float fbWidth, 
    float fbHeight, 
    float pixelScale
  ) {
    ImFontBaked* baked = ImGui::GetFontBaked();
    if (!baked || fbWidth <= 0.0f || fbHeight <= 0.0f) return;

    auto toNdc = [&](float px, float py) -> glm::vec2 {
      return glm::vec2((px / fbWidth) * 2.0f - 1.0f, 1.0f - (py / fbHeight) * 2.0f);
    };

    glm::vec2 pen = screenPosPixels;
    for (const char c : text) {
      const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(c));
      if (!glyph) continue;

      if (glyph->Visible) {
        const glm::vec2 p0 = toNdc(pen.x + glyph->X0 * pixelScale, pen.y + glyph->Y0 * pixelScale);
        const glm::vec2 p1 = toNdc(pen.x + glyph->X1 * pixelScale, pen.y + glyph->Y0 * pixelScale);
        const glm::vec2 p2 = toNdc(pen.x + glyph->X1 * pixelScale, pen.y + glyph->Y1 * pixelScale);
        const glm::vec2 p3 = toNdc(pen.x + glyph->X0 * pixelScale, pen.y + glyph->Y1 * pixelScale);

        const glm::vec2 uv0(glyph->U0, glyph->V0);
        const glm::vec2 uv1(glyph->U1, glyph->V0);
        const glm::vec2 uv2(glyph->U1, glyph->V1);
        const glm::vec2 uv3(glyph->U0, glyph->V1);

        m_textBuffer.push_back({p0, uv0, color});
        m_textBuffer.push_back({p1, uv1, color});
        m_textBuffer.push_back({p2, uv2, color});

        m_textBuffer.push_back({p0, uv0, color});
        m_textBuffer.push_back({p2, uv2, color});
        m_textBuffer.push_back({p3, uv3, color});
      }

      pen.x += glyph->AdvanceX * pixelScale;
    }
  }

  void ViewportRenderer::clearBuffers() {
    m_lineBuffer.clear();
    m_glowLineBuffer.clear();
    m_pointBuffer.clear();
  }

  void ViewportRenderer::clearTextBuffer() {
    m_textBuffer.clear();
  }

  void ViewportRenderer::reserve(size_t lineCount, size_t pointCount) {
    m_lineBuffer.reserve(lineCount * 2);
    m_pointBuffer.reserve(pointCount);
  }

  void ViewportRenderer::uploadCurrentBuffer() {
    m_lineVertexCount = static_cast<GLsizei>(m_lineBuffer.size());
    if (m_lineVertexCount > 0) {
      glBindVertexArray(m_lineVao);
      glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
      glBufferData(GL_ARRAY_BUFFER, m_lineBuffer.size() * sizeof(Vertex3D), m_lineBuffer.data(), GL_DYNAMIC_DRAW);
    }

    m_glowLineVertexCount = static_cast<GLsizei>(m_glowLineBuffer.size());
    if (m_glowLineVertexCount > 0) {
      glBindVertexArray(m_glowLineVao);
      glBindBuffer(GL_ARRAY_BUFFER, m_glowLineVbo);
      glBufferData(GL_ARRAY_BUFFER, m_glowLineBuffer.size() * sizeof(Vertex3D), m_glowLineBuffer.data(), GL_DYNAMIC_DRAW);
    }

    m_pointVertexCount = static_cast<GLsizei>(m_pointBuffer.size());
    if (m_pointVertexCount > 0) {
      glBindVertexArray(m_pointVao);
      glBindBuffer(GL_ARRAY_BUFFER, m_pointVbo);
      glBufferData(GL_ARRAY_BUFFER, m_pointBuffer.size() * sizeof(Point3D), m_pointBuffer.data(), GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
  }

  void ViewportRenderer::uploadTextBuffer() {
    m_textVertexCount = static_cast<GLsizei>(m_textBuffer.size());
    if (m_textVertexCount > 0) {
      glBindVertexArray(m_textVao);
      glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
      glBufferData(GL_ARRAY_BUFFER, m_textBuffer.size() * sizeof(TextVertex), m_textBuffer.data(), GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
  }

  void ViewportRenderer::renderGrid(const glm::mat4& mvp, float spacing) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glUseProgram(m_gridProgram);
    glUniformMatrix4fv(m_gridMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(m_gridSpacingLoc, spacing);
    glUniform1f(m_gridAxisGapLoc, spacing * 0.16f);

    glBindVertexArray(m_gridVao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  void ViewportRenderer::render(const glm::mat4& mvp) {
    glUseProgram(m_program);
    glUniformMatrix4fv(m_mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

    // Coverage-based AA on top of MSAA, so thin lines don't fall back to hard, blocky edges.
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (m_lineVertexCount > 0) {
      glBindVertexArray(m_lineVao);
      glLineWidth(1.5f);
      glDrawArrays(GL_LINES, 0, m_lineVertexCount);
    }

    // Additive-blended halo pass: thicker, translucent copies of the force arrows fake a glow/bloom.
    if (m_glowLineVertexCount > 0) {
      glDepthMask(GL_FALSE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);

      glBindVertexArray(m_glowLineVao);
      glLineWidth(6.0f);
      glDrawArrays(GL_LINES, 0, m_glowLineVertexCount);

      glDepthMask(GL_TRUE);
      glLineWidth(1.5f);
    }

    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);

    if (m_pointVertexCount > 0) {
      glEnable(GL_PROGRAM_POINT_SIZE);
      glBindVertexArray(m_pointVao);
      glDrawArrays(GL_POINTS, 0, m_pointVertexCount);
    }

    glBindVertexArray(0);
    glUseProgram(0);
  }

  // Draws the glyph batch on top of the scene, unaffected by depth, using ImGui's font atlas texture.
  void ViewportRenderer::renderText() {
    if (m_textVertexCount <= 0) return;

    const GLuint fontTex = static_cast<GLuint>(ImGui::GetIO().Fonts->TexRef.GetTexID());
    if (fontTex == 0) return;

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_textProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glUniform1i(m_textSamplerLoc, 0);

    glBindVertexArray(m_textVao);
    glDrawArrays(GL_TRIANGLES, 0, m_textVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);

    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
  }
} // namespace anaf::GUI end
