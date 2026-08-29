#include "gui.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>

#include "framebuffer.hpp"
#include "imGuiLayer.hpp"
#include "iPanel.hpp"
#include "mainDockSpaceHost.hpp"
#include "trussControlPanel.hpp"
#include "viewportPanel.hpp"
#include "trussControlPanel.hpp"


int initgui(){
#ifdef __linux__
    glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
#endif

    if(!glfwInit()){
        std::cerr << "Failed to initialize GLFW" << '\n';
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
        if (!glfwInit()) {
            std::cerr << "Fatal: GLFW initialization failed completely.\n";
            return -1;
        }
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwwindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1600, 900, "Modular Simulation Engine", nullptr, nullptr);

    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    gladLoadGL((GLADloadfunc)glfwGetProcAddress);

    //core system initialization
    ImGuiLayer imguiLayer;
    imguiLayer.init(window);

    auto fbo = std::make_shared<Framebuffer>(1280, 720);

    // register UI panels
    PanelManager panelManager;
    panelManager.addPanel<MainDockSpaceHost>(window);
    panelManager.addPanel<ViewportPanel>(fbo);
    panelManager.addPanel<TrussControlPanel>();

    // game loop
    while (!glfwWindowShouldClose(window)){
        glfwPollEvents();

        // render 3d sim scene into custom fbo
        fbo->bind();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        //native 3d draw calls
        fbo->unbind();

        //clear default framebuffer and render imgui panels
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        imguiLayer.beginFrame();
        
        panelManager.onImGuiRender();
        imguiLayer.endFrame();

        glfwSwapBuffers(window);

    }

    imguiLayer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
