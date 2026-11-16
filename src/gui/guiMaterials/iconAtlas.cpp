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

#include "iconAtlas.hpp"

#include "glHandle.hpp"
#include "theme.hpp"

#include <directory/getExecutableDirectory.hpp>
#include <io/core/pathUtf8.hpp>
#include <log/anaf_info.hpp>

// nanosvg (zlib license, external/nanosvg/): the implementation lives in this translation unit only.
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace anaf::GUI::ICONS {

  namespace {
#define ANAFINEN_ICON_FILE(name, file) file,
    constexpr std::array<const char*, iconCount> fileNames{ANAFINEN_ICON_LIST(ANAFINEN_ICON_FILE)};
#undef ANAFINEN_ICON_FILE

    struct SvgDeleter {
      void operator()(NSVGimage* image) const noexcept { nsvgDelete(image); }
    };
    struct RasterizerDeleter {
      void operator()(NSVGrasterizer* rasterizer) const noexcept { nsvgDeleteRasterizer(rasterizer); }
    };

    // Every icon at one pixel size, in a grid of cells with a one pixel transparent border (no bleeding
    // of the neighbours under linear filtering).
    struct Page {
      int pixelSize{0};
      int width{0};
      int height{0};
      int columns{0};
      GlTexture texture;
    };

    struct AtlasState {
      bool loaded{false};
      bool light{false};
      float scale{1.0f};
      std::array<std::unique_ptr<NSVGimage, SvgDeleter>, iconCount> images;
      std::vector<Page> pages;
    };

    AtlasState g_atlas;

    std::unique_ptr<NSVGimage, SvgDeleter> parseSvg(const std::filesystem::path& path) {
      // Read through std::filesystem (wide paths on Windows) instead of nsvgParseFromFile's fopen.
      std::ifstream file(path, std::ios::binary);
      if (!file) return nullptr;
      std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      if (text.empty()) return nullptr;
      std::unique_ptr<NSVGimage, SvgDeleter> image(nsvgParse(text.data(), "px", 96.0f)); // parses in place
      if (!image || image->width <= 0.0f || image->height <= 0.0f || !image->shapes) return nullptr;
      return image;
    }

    // Parses the dark or light set; a missing or broken file is reported once per set in one line.
    void loadSet(const bool light) {
      g_atlas.pages.clear();
      g_atlas.light = light;
      g_atlas.loaded = true;

      const char* setName = light ? "light" : "dark";
      const std::filesystem::path setSubpath = std::filesystem::path("icons") / "ui" / setName;
      std::string missing;
      std::size_t missingCount = 0;
      for (std::size_t i = 0; i < iconCount; ++i) {
        const std::filesystem::path resolved = anaf::DIRECTORY::findAssetPath(setSubpath / (std::string(fileNames[i]) + ".svg"));
        g_atlas.images[i] = resolved.empty() ? nullptr : parseSvg(resolved);
        if (!g_atlas.images[i]) {
          if (missingCount < 8) missing += (missingCount == 0 ? "" : ", ") + std::string(fileNames[i]);
          ++missingCount;
        }
      }
      if (missingCount > 0) {
        anaf::LOG::warn("[Icons] {} of {} icons missing or unreadable in the {} set ({}{}); their text labels are shown instead.",
                        missingCount, iconCount, setName, missing, missingCount > 8 ? ", ..." : "");
      }
    }

    void ensureLoaded() {
      const bool light = THEME::theme().light;
      if (!g_atlas.loaded || g_atlas.light != light) loadSet(light);
    }

    const Page& buildPage(const int pixelSize) {
      const int cell = pixelSize + 2;
      const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(iconCount))));
      const int rows = (static_cast<int>(iconCount) + columns - 1) / columns;
      Page page;
      page.pixelSize = pixelSize;
      page.columns = columns;
      page.width = columns * cell;
      page.height = rows * cell;

      std::vector<unsigned char> pixels(static_cast<std::size_t>(page.width) * static_cast<std::size_t>(page.height) * 4u, 0u);
      const std::unique_ptr<NSVGrasterizer, RasterizerDeleter> rasterizer(nsvgCreateRasterizer());
      const int stride = page.width * 4;
      for (std::size_t i = 0; i < iconCount && rasterizer; ++i) {
        NSVGimage* image = g_atlas.images[i].get();
        if (!image) continue;
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        const std::size_t offset = static_cast<std::size_t>((row * cell + 1) * stride + (column * cell + 1) * 4);
        // The art is drawn on a square viewBox (24 x 24); a non-square file keeps its aspect ratio.
        const float scale = static_cast<float>(pixelSize) / std::max(image->width, image->height);
        nsvgRasterize(rasterizer.get(), image, 0.0f, 0.0f, scale, pixels.data() + offset, pixelSize, pixelSize, stride);
      }

      // Straight (not premultiplied) alpha, like ImGui's OpenGL backend blends it.
      page.texture = createTexture(GL_TEXTURE_2D);
      const GLuint texture = page.texture.get();
      glTextureStorage2D(texture, 1, GL_RGBA8, page.width, page.height);
      glTextureSubImage2D(texture, 0, 0, 0, page.width, page.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      g_atlas.pages.push_back(std::move(page));
      return g_atlas.pages.back();
    }

    ImVec4 hoverColor(const float amount) {
      const THEME::ThemePalette& palette = THEME::theme();
      return THEME::mix(palette.panel, palette.text, amount);
    }
  } // namespace end

  void beginFrame() {
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const float scale = framebufferScale.x > 0.0f ? framebufferScale.x : 1.0f;
    if (scale != g_atlas.scale) {
      g_atlas.scale = scale;
      g_atlas.pages.clear();
    }
    ensureLoaded();
  }

  void release() {
    g_atlas.pages.clear();
    for (auto& image : g_atlas.images) image.reset();
    g_atlas.loaded = false;
  }

  std::optional<IconImage> icon(const E_IconId id, const float size) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= iconCount) return std::nullopt;
    ensureLoaded();
    if (!g_atlas.images[index]) return std::nullopt;

    const int pixelSize = std::max(1, static_cast<int>(std::lround(size * g_atlas.scale)));
    const auto found = std::ranges::find(g_atlas.pages, pixelSize, &Page::pixelSize);
    const Page& page = found != g_atlas.pages.end() ? *found : buildPage(pixelSize);

    const int cell = pixelSize + 2;
    const int column = static_cast<int>(index) % page.columns;
    const int row = static_cast<int>(index) / page.columns;
    const auto width = static_cast<float>(page.width);
    const auto height = static_cast<float>(page.height);
    const float x0 = static_cast<float>(column * cell + 1);
    const float y0 = static_cast<float>(row * cell + 1);
    const auto textureId = static_cast<ImTextureID>(static_cast<std::uintptr_t>(page.texture.get()));
    return IconImage{ImTextureRef(textureId), ImVec2(x0 / width, y0 / height),
                     ImVec2((x0 + static_cast<float>(pixelSize)) / width, (y0 + static_cast<float>(pixelSize)) / height)};
  }

  float toolbarSize() { return std::round(ImGui::GetFontSize() * 4.0f / 3.0f); }
  float rowSize() { return std::round(ImGui::GetFontSize()); }

  bool drawIcon(ImDrawList* drawList, const E_IconId id, const ImVec2 min, const float size) {
    const std::optional<IconImage> image = icon(id, size);
    if (!image) return false;
    // Whole pixels: the texture holds the icon at exactly size x scale pixels.
    const ImVec2 snapped(std::round(min.x), std::round(min.y));
    drawList->AddImage(image->texture, snapped, ImVec2(snapped.x + size, snapped.y + size), image->uv0, image->uv1);
    return true;
  }

  bool iconButton(const char* strId, const E_IconId id, const bool selected, const char* fallbackLabel, const bool dropdown) {
    const THEME::ThemePalette& palette = THEME::theme();
    const float size = toolbarSize();
    const ImVec4 selectedColor = THEME::mix(palette.input, palette.accent, 0.22f);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? selectedColor : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? THEME::mix(palette.input, palette.accent, 0.30f) : hoverColor(0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, selected ? THEME::mix(palette.input, palette.accent, 0.36f) : hoverColor(0.16f));

    bool pressed = false;
    if (const std::optional<IconImage> image = icon(id, size)) {
      pressed = ImGui::ImageButton(strId, image->texture, ImVec2(size, size), image->uv0, image->uv1);
    } else {
      const std::string label = std::string(fallbackLabel) + "##" + strId;
      pressed = ImGui::Button(label.c_str(), ImVec2(0.0f, size + 2.0f * ImGui::GetStyle().FramePadding.y));
    }
    ImGui::PopStyleColor(3);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (selected) drawList->AddRectFilled(ImVec2(min.x, max.y - 2.0f), max, THEME::toU32(palette.accent));
    if (dropdown) {
      // Small triangle in the bottom right corner, above the selection line.
      const float side = std::max(3.0f, std::round(size * 0.22f));
      const ImVec2 corner(max.x - 2.0f, max.y - 3.0f);
      drawList->AddTriangleFilled(ImVec2(corner.x, corner.y - side), corner, ImVec2(corner.x - side, corner.y),
                                  THEME::toU32(palette.textDim));
    }
    return pressed;
  }

  void iconText(const E_IconId id, const char* text) {
    const float size = rowSize();
    if (const std::optional<IconImage> image = icon(id, size)) {
      ImGui::Image(image->texture, ImVec2(size, size), image->uv0, image->uv1);
      ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::TextUnformatted(text);
  }

  bool iconTreeNode(const char* strId, const E_IconId id, const char* label, const ImGuiTreeNodeFlags flags) {
    const float size = rowSize();
    const std::optional<IconImage> image = icon(id, size);
    if (!image) return ImGui::TreeNodeEx(strId, flags, "%s", label);

    // Empty label, the whole row is its hit area; icon and text follow as plain (non-interactive) items.
    const bool open = ImGui::TreeNodeEx(strId, flags | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", "");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Image(image->texture, ImVec2(size, size), image->uv0, image->uv1);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
    return open;
  }

  bool iconSelectable(const E_IconId id, const char* label, const bool selected) {
    const float size = rowSize();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = size + gap + ImGui::CalcTextSize(label).x;
    ImGui::PushID(label);
    const bool clicked = ImGui::Selectable("##iconSelectable", selected, ImGuiSelectableFlags_None, ImVec2(width, 0.0f));
    ImGui::PopID();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float textX = drawIcon(drawList, id, min, size) ? min.x + size + gap : min.x;
    drawList->AddText(ImVec2(textX, min.y), ImGui::GetColorU32(ImGuiCol_Text), label);
    return clicked;
  }

} // namespace anaf::GUI::ICONS end
