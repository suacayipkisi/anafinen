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

#include <memory>
#include <utility>
#include <vector>

namespace anaf::GUI {

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

} //namespace anaf::GUI end
