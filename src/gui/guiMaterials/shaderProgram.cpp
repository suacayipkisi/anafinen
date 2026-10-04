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

#include "shaderProgram.hpp"

#include <log/anaf_info.hpp>

#include <algorithm>
#include <cstddef>
#include <string>

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
    GlProgram buildProgramImpl(const char* vertexSource, const char* fragmentSource, std::string_view programName) {
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

  } // namespace end

  GlProgram buildShaderProgram(const char* vertexSource, const char* fragmentSource, const std::string_view programName) {
    return buildProgramImpl(vertexSource, fragmentSource, programName);
  }

} // namespace anaf::GUI end
