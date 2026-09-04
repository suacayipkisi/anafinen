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
    };

    class ViewportRenderer {
    private:
        GLuint m_program{0};
        GLuint m_vao{0};
        GLuint m_vbo{0};
        GLint m_mvpLoc{-1};

        std::vector<Vertex3D> m_lineBuffer;

        void compileShaders () {
            const char* vertexShaderSource = R"(
                #version 460 core
                layout (location = 0) in vec3 aPos;
                layout (location = 1) in vec4 aColor;

                uniform mat4 u_MVP;
                out vec4 vColor;

                void main() {
                    vColor = aColor;
                    gl_Position = u_MVP * vec4(aPos, 1.0);
                }
            )";

            const char* fragmentShaderSource = R"(
                #version 460 core
                in vec4 vColor;
                out vec4 FragColor;

                void main() {
                    FragColor = vColor;
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
        ViewportRenderer () {
            compileShaders();

            glGenVertexArrays(1, &m_vao);
            glGenBuffers(1, &m_vbo);

            glBindVertexArray(m_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));

            glBindVertexArray(0);
        }

        ~ViewportRenderer() {
            if (m_vao) glDeleteVertexArrays(1, &m_vao);
            if (m_vbo) glDeleteBuffers(1, &m_vbo);
            if (m_program) glDeleteProgram(m_program);
        }

        void addLine (
            const glm::vec3& p1, 
            const glm::vec3& p2, 
            const glm::vec4& c1, 
            const glm::vec4& c2)
        {
            m_lineBuffer.push_back({p1, c1});
            m_lineBuffer.push_back({p2, c2});
        }

        void addLine (
            const glm::vec3& p1,
            const glm::vec3& p2,
            const glm::vec4& color
        )
        {
            addLine(p1, p2, color, color);
        }

        void clearLines () {
            m_lineBuffer.clear();
        }

        void flush (const glm::mat4& mvp) {
            if (m_lineBuffer.empty()) {
                return;
            }

            glUseProgram(m_program);
            glUniformMatrix4fv(m_mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

            glBindVertexArray(m_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferData(GL_ARRAY_BUFFER, m_lineBuffer.size() * sizeof(Vertex3D), m_lineBuffer.data(), GL_STREAM_DRAW);

            glLineWidth(2.0f);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_lineBuffer.size()));

            glBindVertexArray(0);
            glUseProgram(0);
        }

    };

} // namespace anaf::GUI end


