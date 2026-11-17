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

#include "titleBar.hpp"

#include "iconAtlas.hpp"
#include "theme.hpp"

#include <log/anaf_info.hpp>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10: GetDpiForWindow, GetSystemMetricsForDpi, AdjustWindowRectExForDpi
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>
#endif

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "imgui.h"
#include "imgui_internal.h" // MenuBarRect, OpenPopupStack

#include <algorithm>
#include <array>
#include <cmath>

namespace anaf::GUI::TITLEBAR {

  namespace {

    enum class E_WindowButton : int { Minimize = 0, Maximize = 1, Close = 2 };
    constexpr int windowButtonCount = 3;

    struct TitleBarState {
      GLFWwindow* window{nullptr};
      bool wanted{true}; // the user's choice (Settings > Title Bar)
      GLFWmousebuttonfun previousMouseButton{nullptr};

      // Layout of the last frame in window coordinates (= ImGui coordinates; no multi-viewport).
      // Read by the mouse button callback / window procedure, which run inside glfwPollEvents on
      // the GUI thread, between two frames.
      bool barValid{false};
      ImVec2 barMin{};
      ImVec2 barMax{};
      float menusEndX{0.0f};     // the logo and the menus occupy [barMin.x, menusEndX)
      float buttonsStartX{0.0f}; // the window buttons occupy [buttonsStartX, barMax.x)
      std::array<ImRect, windowButtonCount> buttons{};
      bool menuPopupOpen{false}; // a click on the bar closes it (ImGui gets the press) instead of dragging
      int tooltipButton{-1};
      double tooltipSince{0.0};

      // Double click on the drag region (Linux; Windows does it from HTCAPTION itself).
      double lastDragPressTime{-1.0};
      double lastDragPressX{0.0};
      double lastDragPressY{0.0};

#ifdef _WIN32
      WNDPROC originalProc{nullptr};
      // The maximize button is HTMAXBUTTON (snap layouts), so its mouse input arrives as
      // non-client messages and never reaches ImGui.
      bool maximizeHovered{false};
      bool maximizePressed{false};
#endif
    };

    TitleBarState g_titleBar;

    bool isLinuxWindowSystem() {
      const int platform = glfwGetPlatform();
      return platform == GLFW_PLATFORM_WAYLAND || platform == GLFW_PLATFORM_X11;
    }

    bool windowMaximized() { return glfwGetWindowAttrib(g_titleBar.window, GLFW_MAXIMIZED) == GLFW_TRUE; }

    void toggleMaximized() {
      if (windowMaximized()) glfwRestoreWindow(g_titleBar.window);
      else glfwMaximizeWindow(g_titleBar.window);
    }

    // Empty part of the title bar: between the menus and the window buttons, no menu open.
    bool inDragRegion(const double x, const double y) {
      const TitleBarState& s = g_titleBar;
      if (!s.barValid || s.menuPopupOpen) return false;
      if (y < s.barMin.y || y >= s.barMax.y) return false;
      return x >= s.menusEndX && x < s.buttonsStartX;
    }

    // A menu (or another non-modal popup) is open. Modal dialogs do not count: the window can be
    // moved while one is open, as with a native title bar.
    bool nonModalPopupOpen() {
      const ImGuiContext& context = *ImGui::GetCurrentContext();
      for (const ImGuiPopupData& popup : context.OpenPopupStack) {
        if (popup.Window == nullptr || !(popup.Window->Flags & ImGuiWindowFlags_Modal)) return true;
      }
      return false;
    }

    float barHeightFor(const float fontSize) { return std::round(fontSize * 2.1f); }

#if defined(GLFW_ANAFINEN_CLIENT_DECORATIONS)
    // Width of the invisible resize border inside the window (Linux: the window has no frame).
    double resizeBorder() {
      float xscale = 1.0f, yscale = 1.0f;
      // X11 window coordinates are pixels; Wayland's are already logical.
      if (glfwGetPlatform() == GLFW_PLATFORM_X11) glfwGetWindowContentScale(g_titleBar.window, &xscale, &yscale);
      return 6.0 * static_cast<double>(xscale);
    }

