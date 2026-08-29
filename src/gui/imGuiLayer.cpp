#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "imGuiLayer.hpp"

void setupSpecialTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();

    // -------------------------------------------------------------
    // Rounding Corners
    // -------------------------------------------------------------
    style.WindowRounding    = 8.0f * scale; // Main window corners
    style.ChildRounding     = 6.0f * scale; // Sub-panels & regions
    style.FrameRounding     = 5.0f * scale; // Input boxes, buttons, sliders
    style.PopupRounding     = 6.0f * scale; // Context menus & tooltips
    style.ScrollbarRounding = 9.0f * scale; // Scrollbar handles
    style.GrabRounding      = 4.0f * scale; // Slider grab handles
    style.TabRounding       = 6.0f * scale; // Docking tabs

    // -------------------------------------------------------------
    // Borders & Padding
    // -------------------------------------------------------------
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(10.0f * scale, 10.0f * scale);
    style.FramePadding      = ImVec2(8.0f * scale, 4.0f * scale);
    style.ItemSpacing       = ImVec2(8.0f * scale, 6.0f * scale);
    style.ItemInnerSpacing  = ImVec2(6.0f * scale, 4.0f * scale);
    style.ScrollbarSize     = 12.0f * scale;

    // -------------------------------------------------------------
    // Modern Dark Color Palette (Slate / Dark Zinc)
    // -------------------------------------------------------------
    ImVec4* colors = style.Colors;

    // Backgrounds
    colors[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.22f, 0.24f, 0.28f, 0.60f);

    // Headers & Title Bars
    colors[ImGuiCol_TitleBg]              = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.20f, 0.22f, 0.26f, 0.70f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.30f, 0.37f, 0.80f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.30f, 0.35f, 0.43f, 1.00f);

    // Buttons (Accent: Sleek Blue)
    colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.39f, 0.65f, 0.80f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.22f, 0.47f, 0.78f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.15f, 0.34f, 0.58f, 1.00f);

    // Frame Controls (Checkboxes, Inputs, Sliders)
    colors[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.35f, 0.68f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.30f, 0.58f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.24f, 0.28f, 0.34f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.18f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.15f, 0.17f, 0.20f, 1.00f);

    // Text
    colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.53f, 0.58f, 1.00f);
}

void ImGuiLayer::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    float xscale = 1.0f;
    float yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);

    ImFontConfig font_config;
    font_config.OversampleH = 3; // Smooth horizontal anti-aliasing
    font_config.OversampleV = 3; // Smooth vertical anti-aliasing
    font_config.PixelSnapH = false;

    const char* font_path = "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf";
    float font_size = 15.0f * xscale; // Scaled font size

    ImFont* custom_font = io.Fonts->AddFontFromFileTTF(font_path, font_size, &font_config);

    if (!custom_font) {
        io.Fonts->AddFontDefault();
    }

    setupSpecialTheme(xscale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

