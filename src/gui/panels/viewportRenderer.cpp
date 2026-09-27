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

#include "viewportRenderer.hpp"

#include <glad/gl.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <log/anaf_info.hpp>

namespace anaf::GUI {

  namespace {

    // Drops the null terminator and trailing newlines drivers append to info logs.
    void trimInfoLog(std::string& infoLog) {
      while (!infoLog.empty() && (infoLog.back() == '\0' || infoLog.back() == '\n')) {
        infoLog.pop_back();
      }
    }

    // Returns an empty handle and logs the driver's info log when compilation fails.
    GlShader compileStage(GLenum stage, const char* source, std::string_view programName) {
      GlShader shader{glCreateShader(stage)};
      glShaderSource(shader.get(), 1, &source, nullptr);
      glCompileShader(shader.get());

      GLint status = GL_FALSE;
      glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &status);
      if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader.get(), logLength, nullptr, infoLog.data());
        trimInfoLog(infoLog);
        anaf::LOG::error("{} {} shader compile failed: {}",
          programName, stage == GL_VERTEX_SHADER ? "vertex" : "fragment", infoLog);
        return {};
      }
      return shader;
    }

    // Returns an empty handle and logs the driver's info log when any stage or the link fails.
    // Shader objects are released on return; a linked program does not need them.
    GlProgram buildProgram(const char* vertexSource, const char* fragmentSource, std::string_view programName) {
      const GlShader vs = compileStage(GL_VERTEX_SHADER, vertexSource, programName);
      const GlShader fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource, programName);
      if (!vs || !fs) {
        return {};
      }

      GlProgram program{glCreateProgram()};
      glAttachShader(program.get(), vs.get());
      glAttachShader(program.get(), fs.get());
      glLinkProgram(program.get());
      glDetachShader(program.get(), vs.get());
      glDetachShader(program.get(), fs.get());

      GLint status = GL_FALSE;
      glGetProgramiv(program.get(), GL_LINK_STATUS, &status);
      if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program.get(), logLength, nullptr, infoLog.data());
        trimInfoLog(infoLog);
        anaf::LOG::error("{} shader program link failed: {}", programName, infoLog);
        return {};
      }
      return program;
    }

    // DSA vertex layout helpers: every VAO here reads from a single vertex buffer at binding 0.
    constexpr GLuint kVertexBinding = 0;

    void setFloatAttrib(GLuint vao, GLuint location, GLint components, std::size_t offset) {
      glEnableVertexArrayAttrib(vao, location);
      glVertexArrayAttribFormat(vao, location, components, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offset));
      glVertexArrayAttribBinding(vao, location, kVertexBinding);
    }

    void setIntAttrib(GLuint vao, GLuint location, std::size_t offset) {
      glEnableVertexArrayAttrib(vao, location);
      glVertexArrayAttribIFormat(vao, location, 1, GL_INT, static_cast<GLuint>(offset));
      glVertexArrayAttribBinding(vao, location, kVertexBinding);
    }

    // Layout shared by line and glow-line batches: position, color, entity ID.
    void setupVertex3DLayout(const GlVertexArray& vao, const GlBuffer& vbo) {
      glVertexArrayVertexBuffer(vao.get(), kVertexBinding, vbo.get(), 0, sizeof(Vertex3D));
      setFloatAttrib(vao.get(), 0, 3, offsetof(Vertex3D, position));
      setFloatAttrib(vao.get(), 1, 4, offsetof(Vertex3D, color));
      setIntAttrib(vao.get(), 2, offsetof(Vertex3D, entityID));
    }

    // Re-specifies the whole store each upload (orphaning), same as the previous glBufferData path.
    template <typename T>
    GLsizei uploadVertices(const GlBuffer& vbo, const std::vector<T>& vertices) {
      if (!vertices.empty()) {
        glNamedBufferData(vbo.get(), static_cast<GLsizeiptr>(vertices.size() * sizeof(T)), vertices.data(), GL_DYNAMIC_DRAW);
      }
      return static_cast<GLsizei>(vertices.size());
    }

  } // namespace end

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

    m_program = buildProgram(vertexShaderSource, fragmentShaderSource, "scene");

    m_mvpLoc = glGetUniformLocation(m_program.get(), "u_MVP");
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

    m_gridProgram = buildProgram(vertexShaderSource, fragmentShaderSource, "grid");

    m_gridMvpLoc = glGetUniformLocation(m_gridProgram.get(), "u_MVP");
    m_gridSpacingLoc = glGetUniformLocation(m_gridProgram.get(), "u_GridSpacing");
    m_gridAxisGapLoc = glGetUniformLocation(m_gridProgram.get(), "u_AxisGap");
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

    m_textProgram = buildProgram(vertexShaderSource, fragmentShaderSource, "text");

    // The font atlas is always bound to texture unit 0, so the sampler is set once here.
    glProgramUniform1i(m_textProgram.get(), glGetUniformLocation(m_textProgram.get(), "u_FontTex"), 0);
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

    m_gridVbo = createBuffer();
    glNamedBufferStorage(m_gridVbo.get(), sizeof(gridVertices), gridVertices, 0);
    m_gridVao = createVertexArray();
    glVertexArrayVertexBuffer(m_gridVao.get(), kVertexBinding, m_gridVbo.get(), 0, sizeof(glm::vec3));
    setFloatAttrib(m_gridVao.get(), 0, 3, 0);

    // Line Buffers
    m_lineVbo = createBuffer();
    m_lineVao = createVertexArray();
    setupVertex3DLayout(m_lineVao, m_lineVbo);

    // Glow Line Buffers (additive-blended halo pass, same vertex layout as regular lines)
    m_glowLineVbo = createBuffer();
    m_glowLineVao = createVertexArray();
    setupVertex3DLayout(m_glowLineVao, m_glowLineVbo);

    // Point Buffers
    m_pointVbo = createBuffer();
    m_pointVao = createVertexArray();
    glVertexArrayVertexBuffer(m_pointVao.get(), kVertexBinding, m_pointVbo.get(), 0, sizeof(Point3D));
    setFloatAttrib(m_pointVao.get(), 0, 3, offsetof(Point3D, position));
    setFloatAttrib(m_pointVao.get(), 1, 4, offsetof(Point3D, color));
    setIntAttrib(m_pointVao.get(), 2, offsetof(Point3D, entityID));
    setFloatAttrib(m_pointVao.get(), 3, 1, offsetof(Point3D, size));

    // Text (glyph quad) Buffers
    m_textVbo = createBuffer();
    m_textVao = createVertexArray();
    glVertexArrayVertexBuffer(m_textVao.get(), kVertexBinding, m_textVbo.get(), 0, sizeof(TextVertex));
    setFloatAttrib(m_textVao.get(), 0, 2, offsetof(TextVertex, position));
    setFloatAttrib(m_textVao.get(), 1, 2, offsetof(TextVertex, uv));
    setFloatAttrib(m_textVao.get(), 2, 4, offsetof(TextVertex, color));
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
    m_lineVertexCount = uploadVertices(m_lineVbo, m_lineBuffer);
    m_glowLineVertexCount = uploadVertices(m_glowLineVbo, m_glowLineBuffer);
    m_pointVertexCount = uploadVertices(m_pointVbo, m_pointBuffer);
  }

  void ViewportRenderer::uploadTextBuffer() {
    m_textVertexCount = uploadVertices(m_textVbo, m_textBuffer);
  }

  void ViewportRenderer::renderGrid(const glm::mat4& mvp, float spacing) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glProgramUniformMatrix4fv(m_gridProgram.get(), m_gridMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glProgramUniform1f(m_gridProgram.get(), m_gridSpacingLoc, spacing);
    glProgramUniform1f(m_gridProgram.get(), m_gridAxisGapLoc, spacing * 0.16f);

    glUseProgram(m_gridProgram.get());
    glBindVertexArray(m_gridVao.get());
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  void ViewportRenderer::render(const glm::mat4& mvp) {
    glProgramUniformMatrix4fv(m_program.get(), m_mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUseProgram(m_program.get());

    // Coverage-based AA on top of MSAA, so thin lines don't fall back to hard, blocky edges.
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (m_lineVertexCount > 0) {
      glBindVertexArray(m_lineVao.get());
      glLineWidth(1.5f);
      glDrawArrays(GL_LINES, 0, m_lineVertexCount);
    }

    // Additive-blended halo pass: thicker, translucent copies of the force arrows fake a glow/bloom.
    if (m_glowLineVertexCount > 0) {
      glDepthMask(GL_FALSE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);

      glBindVertexArray(m_glowLineVao.get());
      glLineWidth(6.0f);
      glDrawArrays(GL_LINES, 0, m_glowLineVertexCount);

      glDepthMask(GL_TRUE);
      glLineWidth(1.5f);
    }

    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);

    if (m_pointVertexCount > 0) {
      glEnable(GL_PROGRAM_POINT_SIZE);
      glBindVertexArray(m_pointVao.get());
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

    glUseProgram(m_textProgram.get());
    glBindTextureUnit(0, fontTex);

    glBindVertexArray(m_textVao.get());
    glDrawArrays(GL_TRIANGLES, 0, m_textVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);

    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
  }
} // namespace anaf::GUI end
