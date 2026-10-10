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

#include "beamSceneRenderer.hpp"

#include <guiMaterials/shaderProgram.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <numbers>

namespace anaf::GUI {

  namespace {
    constexpr GLuint meshBinding = 0;
    constexpr GLuint instanceBinding = 1;

    // Shared fragment stage: a light at the eye plus ambient, two-sided (sections are seen from
    // inside at open ends), and the entity ID for picking.
    constexpr const char* litFragment = R"(
      #version 460 core
      layout (location = 0) out vec4 FragColor;
      layout (location = 1) out int EntityID;

      in vec3 vNormal;
      in vec4 vColor;
      flat in int vEntityID;

      uniform vec3 u_ViewDir;

      void main() {
        float lit = 1.0;
        if (dot(vNormal, vNormal) > 1e-12) {
          float facing = abs(dot(normalize(vNormal), u_ViewDir));
          lit = 0.38 + 0.62 * facing;
        }
        FragColor = vec4(vColor.rgb * lit, vColor.a);
        EntityID = vEntityID;
      }
    )";

    constexpr const char* beamVertex = R"(
      #version 460 core
      layout (location = 0) in vec3 aLocal;
      layout (location = 1) in vec3 aNormal;
      layout (location = 2) in vec3 iStart;
      layout (location = 3) in vec3 iEnd;
      layout (location = 4) in vec3 iAxisY0;
      layout (location = 5) in vec3 iAxisZ0;
      layout (location = 6) in vec3 iAxisY1;
      layout (location = 7) in vec3 iAxisZ1;
      layout (location = 8) in vec4 iColor0;
      layout (location = 9) in vec4 iColor1;
      layout (location = 10) in int iEntityID;

      uniform mat4 u_MVP;

      out vec3 vNormal;
      out vec4 vColor;
      flat out int vEntityID;

