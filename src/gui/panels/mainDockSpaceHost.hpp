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

#include <guiMaterials/iPanel.hpp>

#include <GLFW/glfw3.h>

#include <functional>

namespace anaf::GUI {

    enum AnalyzeStructureType {
        Truss_1D,
        Truss_3D
    };

    class MainDockSpaceHost : public IPanel {
    private:
        GLFWwindow* m_window_;

    public:
        std::function<void(AnalyzeStructureType)> on_select_analyze_structure;
        std::function<void()> on_import_mesh;
        std::function<void()> on_export_results;
        std::function<void()> on_run_solver;

        MainDockSpaceHost(GLFWwindow* window) : m_window_(window) {}

        void onImGuiRender() override;
    };

} // namespace anaf:GUI end
