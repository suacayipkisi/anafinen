#include "gui.hpp"

#include <array>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "../anafInfo.hpp"
#include "../test/status.hpp"

void Framebuffer::init(int w, int h) {
    width = w;
    height = h;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        anafLog::error("Framebuffer initialization failed.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::rescale(int w, int h) {
    if (w <= 0 || h <= 0 || (w == width && h == height)) return;
    width = w;
    height = h;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
}

void Framebuffer::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (texture) glDeleteTextures(1, &texture);
    if (rbo) glDeleteRenderbuffers(1, &rbo);
}

void setup_cad_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.27f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.28f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.32f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.27f, 0.30f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.24f, 0.26f, 0.28f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.18f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.14f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.22f, 0.24f, 0.60f);
}

int initRunGUI(AllStatus& status) {

    if (!glfwInit()) {
        anafLog::error("GLFW initialization failed.");
        return -1;
    }
    anafLog::success("GLFW initialized successfully.");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1440, 900, "ANAFINEN", nullptr, nullptr);
    if (!window) {
        anafLog::error("Failed to create GLFW window context.");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        anafLog::error("GLAD loader failed to bind OpenGL context.");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    anafLog::success("OpenGL context and subsystems initialized successfully.");

    const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    std::array<int, 2> graphStatus{1, 1};
    if(gl_version){
        anafLog::success("OpenGL Version: {}", gl_version);
        graphStatus[0] = 0;
    }
    else{
        anafLog::warn("OpenGL Version Couldn't Determined: {}", "Unknown");
    }

    if(gl_renderer){
        anafLog::success("GPU Renderer  : {}", gl_renderer);
        graphStatus[1] = 0;
    }
    else{
        anafLog::warn("GPU Renderer Couldn't Determined  : {}", "Unknown");
    }

    status.setGraphicsStatus(graphStatus);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    setup_cad_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    Framebuffer viewport_fbo;
    viewport_fbo.init(1280, 720);

    float clear_color[4] = { 0.16f, 0.18f, 0.20f, 1.0f };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. Offscreen 3D Scene Pass
        glBindFramebuffer(GL_FRAMEBUFFER, viewport_fbo.fbo);
        glViewport(0, 0, viewport_fbo.width, viewport_fbo.height);
        glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2. Dear ImGui UI Pass
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(main_viewport->Pos);
        ImGui::SetNextWindowSize(main_viewport->Size);
        ImGui::SetNextWindowViewport(main_viewport->ID);

        ImGuiWindowFlags root_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("CADMainDockSpaceHost", nullptr, root_flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("CADMainDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import Mesh (.vtk / .obj)...")) {}
                if (ImGui::MenuItem("Export Results...")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) { glfwSetWindowShouldClose(window, GLFW_TRUE); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Solver")) {
                if (ImGui::MenuItem("Run Modal Analysis (Spectra)...")) {}
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        ImGui::End();

        // Panel: Model Tree
        ImGui::Begin("Model Tree");
        if (ImGui::TreeNode("Root Assembly")) {
            if (ImGui::TreeNode("1D Truss Subsystem")) {
                ImGui::BulletText("Element 0: [Node 0 -> Node 1]");
                ImGui::BulletText("Element 1: [Node 1 -> Node 2]");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Boundary Conditions")) {
                ImGui::BulletText("Fixed: Node 0 (UX, UY, UZ)");
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::End();

        // Panel: Inspector
        ImGui::Begin("Inspector");
        ImGui::Text("Material Properties");
        ImGui::Separator();
        
        static float youngs_modulus = 210.0f;
        static float density = 7850.0f;
        static float poisson_ratio = 0.30f;

        ImGui::DragFloat("E (GPa)", &youngs_modulus, 1.0f, 1.0f, 1000.0f);
        ImGui::DragFloat("Density (kg/m3)", &density, 10.0f, 100.0f, 20000.0f);
        ImGui::SliderFloat("Poisson (nu)", &poisson_ratio, 0.0f, 0.49f);

        ImGui::Spacing();
        ImGui::Text("Modal Parameters");
        ImGui::Separator();
        static int target_modes = 6;
        ImGui::SliderInt("Extracted Modes", &target_modes, 1, 32);

        if (ImGui::Button("Execute Solver", ImVec2(-1, 32))) {
            anafLog::info("Modal Solver triggered for {} eigenvalues.", target_modes);
        }
        ImGui::End();

        // Panel: 3D Viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("3D Viewport");
        ImVec2 viewport_panel_size = ImGui::GetContentRegionAvail();
        
        viewport_fbo.rescale(static_cast<int>(viewport_panel_size.x), static_cast<int>(viewport_panel_size.y));

        ImGui::Image(
            (ImTextureID)(intptr_t)viewport_fbo.texture,
            viewport_panel_size,
            ImVec2(0, 1),
            ImVec2(1, 0)
        );
        ImGui::End();
        ImGui::PopStyleVar();

        // 3. Swap Buffers & Present Frame
        ImGui::Render();
        
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup Subsystems
    viewport_fbo.destroy();
    anafLog::info("Closing the main window.");
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
