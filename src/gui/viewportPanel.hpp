#pragma once



#include "iPanel.hpp"
#include "framebuffer.hpp"
#include "imgui.h"
#include <cstdint>
#include <memory>

class ViewportPanel : public IPanel {
private:
    std::shared_ptr<Framebuffer> m_fbo_ ;
    bool m_viewportFocused_ {false};
    bool m_viewportHovered_ {false};
public:
    ViewportPanel(std::shared_ptr<Framebuffer> fbo) : m_fbo_(fbo) {}

    void onImGuiRender() override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("3D Simulation Viewport", &isOpen);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

        if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {

            std::uint32_t w = static_cast<uint32_t>(viewportPanelSize.x);
            std::uint32_t h = static_cast<uint32_t>(viewportPanelSize.y);

            m_fbo_->resize(w, h);
        }

        std::uint32_t textureID = m_fbo_->getTextureID();
        //std::cout << "[DEBUG] Viewport Texture ID: " << textureID << std::endl;
        if (textureID > 0) {
            ImGui::Image(
                static_cast<ImTextureID>(textureID),
                viewportPanelSize, 
                ImVec2(0, 1), 
                ImVec2(1, 0)
            );
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    bool isFocused() const { return m_viewportFocused_; }
    bool isHovered() const { return m_viewportHovered_; }
    
};
