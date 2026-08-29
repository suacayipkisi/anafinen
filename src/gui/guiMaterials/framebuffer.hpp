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
#include "../../log/anaf_info.h"

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

    void resize(std::uint32_t width, std::uint32_t height) {
        if (width == 0 || height == 0 || (width == m_width_ && height == m_height_ && m_fbo_id_ != 0)) {
            return;
        }

        m_width_ = width;
        m_height_ = height;

        cleanup();

        glGenFramebuffers(1, &m_fbo_id_);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_id_);

        glGenTextures(1, &m_texture_id_);
        glBindTexture(GL_TEXTURE_2D, m_texture_id_);
        glTexImage2D(
            GL_TEXTURE_2D, 
            0, 
            GL_RGB, 
            m_width_, 
            m_height_, 
            0, 
            GL_RGB, 
            GL_UNSIGNED_BYTE, 
            nullptr
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, 
            GL_COLOR_ATTACHMENT0, 
            GL_TEXTURE_2D, 
            m_texture_id_, 
            0
        );

        glGenRenderbuffers(1, &m_rbo_id_);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rbo_id_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width_, m_height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo_id_);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            anaf_error("ERROR: Framebuffer is not complete!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    std::uint32_t getTextureID() const { return m_texture_id_;}
    std::uint32_t getWidth() const { return m_width_;}
    std::uint32_t getHeight() const { return m_height_;}

};
