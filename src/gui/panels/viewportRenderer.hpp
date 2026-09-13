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
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cstddef>

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

    class ViewportRenderer {
    private:
        GLuint m_program{0};
        GLuint m_lineVao{0}, m_lineVbo{0};
        GLuint m_pointVao{0}, m_pointVbo{0};
        GLint m_mvpLoc{-1};

        std::vector<Vertex3D> m_lineBuffer;
        std::vector<Point3D> m_pointBuffer;

        GLsizei m_lineVertexCount{0};
        GLsizei m_pointVertexCount{0};

        void compileShaders() {
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

    public:
        ViewportRenderer() {
            compileShaders();

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

            glBindVertexArray(0);
        }

        ~ViewportRenderer() {
            if (m_lineVao) glDeleteVertexArrays(1, &m_lineVao);
            if (m_lineVbo) glDeleteBuffers(1, &m_lineVbo);
            if (m_pointVao) glDeleteVertexArrays(1, &m_pointVao);
            if (m_pointVbo) glDeleteBuffers(1, &m_pointVbo);
            if (m_program) glDeleteProgram(m_program);
        }

        void addLine(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color, int entityID = -1) {
            m_lineBuffer.push_back({p1, color, entityID});
            m_lineBuffer.push_back({p2, color, entityID});
        }

        void addPoint(const glm::vec3& p, const glm::vec4& color, int entityID, float size = 12.0f) {
            m_pointBuffer.push_back({p, color, entityID, size});
        }

        void clearBuffers() {
            m_lineBuffer.clear();
            m_pointBuffer.clear();
        }

        void reserve(size_t lineCount, size_t pointCount) {
            m_lineBuffer.reserve(lineCount * 2);
            m_pointBuffer.reserve(pointCount);
        }

        void uploadCurrentBuffer() {
            m_lineVertexCount = static_cast<GLsizei>(m_lineBuffer.size());
            if (m_lineVertexCount > 0) {
                glBindVertexArray(m_lineVao);
                glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
                glBufferData(GL_ARRAY_BUFFER, m_lineBuffer.size() * sizeof(Vertex3D), m_lineBuffer.data(), GL_DYNAMIC_DRAW);
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

        void render(const glm::mat4& mvp) {
            glUseProgram(m_program);
            glUniformMatrix4fv(m_mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

            if (m_lineVertexCount > 0) {
                glBindVertexArray(m_lineVao);
                glLineWidth(1.5f);
                glDrawArrays(GL_LINES, 0, m_lineVertexCount);
            }

            if (m_pointVertexCount > 0) {
                glEnable(GL_PROGRAM_POINT_SIZE);
                glBindVertexArray(m_pointVao);
                glDrawArrays(GL_POINTS, 0, m_pointVertexCount);
            }

            glBindVertexArray(0);
            glUseProgram(0);
        }
    };

} // namespace anaf::GUI end


