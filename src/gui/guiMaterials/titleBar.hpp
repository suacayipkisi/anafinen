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

#pragma once

struct GLFWwindow;

// Integrated title bar: the main menu bar doubles as the window's title bar.
//
//   [logo] File Analyze Panels Settings Help          Anafinen          [-] [□] [×]
//   |<------- menus (client) ------->|<------ drag region ------>|<- window buttons ->|
//
// The window system still does the moving, resizing, snapping and the window menu; the app only
// says where they start:
//
//   Linux (Wayland, X11)  GLFW_DECORATED off. The mouse button callback catches presses on the drag
//                         region and the resize border and hands them to the patched GLFW
//                         (glfwDragWindow / glfwResizeWindowFromEdge / glfwShowWindowMenu:
//                         xdg_toplevel.move / resize / show_window_menu, _NET_WM_MOVERESIZE).
//   Windows               the standard frame stays (shadow, Aero Snap, animations); WM_NCCALCSIZE
//                         removes only the caption and WM_NCHITTEST reports the drag region as
//                         HTCAPTION, the top border as HTTOP and the maximize button as HTMAXBUTTON
//                         (Windows 11 snap layouts).
//
// Settings > Title Bar switches between this and the window system's own title bar at run time.
// GUI thread only.
namespace anaf::GUI::TITLEBAR {

  // False where the system title bar always stays (macOS, a GLFW built without the anafinen patch).
  bool platformSupported();

  // Before glfwCreateWindow (after glfwInit): the decoration hint for the chosen title bar.
  void applyWindowHints(bool integrated);
  // After ImGuiLayer::init (wraps the mouse button callback the ImGui backend installed).
  void install(GLFWwindow* window, bool integrated);
  // Before ImGuiLayer::shutdown.
  void uninstall();

  // Integrated title bar in use (chosen and supported).
  bool integrated();
  void setIntegrated(bool on);

  // Vertical frame padding of the menu bar row: the title bar is taller than a plain menu bar.
  // Pushed (ImGuiStyleVar_FramePadding) around the host window's Begin and the menu bar items.
  float menuBarFramePaddingY();

  // Inside BeginMenuBar / EndMenuBar of the host window: beginMenuBar() before the first menu
  // (logo), endMenuBar() after the last one (title, window buttons, drag region). No-ops while the
  // system title bar is in use.
  void beginMenuBar();
  void endMenuBar();

  // After every panel of the frame: resize cursors on the window border and the window outline.
  void endFrame();

} // namespace anaf::GUI::TITLEBAR end
