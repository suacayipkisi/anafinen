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

// Lit, instanced geometry of the viewport: beam elements drawn with their real cross-section
// and node spheres. A beam mesh is one cross-section extruded over a unit length (local x from
// 0 to 1, y / z in metres); every drawn segment is an instance that places it between two
// points with the element's local axes, so a section's geometry exists once on the GPU however
// many elements use it.

#include <glad/gl.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/matrix_float4x4.hpp>

#include <guiMaterials/glHandle.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace anaf::GUI {

  struct MeshVertex {
    glm::vec3 local;  // x in [0, 1] along the segment, y / z in the section plane (m)
    glm::vec3 normal; // in the local frame
  };

  struct BeamInstance {
    glm::vec3 start;
    glm::vec3 end;
    glm::vec3 axisY0; // section axes at start and at end (unit vectors), interpolated along the
    glm::vec3 axisZ0; // segment: a rotated section (twist, bending rotation) stays continuous
    glm::vec3 axisY1;
    glm::vec3 axisZ1;
    glm::vec4 color0; // at start / end, interpolated along the segment
    glm::vec4 color1;
    int entityID{-1};
  };

  struct SphereInstance {
    glm::vec4 centerRadius;
    glm::vec4 color;
    int entityID{-1};
  };

  class BeamSceneRenderer {
  public:
    BeamSceneRenderer();

    // Adds a mesh (GL_TRIANGLES or GL_LINES) and returns its index.
    int addMesh(std::span<const MeshVertex> vertices, GLenum mode);
    void clearMeshes();

    void clearInstances(); // every mesh's instances
    void addInstance(int mesh, const BeamInstance& instance);
    void clearSpheres();
    void addSphere(const glm::vec3& center, float radius, const glm::vec4& color, int entityID);

    void upload();
    // Opaque, depth-tested; lit by a light at the eye (viewDirection = camera forward).
    void render(const glm::mat4& mvp, const glm::vec3& viewDirection);

  private:
    struct Mesh {
      GlVertexArray vao;
      GlBuffer vertices;
      GlBuffer instances;
      GLsizei vertexCount{0};
      GLenum mode{GL_TRIANGLES};
      std::vector<BeamInstance> pending;
      GLsizei instanceCount{0};
    };

    GlProgram m_beamProgram;
    GlProgram m_sphereProgram;
    GLint m_beamMvp{-1}, m_beamView{-1}, m_sphereMvp{-1}, m_sphereView{-1};
    std::vector<Mesh> m_meshes;

    GlVertexArray m_sphereVao;
    GlBuffer m_sphereVertices;
    GlBuffer m_sphereInstances;
    GLsizei m_sphereVertexCount{0};
    std::vector<SphereInstance> m_pendingSpheres;
    GLsizei m_sphereCount{0};
  };

} // namespace anaf::GUI end
