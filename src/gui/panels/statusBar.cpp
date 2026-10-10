// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
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

#include "statusBar.hpp"

#include <guiMaterials/theme.hpp>

#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>
#include <platform/systemInfo.hpp>

#include <glad/gl.h>
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <format>
#include <optional>
#include <string_view>

namespace anaf::GUI {

  namespace {
    // GL_NVX_gpu_memory_info: NVIDIA and Mesa (radeonsi, ...). Value in KiB.
    constexpr GLenum gpuMemoryDedicatedNvx = 0x9047;

    bool hasGlExtension(const std::string_view name) {
      GLint count = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &count);
      for (GLint i = 0; i < count; ++i) {
        const auto* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension && name == extension) return true;
      }
      return false;
    }

    // "16 GB", "13.3 GB": one decimal only when it says something.
    std::string gigabytes(const double value) {
      if (std::abs(value - std::round(value)) < 0.05) return std::format("{:.0f} GB", value);
      return std::format("{:.1f} GB", value);
    }
  } // namespace end

  void StatusBar::queryHardware() {
    const auto system = PLATFORM::querySystemInfo();

    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string rendererName = renderer ? renderer : "";
    std::optional<double> vramGiB;
    if (hasGlExtension("GL_NVX_gpu_memory_info")) {
      GLint kib = 0;
      glGetIntegerv(gpuMemoryDedicatedNvx, &kib);
      if (kib > 0) vramGiB = static_cast<double>(kib) / (1024.0 * 1024.0);
    }
    if (!vramGiB) vramGiB = PLATFORM::queryVideoMemoryGiB(rendererName);

    m_hardware = std::format("{} {}T", system.cpuName, system.threads);
    if (!rendererName.empty()) {
      m_hardware += "  |  " + PLATFORM::shortGpuName(rendererName);
      if (vramGiB) m_hardware += " " + gigabytes(*vramGiB);
    }
    if (system.totalRamGiB > 0.0) m_hardware += "  |  " + gigabytes(system.totalRamGiB) + " RAM";
    anaf::LOG::info("Hardware: {} | GL renderer: {}", m_hardware, rendererName);
  }

  float StatusBar::height() {
    return ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  }

  void StatusBar::render() {
    if (m_hardware.empty()) queryHardware();

    const auto& bridge = BRIDGE::buildBridge();
    const bool solving = bridge.isRunning.load();
    const bool previewing = bridge.isGeneratingPreview.load();

    ImGui::Separator();

    // Left: worker state, then the hardware summary right next to it.
    const ImVec4 accent = (solving || previewing) ? THEME::theme().warn : THEME::theme().good;
    char state[32];
    if (solving) std::snprintf(state, sizeof(state), "SOLVING %.0f%%", static_cast<double>(bridge.progress.load() * 100.0f));
    else if (previewing) std::snprintf(state, sizeof(state), "PREVIEW");
    else std::snprintf(state, sizeof(state), "IDLE");
    ImGui::TextColored(accent, "%s", state);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_hardware.c_str());

    // Right: live usage, right-aligned; stays on the same row and is clipped when narrow.
    const auto& usage = m_monitor.sample();
    if (!usage.available) return;
    char resources[96];
    std::snprintf(resources, sizeof(resources), "CPU %.0f%%  Mem %.0f MiB  Sys RAM %.0f%%",
                  static_cast<double>(usage.processCpuPercent), static_cast<double>(usage.processRamMiB),
                  static_cast<double>(usage.systemRamPercent));
    ImGui::SameLine();
    const float width = ImGui::CalcTextSize(resources).x;
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    if (right > ImGui::GetCursorPosX()) ImGui::SameLine(right);
    ImGui::TextColored(accent, "%s", resources);
  }

} // namespace anaf::GUI end