    // GLFW_WINDOW_EDGE_* under the cursor, 0 inside. Corners reach three borders along each edge.
    int resizeEdgesAt(const double x, const double y) {
      if (windowMaximized() || glfwGetWindowMonitor(g_titleBar.window) != nullptr) return 0;
      if (glfwGetWindowAttrib(g_titleBar.window, GLFW_RESIZABLE) != GLFW_TRUE) return 0;
      int width = 0, height = 0;
      glfwGetWindowSize(g_titleBar.window, &width, &height);
      if (x < 0.0 || y < 0.0 || x >= width || y >= height) return 0;

      const double border = resizeBorder();
      const double corner = border * 3.0;
      int edges = 0;
      if (y < border) edges |= GLFW_WINDOW_EDGE_TOP;
      else if (y >= height - border) edges |= GLFW_WINDOW_EDGE_BOTTOM;
      if (x < border) edges |= GLFW_WINDOW_EDGE_LEFT;
      else if (x >= width - border) edges |= GLFW_WINDOW_EDGE_RIGHT;

      if (edges == GLFW_WINDOW_EDGE_TOP || edges == GLFW_WINDOW_EDGE_BOTTOM) {
        if (x < corner) edges |= GLFW_WINDOW_EDGE_LEFT;
        else if (x >= width - corner) edges |= GLFW_WINDOW_EDGE_RIGHT;
      } else if (edges == GLFW_WINDOW_EDGE_LEFT || edges == GLFW_WINDOW_EDGE_RIGHT) {
        if (y < corner) edges |= GLFW_WINDOW_EDGE_TOP;
        else if (y >= height - corner) edges |= GLFW_WINDOW_EDGE_BOTTOM;
      }
      return edges;
    }

    // Press on the resize border or the drag region: handed to the window system, ImGui never sees it.
    // Called from the press event itself, so the compositor gets the serial of this very press.
    bool handlePress(const int button) {
      double x = 0.0, y = 0.0;
      glfwGetCursorPos(g_titleBar.window, &x, &y);

      if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (const int edges = resizeEdgesAt(x, y); edges != 0) {
          glfwResizeWindowFromEdge(g_titleBar.window, edges);
          return true;
        }
      }
      if (!inDragRegion(x, y)) return false;

      if (button == GLFW_MOUSE_BUTTON_LEFT) {
        const ImGuiIO& io = ImGui::GetIO();
        const double now = glfwGetTime();
        const double dx = x - g_titleBar.lastDragPressX;
        const double dy = y - g_titleBar.lastDragPressY;
        const bool doubleClick = g_titleBar.lastDragPressTime >= 0.0 && now - g_titleBar.lastDragPressTime <= io.MouseDoubleClickTime &&
                                 std::hypot(dx, dy) <= static_cast<double>(io.MouseDoubleClickMaxDist);
        if (doubleClick) {
          g_titleBar.lastDragPressTime = -1.0;
          toggleMaximized();
        } else {
          g_titleBar.lastDragPressTime = now;
          g_titleBar.lastDragPressX = x;
          g_titleBar.lastDragPressY = y;
          glfwDragWindow(g_titleBar.window);
        }
        return true;
      }
      if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        glfwShowWindowMenu(g_titleBar.window);
        return true;
      }
      return false;
    }

    // Sits in front of the ImGui backend's callback (which it forwards to unless the press was taken).
    void mouseButtonCallback(GLFWwindow* window, const int button, const int action, const int mods) {
      if (action == GLFW_PRESS && window == g_titleBar.window && integrated() && isLinuxWindowSystem() && handlePress(button)) return;
      if (g_titleBar.previousMouseButton) g_titleBar.previousMouseButton(window, button, action, mods);
    }
#endif // GLFW_ANAFINEN_CLIENT_DECORATIONS

