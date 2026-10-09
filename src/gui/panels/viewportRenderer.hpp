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

#include <cstddef>
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
    // World distance the marker is pulled toward the eye before the depth test, so the element
    // it sits in (a beam section around the node) does not hide it; anything further in front does.
    float depthLift {0.0f};
  };

  // Screen-space glyph quad vertex; position is already in NDC (-1..1).
  struct TextVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
  };

  // Camera data for the procedural ground grid. Every pixel casts its own view ray at the
  // y = 0 plane, so the grid has no large geometry to clip and stays exact at any distance.
  struct GridView {
    glm::vec3 eyeLocal{0.0f};  // eye position relative to (origin.x, 0, origin.y)
    glm::vec2 origin{0.0f};    // world x / z of the local grid origin, a multiple of 10 * spacing
    glm::vec3 forward{0.0f};   // unit view direction
    glm::vec3 right{0.0f};     // camera right, scaled by tan(fovY / 2) * aspect
    glm::vec3 up{0.0f};        // camera up, scaled by tan(fovY / 2)
    float spacing{1.0f};       // minor cell size; every tenth line is a major line
    float fadeDistance{100.0f}; // the grid fades out towards this distance from the eye
    glm::vec3 color{0.62f, 0.70f, 0.78f}; // line color (theme); alpha comes from the line coverage
  };

  // Move-only: every GL object is owned by a GlHandle, so copies are rejected at compile time.
  class ViewportRenderer {
  private:
    GlProgram m_program;
    GlVertexArray m_lineVao;
    GlBuffer m_lineVbo;
    GlVertexArray m_glowLineVao;
    GlBuffer m_glowLineVbo;
    GlVertexArray m_triangleVao;
    GlBuffer m_triangleVbo;
    GlVertexArray m_pointVao;
    GlBuffer m_pointVbo;
    GLint m_mvpLoc{-1};
    GLint m_eyeLoc{-1};
    GLint m_worldPerPixelLoc{-1};

    GlProgram m_gridProgram;
    GlVertexArray m_gridVao;
    GlBuffer m_gridVbo;
    GLint m_gridEyeLoc{-1};
    GLint m_gridOriginLoc{-1};
    GLint m_gridForwardLoc{-1};
    GLint m_gridRightLoc{-1};
    GLint m_gridUpLoc{-1};
    GLint m_gridSpacingLoc{-1};
    GLint m_gridAxisGapLoc{-1};
    GLint m_gridFadeLoc{-1};
    GLint m_gridColorLoc{-1};

    // Vertical gradient behind the scene, drawn with the grid's fullscreen triangle.
    GlProgram m_backgroundProgram;
    GLint m_backgroundTopLoc{-1};
    GLint m_backgroundBottomLoc{-1};

    GlProgram m_textProgram;
    GlVertexArray m_textVao;
    GlBuffer m_textVbo;

    std::vector<Vertex3D> m_lineBuffer;
    std::vector<Vertex3D> m_glowLineBuffer;
    std::vector<Vertex3D> m_triangleBuffer;
    std::vector<Point3D> m_pointBuffer;
    std::vector<TextVertex> m_textBuffer;

    GLsizei m_lineVertexCount{0};
    GLsizei m_glowLineVertexCount{0};
    GLsizei m_triangleVertexCount{0};
    GLsizei m_pointVertexCount{0};
    GLsizei m_textVertexCount{0};

    void compileShaders();

    void compileGridShader();

    void compileTextShader();

    void compileBackgroundShader();

  public:
    ViewportRenderer();

    void addLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color, int entityID = -1);

    // Adds a line to a separate additive-blended pass, drawn thicker and translucent to fake a glow/bloom halo.
    void addGlowLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color);

    // Adds a translucent, two-sided triangle (e.g. the plane of an inclined support); drawn
    // blended after the lines, without depth writes.
    void addTriangle(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color);

    // depthLift: see Point3D. The marker is lifted at least by its own screen radius, so the
    // lines meeting at the point never cut it.
    void addPoint(const glm::vec3& p, const glm::vec4& color, int entityID, float size = 12.0f, float depthLift = 0.0f);

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

    void reserve(std::size_t lineCount, std::size_t pointCount);

    void uploadCurrentBuffer();
    void uploadTextBuffer();

    // eye: camera position; worldPerPixel: world size of one framebuffer pixel at unit distance
    // (2 tan(fov / 2) / framebuffer height), used to lift the point markers.
    void render(const glm::mat4& mvp, const glm::vec3& eye, float worldPerPixel);

    void renderGrid(const GridView& view);

    // Fills the color target with a top-to-bottom gradient (entity ID -1, depth untouched).
    // Call right after clearing, before any scene geometry.
    void renderBackground(const glm::vec3& top, const glm::vec3& bottom);

    void renderText();
  };

} // namespace anaf::GUI end
