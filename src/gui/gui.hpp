#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

struct Framebuffer {
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint rbo = 0;
    int width = 0;
    int height = 0;

    void init(int w, int h);
    void rescale(int w, int h);
    void destroy();
};

void setup_cad_theme();

int initRunGUI();