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

#include <cstdint>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace anaf::GUI {

    class Framebuffer {
    private:
        std::uint32_t m_fbo_id_ {};
        std::uint32_t m_texture_id_ {};
        std::uint32_t m_rbo_id_ {};
        std::uint32_t m_width_ {};
        std::uint32_t m_height_ {};

        void cleanup() {
            if (m_fbo_id_) {    
                glDeleteFramebuffers(1, &m_fbo_id_);
                glDeleteTextures(1, &m_texture_id_);
                glDeleteRenderbuffers(1, &m_rbo_id_);
                m_fbo_id_ = 0;
                m_texture_id_ = 0;
                m_rbo_id_ = 0;
            }
        }
    public: 
        Framebuffer(std::uint32_t width, uint32_t height) {
            resize(width, height);
        }

        ~Framebuffer(){
            cleanup();
        }

        void bind() const {
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_id_);
            glViewport(0, 0, m_width_, m_height_);
        }

        void unbind() const {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void resize(std::uint32_t width, std::uint32_t height);

        std::uint32_t getTextureID() const { return m_texture_id_;}
        std::uint32_t getWidth() const { return m_width_;}
        std::uint32_t getHeight() const { return m_height_;}

    };

} // namespace anaf::GUI end