      void main() {
        vec3 along = iEnd - iStart;
        vec3 axisY = normalize(mix(iAxisY0, iAxisY1, aLocal.x));
        vec3 axisZ = normalize(mix(iAxisZ0, iAxisZ1, aLocal.x));
        vec3 axisX = cross(axisY, axisZ);
        vec3 world = iStart + along * aLocal.x + axisY * aLocal.y + axisZ * aLocal.z;
        vNormal = axisX * aNormal.x + axisY * aNormal.y + axisZ * aNormal.z;
        vColor = mix(iColor0, iColor1, aLocal.x);
        vEntityID = iEntityID;
        gl_Position = u_MVP * vec4(world, 1.0);
      }
    )";

    constexpr const char* sphereVertex = R"(
      #version 460 core
      layout (location = 0) in vec3 aLocal; // unit sphere: position = normal
      layout (location = 2) in vec4 iCenterRadius;
      layout (location = 3) in vec4 iColor;
      layout (location = 4) in int iEntityID;

      uniform mat4 u_MVP;

      out vec3 vNormal;
      out vec4 vColor;
      flat out int vEntityID;

      void main() {
        vNormal = aLocal;
        vColor = iColor;
        vEntityID = iEntityID;
        gl_Position = u_MVP * vec4(iCenterRadius.xyz + aLocal * iCenterRadius.w, 1.0);
      }
    )";

    void floatAttrib(GLuint vao, GLuint location, GLint components, std::size_t offset, GLuint binding) {
      glEnableVertexArrayAttrib(vao, location);
      glVertexArrayAttribFormat(vao, location, components, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offset));
      glVertexArrayAttribBinding(vao, location, binding);
    }

    void intAttrib(GLuint vao, GLuint location, std::size_t offset, GLuint binding) {
      glEnableVertexArrayAttrib(vao, location);
      glVertexArrayAttribIFormat(vao, location, 1, GL_INT, static_cast<GLuint>(offset));
      glVertexArrayAttribBinding(vao, location, binding);
    }

    // UV sphere of unit radius as a triangle list.
    std::vector<glm::vec3> unitSphere(const int rings, const int sectors) {
      std::vector<glm::vec3> points;
      const auto at = [&](const int ring, const int sector) {
        const float theta = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(rings);
        const float phi = 2.0f * std::numbers::pi_v<float> * static_cast<float>(sector) / static_cast<float>(sectors);
        return glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
      };
      for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
          const glm::vec3 a = at(r, s), b = at(r + 1, s), c = at(r + 1, s + 1), d = at(r, s + 1);
          points.insert(points.end(), {a, b, c, a, c, d});
        }
      }
      return points;
    }
  } // namespace end

  BeamSceneRenderer::BeamSceneRenderer() {
    m_beamProgram = buildShaderProgram(beamVertex, litFragment, "beam");
    m_sphereProgram = buildShaderProgram(sphereVertex, litFragment, "sphere");
    m_beamMvp = glGetUniformLocation(m_beamProgram.get(), "u_MVP");
    m_beamView = glGetUniformLocation(m_beamProgram.get(), "u_ViewDir");
    m_sphereMvp = glGetUniformLocation(m_sphereProgram.get(), "u_MVP");
    m_sphereView = glGetUniformLocation(m_sphereProgram.get(), "u_ViewDir");

    const auto sphere = unitSphere(10, 16);
    m_sphereVertexCount = static_cast<GLsizei>(sphere.size());
    m_sphereVertices = createBuffer();
    glNamedBufferStorage(m_sphereVertices.get(), static_cast<GLsizeiptr>(sphere.size() * sizeof(glm::vec3)), sphere.data(), 0);
    m_sphereInstances = createBuffer();
    m_sphereVao = createVertexArray();
    const GLuint vao = m_sphereVao.get();
    glVertexArrayVertexBuffer(vao, meshBinding, m_sphereVertices.get(), 0, sizeof(glm::vec3));
    floatAttrib(vao, 0, 3, 0, meshBinding);
    glVertexArrayBindingDivisor(vao, instanceBinding, 1);
    floatAttrib(vao, 2, 4, offsetof(SphereInstance, centerRadius), instanceBinding);
    floatAttrib(vao, 3, 4, offsetof(SphereInstance, color), instanceBinding);
    intAttrib(vao, 4, offsetof(SphereInstance, entityID), instanceBinding);
  }

  int BeamSceneRenderer::addMesh(const std::span<const MeshVertex> vertices, const GLenum mode) {
    Mesh mesh;
    mesh.mode = mode;
    mesh.vertexCount = static_cast<GLsizei>(vertices.size());
    mesh.vertices = createBuffer();
    if (!vertices.empty()) {
      glNamedBufferStorage(mesh.vertices.get(), static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), 0);
    }
    mesh.instances = createBuffer();
    mesh.vao = createVertexArray();
    const GLuint vao = mesh.vao.get();
    glVertexArrayVertexBuffer(vao, meshBinding, mesh.vertices.get(), 0, sizeof(MeshVertex));
    floatAttrib(vao, 0, 3, offsetof(MeshVertex, local), meshBinding);
    floatAttrib(vao, 1, 3, offsetof(MeshVertex, normal), meshBinding);
    glVertexArrayBindingDivisor(vao, instanceBinding, 1);
    floatAttrib(vao, 2, 3, offsetof(BeamInstance, start), instanceBinding);
    floatAttrib(vao, 3, 3, offsetof(BeamInstance, end), instanceBinding);
    floatAttrib(vao, 4, 3, offsetof(BeamInstance, axisY0), instanceBinding);
    floatAttrib(vao, 5, 3, offsetof(BeamInstance, axisZ0), instanceBinding);
    floatAttrib(vao, 6, 3, offsetof(BeamInstance, axisY1), instanceBinding);
    floatAttrib(vao, 7, 3, offsetof(BeamInstance, axisZ1), instanceBinding);
    floatAttrib(vao, 8, 4, offsetof(BeamInstance, color0), instanceBinding);
    floatAttrib(vao, 9, 4, offsetof(BeamInstance, color1), instanceBinding);
    intAttrib(vao, 10, offsetof(BeamInstance, entityID), instanceBinding);
    m_meshes.push_back(std::move(mesh));
    return static_cast<int>(m_meshes.size()) - 1;
  }

  void BeamSceneRenderer::clearMeshes() { m_meshes.clear(); }

  void BeamSceneRenderer::clearInstances() {
    for (auto& mesh : m_meshes) mesh.pending.clear();
  }

  void BeamSceneRenderer::addInstance(const int mesh, const BeamInstance& instance) {
    if (mesh >= 0 && mesh < static_cast<int>(m_meshes.size())) m_meshes[static_cast<std::size_t>(mesh)].pending.push_back(instance);
  }

  void BeamSceneRenderer::clearSpheres() { m_pendingSpheres.clear(); }

  void BeamSceneRenderer::addSphere(const glm::vec3& center, const float radius, const glm::vec4& color, const int entityID) {
    m_pendingSpheres.push_back({glm::vec4(center, radius), color, entityID});
  }

  void BeamSceneRenderer::upload() {
    for (auto& mesh : m_meshes) {
      mesh.instanceCount = static_cast<GLsizei>(mesh.pending.size());
      if (mesh.pending.empty()) continue;
      // Re-specified every time (orphaning); the instance list changes with the camera when the
      // level of detail is active.
      glNamedBufferData(mesh.instances.get(), static_cast<GLsizeiptr>(mesh.pending.size() * sizeof(BeamInstance)), mesh.pending.data(), GL_DYNAMIC_DRAW);
      glVertexArrayVertexBuffer(mesh.vao.get(), instanceBinding, mesh.instances.get(), 0, sizeof(BeamInstance));
    }
    m_sphereCount = static_cast<GLsizei>(m_pendingSpheres.size());
    if (!m_pendingSpheres.empty()) {
      glNamedBufferData(m_sphereInstances.get(), static_cast<GLsizeiptr>(m_pendingSpheres.size() * sizeof(SphereInstance)), m_pendingSpheres.data(), GL_DYNAMIC_DRAW);
      glVertexArrayVertexBuffer(m_sphereVao.get(), instanceBinding, m_sphereInstances.get(), 0, sizeof(SphereInstance));
    }
  }

  void BeamSceneRenderer::render(const glm::mat4& mvp, const glm::vec3& viewDirection) {
    glProgramUniformMatrix4fv(m_beamProgram.get(), m_beamMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glProgramUniform3fv(m_beamProgram.get(), m_beamView, 1, glm::value_ptr(viewDirection));
    glProgramUniformMatrix4fv(m_sphereProgram.get(), m_sphereMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glProgramUniform3fv(m_sphereProgram.get(), m_sphereView, 1, glm::value_ptr(viewDirection));

    glUseProgram(m_beamProgram.get());
    for (const auto& mesh : m_meshes) {
      if (mesh.instanceCount == 0 || mesh.vertexCount == 0) continue;
      if (mesh.mode == GL_LINES) glLineWidth(1.5f);
      glBindVertexArray(mesh.vao.get());
      glDrawArraysInstanced(mesh.mode, 0, mesh.vertexCount, mesh.instanceCount);
    }
    if (m_sphereCount > 0) {
      glUseProgram(m_sphereProgram.get());
      glBindVertexArray(m_sphereVao.get());
      glDrawArraysInstanced(GL_TRIANGLES, 0, m_sphereVertexCount, m_sphereCount);
    }
    glBindVertexArray(0);
    glUseProgram(0);
  }

} // namespace anaf::GUI end