#ifdef _WIN32
    // Resize frame thickness at the window's DPI (what DefWindowProc puts around the client area).
    int frameThicknessY(HWND hwnd) {
      const UINT dpi = GetDpiForWindow(hwnd);
      return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    }

    // A maximized window must leave a sliver of the monitor free, else an auto-hide taskbar on that
    // edge can never be revealed (as Windows Terminal does).
    void leaveRoomForAutoHideTaskbar(HWND hwnd, RECT& client) {
      APPBARDATA state{};
      state.cbSize = sizeof(state);
      if ((SHAppBarMessage(ABM_GETSTATE, &state) & ABS_AUTOHIDE) == 0) return;

      HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
      MONITORINFO info{};
      info.cbSize = sizeof(info);
      if (!GetMonitorInfoW(monitor, &info)) return;

      constexpr LONG sliver = 2;
      const std::array<std::pair<UINT, LONG*>, 4> edges{{{ABE_TOP, &client.top},
                                                          {ABE_BOTTOM, &client.bottom},
                                                          {ABE_LEFT, &client.left},
                                                          {ABE_RIGHT, &client.right}}};
      for (const auto& [edge, side] : edges) {
        APPBARDATA bar{};
        bar.cbSize = sizeof(bar);
        bar.uEdge = edge;
        bar.rc = info.rcMonitor;
        if (SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar) == 0) continue;
        *side += (edge == ABE_TOP || edge == ABE_LEFT) ? sliver : -sliver;
      }
    }

    bool insideRect(const ImRect& rect, const double x, const double y) {
      return x >= rect.Min.x && x < rect.Max.x && y >= rect.Min.y && y < rect.Max.y;
    }

    LRESULT callOriginal(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
      return CallWindowProcW(g_titleBar.originalProc, hwnd, message, wParam, lParam);
    }

    // Left, right and bottom keep the standard frame (shadow, resize borders outside the visible edge,
    // Aero Snap); only the caption and the top border go, the menu bar row takes their place.
    LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
      if (!integrated()) return callOriginal(hwnd, message, wParam, lParam);

      switch (message) {
        case WM_NCCALCSIZE: {
          if (wParam == FALSE) break;
          auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
          const LONG originalTop = params->rgrc[0].top;
          const LRESULT result = callOriginal(hwnd, message, wParam, lParam);
          if (result != 0) return result;
          params->rgrc[0].top = originalTop;
          if (IsZoomed(hwnd)) {
            // A maximized window overhangs the monitor by its frame on every side.
            params->rgrc[0].top += frameThicknessY(hwnd);
            leaveRoomForAutoHideTaskbar(hwnd, params->rgrc[0]);
          }
          return 0;
        }

        case WM_NCHITTEST: {
          const LRESULT standard = callOriginal(hwnd, message, wParam, lParam);
          POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
          ScreenToClient(hwnd, &point);
          const bool maximized = IsZoomed(hwnd) != FALSE;
          const int border = frameThicknessY(hwnd);
          if (standard != HTCLIENT) {
            // The side borders reach up into the old caption height: their top part is the corner.
            if (!maximized && point.y < border * 2) {
              if (standard == HTLEFT) return HTTOPLEFT;
              if (standard == HTRIGHT) return HTTOPRIGHT;
            }
            return standard;
          }
          if (!maximized && point.y < border) return HTTOP;
          const double x = point.x, y = point.y;
          if (g_titleBar.barValid && insideRect(g_titleBar.buttons[static_cast<std::size_t>(E_WindowButton::Maximize)], x, y)) return HTMAXBUTTON;
          if (inDragRegion(x, y)) return HTCAPTION;
          return HTCLIENT;
        }

        case WM_NCMOUSEMOVE: {
          const bool hovered = wParam == HTMAXBUTTON;
          if (hovered && !g_titleBar.maximizeHovered) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE | TME_NONCLIENT;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
          }
          g_titleBar.maximizeHovered = hovered;
          if (!hovered) g_titleBar.maximizePressed = false;
          break;
        }

        case WM_NCMOUSELEAVE:
          g_titleBar.maximizeHovered = false;
          g_titleBar.maximizePressed = false;
          break;

        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
          // DefWindowProc would track the press on a classic caption button that is not there.
          if (wParam == HTMAXBUTTON) {
            g_titleBar.maximizePressed = true;
            return 0;
          }
          break;

        case WM_NCLBUTTONUP:
          if (wParam == HTMAXBUTTON) {
            if (g_titleBar.maximizePressed) toggleMaximized();
            g_titleBar.maximizePressed = false;
            return 0;
          }
          break;

        case WM_NCACTIVATE:
          // lParam -1: keep the activation change but skip painting a caption over the client area.
          return DefWindowProcW(hwnd, message, wParam, -1);

        case WM_GETDPISCALEDSIZE: {
          // GLFW keeps the content size and adds the frame delta, caption included; this frame has
          // no caption, so only the side and bottom borders scale.
          const auto style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
          const auto exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
          const auto frame = [&](const UINT dpi) {
            RECT rect{};
            AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi);
            return SIZE{rect.right - rect.left, rect.bottom};
          };
          const SIZE before = frame(GetDpiForWindow(hwnd));
          const SIZE after = frame(static_cast<UINT>(LOWORD(wParam)));
          auto* size = reinterpret_cast<SIZE*>(lParam);
          size->cx += after.cx - before.cx;
          size->cy += after.cy - before.cy;
          return TRUE;
        }

        default: break;
      }
      return callOriginal(hwnd, message, wParam, lParam);
    }

    // Recomputes the non-client area after the title bar mode changed.
    void refreshFrame() {
      HWND hwnd = glfwGetWin32Window(g_titleBar.window);
      if (!hwnd) return;
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
#endif // _WIN32

    // Glyphs of the window buttons, drawn with lines so they stay crisp at every size.
    void drawButtonGlyph(ImDrawList* drawList, const E_WindowButton button, const ImVec2 center, const float size, const ImU32 color,
                         const bool maximized) {
      const float half = std::floor(size * 0.5f);
      const float thickness = std::max(1.0f, std::round(size / 10.0f));
      const ImVec2 c{std::floor(center.x) + 0.5f, std::floor(center.y) + 0.5f};
      switch (button) {
        case E_WindowButton::Minimize:
          drawList->AddLine(ImVec2(c.x - half, c.y), ImVec2(c.x + half, c.y), color, thickness);
          break;
        case E_WindowButton::Maximize:
          if (maximized) {
            // Restore: two overlapping squares.
            const float offset = std::round(size * 0.2f);
            drawList->AddRect(ImVec2(c.x - half, c.y - half + offset), ImVec2(c.x + half - offset, c.y + half), color, 0.0f, 0, thickness);
            drawList->PathLineTo(ImVec2(c.x - half + offset, c.y - half + offset));
            drawList->PathLineTo(ImVec2(c.x - half + offset, c.y - half));
            drawList->PathLineTo(ImVec2(c.x + half, c.y - half));
            drawList->PathLineTo(ImVec2(c.x + half, c.y + half - offset));
            drawList->PathLineTo(ImVec2(c.x + half - offset, c.y + half - offset));
            drawList->PathStroke(color, 0, thickness);
          } else {
            drawList->AddRect(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), color, 0.0f, 0, thickness);
          }
          break;
        case E_WindowButton::Close:
          drawList->AddLine(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), color, thickness);
          drawList->AddLine(ImVec2(c.x - half, c.y + half), ImVec2(c.x + half, c.y - half), color, thickness);
          break;
      }
    }

    // The buttons are hit tested by hand rather than as ImGui items, so they keep working while a
    // modal dialog blocks the rest of the UI (as the buttons of a native title bar do).
    void windowButtons(const ImRect bar) {
      TitleBarState& s = g_titleBar;
      const ImGuiIO& io = ImGui::GetIO();
      const THEME::ThemePalette& palette = THEME::theme();
      ImDrawList* drawList = ImGui::GetWindowDrawList();

      const float height = bar.GetHeight();
      const float width = std::round(height * 1.45f);
      const float glyph = std::round(ImGui::GetFontSize() * 0.66f);
      const bool maximized = windowMaximized();
      const bool focused = glfwGetWindowAttrib(s.window, GLFW_FOCUSED) == GLFW_TRUE;
      s.buttonsStartX = bar.Max.x - width * windowButtonCount;
      int hoveredButton = -1;

      for (int i = 0; i < windowButtonCount; ++i) {
        const auto button = static_cast<E_WindowButton>(i);
        const ImRect rect(ImVec2(s.buttonsStartX + width * static_cast<float>(i), bar.Min.y),
                          ImVec2(s.buttonsStartX + width * static_cast<float>(i + 1), bar.Max.y));
        s.buttons[static_cast<std::size_t>(i)] = rect;

        const bool mouseInside = ImGui::IsMousePosValid() && rect.Contains(io.MousePos);
        const bool pressStartedHere = rect.Contains(io.MouseClickedPos[ImGuiMouseButton_Left]);
        bool hovered = mouseInside;
        bool held = mouseInside && pressStartedHere && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool clicked = mouseInside && pressStartedHere && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
#ifdef _WIN32
        if (button == E_WindowButton::Maximize) {
          hovered = s.maximizeHovered;
          held = s.maximizePressed;
        }
#endif
        const bool close = button == E_WindowButton::Close;
        if (hovered || held) {
          // Close turns red like every native close button.
          // The others: the text color over the bar, faint (as GTK and Windows 11 caption buttons).
          ImVec4 overlay = palette.text;
          overlay.w = held ? 0.16f : 0.09f;
          const ImU32 background = close ? (held ? IM_COL32(148, 20, 26, 255) : IM_COL32(196, 43, 28, 255))
                                         : ImGui::ColorConvertFloat4ToU32(overlay);
          drawList->AddRectFilled(rect.Min, rect.Max, background);
        }
        const ImVec4 glyphColor = (close && (hovered || held)) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : (focused ? palette.text : palette.textDim);
        drawButtonGlyph(drawList, button, rect.GetCenter(), glyph, ImGui::ColorConvertFloat4ToU32(glyphColor), maximized);

        if (clicked) {
          switch (button) {
            case E_WindowButton::Minimize: glfwIconifyWindow(s.window); break;
            case E_WindowButton::Maximize: toggleMaximized(); break;
            case E_WindowButton::Close: glfwSetWindowShouldClose(s.window, GLFW_TRUE); break;
          }
        }
        if (hovered && !held) hoveredButton = i;
      }

      // Tooltip after the usual delay, as on native caption buttons.
      if (hoveredButton != s.tooltipButton) {
        s.tooltipButton = hoveredButton;
        s.tooltipSince = ImGui::GetTime();
      }
      if (hoveredButton >= 0 && ImGui::GetTime() - s.tooltipSince >= static_cast<double>(ImGui::GetStyle().HoverDelayNormal)) {
        static constexpr std::array<const char*, windowButtonCount> tooltips{"Minimize", "Maximize", "Close"};
        const bool restore = hoveredButton == static_cast<int>(E_WindowButton::Maximize) && maximized;
        ImGui::SetTooltip("%s", restore ? "Restore Down" : tooltips[static_cast<std::size_t>(hoveredButton)]);
      }
    }

  } // namespace end

  bool platformSupported() {
#ifdef _WIN32
    return glfwGetPlatform() == GLFW_PLATFORM_WIN32;
#elif defined(GLFW_ANAFINEN_CLIENT_DECORATIONS)
    return isLinuxWindowSystem();
#else
    return false;
#endif
  }

  void applyWindowHints(const bool integrated) {
    // Windows keeps its standard frame (the caption is removed in WM_NCCALCSIZE instead).
    if (isLinuxWindowSystem()) glfwWindowHint(GLFW_DECORATED, (integrated && platformSupported()) ? GLFW_FALSE : GLFW_TRUE);
  }

  void install(GLFWwindow* window, const bool integrated) {
    g_titleBar.window = window;
    g_titleBar.wanted = integrated;
    if (!platformSupported()) {
      if (integrated) anaf::LOG::info("[GUI] Integrated title bar not available on this window system; using the system title bar.");
      return;
    }
#if defined(GLFW_ANAFINEN_CLIENT_DECORATIONS)
    if (isLinuxWindowSystem()) g_titleBar.previousMouseButton = glfwSetMouseButtonCallback(window, mouseButtonCallback);
#endif
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    g_titleBar.originalProc =
      reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
    refreshFrame();
#endif
  }

  void uninstall() {
    if (!g_titleBar.window) return;
#if defined(GLFW_ANAFINEN_CLIENT_DECORATIONS)
    // Hands the ImGui backend its own callback back before it restores the previous ones.
    if (g_titleBar.previousMouseButton) glfwSetMouseButtonCallback(g_titleBar.window, g_titleBar.previousMouseButton);
    g_titleBar.previousMouseButton = nullptr;
#endif
#ifdef _WIN32
    if (g_titleBar.originalProc) {
      SetWindowLongPtrW(glfwGetWin32Window(g_titleBar.window), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_titleBar.originalProc));
      g_titleBar.originalProc = nullptr;
    }
