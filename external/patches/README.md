# Patches to external sources

The submodules under `external/` stay exactly as released. A patch here is applied by CMake to a
copy of the submodule inside the build tree (`<build>/_deps/<name>-patched/`). It is applied again
whenever the patch file or the submodule commit changes.

## glfw-3.4-anafinen.patch

Applies to GLFW 3.4 (`external/glfw`, tag `3.4`). It adds what an application needs to draw its own
title bar while the window system still moves, resizes and snaps the window:

| Function | Wayland | X11 | Win32 |
|---|---|---|---|
| `glfwDragWindow` | `xdg_toplevel.move` | `_NET_WM_MOVERESIZE` (move) | `WM_NCLBUTTONDOWN` + `HTCAPTION` |
| `glfwResizeWindowFromEdge` | `xdg_toplevel.resize` | `_NET_WM_MOVERESIZE` (size) | `WM_NCLBUTTONDOWN` + `HT*` border |
| `glfwShowWindowMenu` | `xdg_toplevel.show_window_menu` | `_GTK_SHOW_WINDOW_MENU` | system menu (`TrackPopupMenu`) |

- `GLFW_ANAFINEN_CLIENT_DECORATIONS` is defined in `glfw3.h` when the patch is in.
- `GLFW_WINDOW_EDGE_*` use the `xdg_toplevel.resize_edge` values.
- On Wayland the request is sent to the `xdg_toplevel` of the window. When libdecor is in use, that
  is the toplevel libdecor owns (`libdecor_frame_get_xdg_toplevel`). The request carries the serial
  of the last pointer button press on the window, kept in `_glfw.wl.pointerButtonSerial` (key events
  overwrite GLFW's general `serial`).
- Call these functions while the button is held, ideally from the mouse button callback of that
  press.

Every changed block is marked `ANAFINEN PATCH` (zlib license: altered source versions must be marked).

To update GLFW: check out the new tag in `external/glfw`, regenerate the patch against it
(`git diff` of a patched checkout) and configure again. A patch that no longer applies stops the
configure step with the `git apply` error.
