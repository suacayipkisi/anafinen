#pragma once

#include <memory>
#include <utility>
#include <vector>

class IPanel {
public:
    virtual ~IPanel() = default;
    virtual void onImGuiRender() = 0;

    bool isOpen = true;
};

class PanelManager {
private:
    std::vector<std::shared_ptr<IPanel>> m_panels_ ;
public:
    template<typename  T, typename ... Args>
    std::shared_ptr<T> addPanel(Args&& ... args) {
        auto panel = std::make_shared<T>(std::forward<Args>(args) ...);
        m_panels_.push_back(panel);
        return panel;
    }

    void onImGuiRender() {
        for (auto& panel : m_panels_) {
            if (panel->isOpen) {
                panel->onImGuiRender();
            }
        }
    }
};