#endif
    g_titleBar = TitleBarState{};
  }

  bool integrated() { return g_titleBar.window != nullptr && g_titleBar.wanted && platformSupported(); }

  void setIntegrated(const bool on) {
    if (g_titleBar.wanted == on) return;
    g_titleBar.wanted = on;
    g_titleBar.barValid = false;
    if (!g_titleBar.window || !platformSupported()) return;
    if (isLinuxWindowSystem()) glfwSetWindowAttrib(g_titleBar.window, GLFW_DECORATED, on ? GLFW_FALSE : GLFW_TRUE);
#ifdef _WIN32
    refreshFrame();
#endif
  }

  float menuBarFramePaddingY() {
    const float themePadding = ImGui::GetStyle().FramePadding.y;
    if (!integrated()) return themePadding;
    const float fontSize = ImGui::GetFontSize();
    return std::max(themePadding, std::floor((barHeightFor(fontSize) - fontSize) * 0.5f));
  }

  void beginMenuBar() {
    if (!integrated()) return;
    const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();
    const float size = std::round(ImGui::GetFontSize() * 1.2f);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 logoMin(cursor.x, std::floor(bar.Min.y + (bar.GetHeight() - size) * 0.5f));
    if (ICONS::drawIcon(ImGui::GetWindowDrawList(), ICONS::E_IconId::AppLogo, logoMin, size)) {
      ImGui::Dummy(ImVec2(size + ImGui::GetStyle().ItemSpacing.x * 0.5f, ImGui::GetFontSize()));
    }
  }

  void endMenuBar() {
    TitleBarState& s = g_titleBar;
    if (!integrated()) {
      s.barValid = false;
      return;
    }
    const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();
    s.barMin = bar.Min;
    s.barMax = bar.Max;
    s.menusEndX = ImGui::GetCursorScreenPos().x;
    s.menuPopupOpen = nonModalPopupOpen();

    windowButtons(bar);

    // Title in the middle of the window, pushed aside by the menus and the buttons; dim when inactive.
    const char* title = glfwGetWindowTitle(s.window);
    if (title != nullptr && *title != '\0') {
      const float gap = ImGui::GetStyle().ItemSpacing.x * 3.0f;
      const ImVec2 textSize = ImGui::CalcTextSize(title);
      const float lowest = s.menusEndX + gap;
      const float highest = s.buttonsStartX - gap - textSize.x;
      if (lowest <= highest) {
        const float x = std::clamp(std::floor((bar.Min.x + bar.Max.x - textSize.x) * 0.5f), lowest, highest);
        const float y = std::floor(bar.Min.y + (bar.GetHeight() - textSize.y) * 0.5f);
        const bool focused = glfwGetWindowAttrib(s.window, GLFW_FOCUSED) == GLFW_TRUE;
        const THEME::ThemePalette& palette = THEME::theme();
        ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(focused ? palette.text : palette.textDim), title);
      }
    }
    s.barValid = true;
  }

  void endFrame() {
    if (!integrated() || !isLinuxWindowSystem()) return;
#if defined(GLFW_ANAFINEN_CLIENT_DECORATIONS)
    const ImGuiIO& io = ImGui::GetIO();
    // Resize cursor over the border (set last, so it wins over the widget below).
    if (ImGui::IsMousePosValid() && !ImGui::IsAnyMouseDown()) {
      switch (resizeEdgesAt(io.MousePos.x, io.MousePos.y)) {
        case GLFW_WINDOW_EDGE_TOP:
        case GLFW_WINDOW_EDGE_BOTTOM: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); break;
        case GLFW_WINDOW_EDGE_LEFT:
        case GLFW_WINDOW_EDGE_RIGHT: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
        case GLFW_WINDOW_EDGE_TOP | GLFW_WINDOW_EDGE_LEFT:
        case GLFW_WINDOW_EDGE_BOTTOM | GLFW_WINDOW_EDGE_RIGHT: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
        case GLFW_WINDOW_EDGE_TOP | GLFW_WINDOW_EDGE_RIGHT:
        case GLFW_WINDOW_EDGE_BOTTOM | GLFW_WINDOW_EDGE_LEFT: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW); break;
        default: break;
      }
    }
    // A frameless window needs its own edge against the windows behind it (not when it fills the screen).
    if (!windowMaximized() && glfwGetWindowMonitor(g_titleBar.window) == nullptr) {
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImGui::GetForegroundDrawList(viewport)->AddRect(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                                                     ImGui::ColorConvertFloat4ToU32(THEME::theme().border));
    }
#endif
  }

} // namespace anaf::GUI::TITLEBAR end
