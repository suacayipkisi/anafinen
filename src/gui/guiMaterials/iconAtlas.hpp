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

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <optional>

// UI icons (UI_POLISH_PLAN S3): the hand-drawn SVGs in assets/icons/ui/{dark,light}/, rasterized
// by nanosvg once per pixel size into one RGBA texture per size. The set follows ThemePalette::light;
// a dark / light switch or a framebuffer scale change drops the textures, which are rebuilt on demand.
// GUI thread only. Every widget here falls back to its text label when an icon is missing.

// X(enumerator, file name without ".svg"). The file name is the stable key, the enumerator follows it.
#define ANAFINEN_ICON_LIST(X)                                 \
  X(ActionAdd, "action-add")                                  \
  X(ActionClear, "action-clear")                              \
  X(ActionCopy, "action-copy")                                \
  X(ActionExport, "action-export")                            \
  X(ActionHelp, "action-help")                                \
  X(ActionImport, "action-import")                            \
  X(ActionInfo, "action-info")                                \
  X(ActionNew, "action-new")                                  \
  X(ActionRemove, "action-remove")                            \
  X(ActionSettings, "action-settings")                        \
  X(ActionSolve, "action-solve")                              \
  X(ActionStop, "action-stop")                                \
  X(AnalysisDynamic, "analysis-dynamic")                      \
  X(AnalysisModal, "analysis-modal")                          \
  X(AnalysisStatic, "analysis-static")                        \
  X(DisplayColorDisplacement, "display-color-displacement")   \
  X(DisplayColorStress, "display-color-stress")               \
  X(DisplayHidden, "display-hidden")                          \
  X(DisplayLoads, "display-loads")                            \
  X(DisplayNodesSphere, "display-nodes-sphere")               \
  X(DisplayNodesSquare, "display-nodes-square")               \
  X(DisplaySupportsDof, "display-supports-dof")               \
  X(DisplaySupportsSymbols, "display-supports-symbols")       \
  X(DisplayVisible, "display-visible")                        \
  X(LoadDistributed, "load-distributed")                      \
  X(LoadForce, "load-force")                                  \
  X(LoadGravity, "load-gravity")                              \
  X(LoadMoment, "load-moment")                                \
  X(ModelElementBar, "model-element-bar")                     \
  X(ModelElementBeam, "model-element-beam")                   \
  X(ModelFrame, "model-frame")                                \
  X(ModelLibrary, "model-library")                            \
  X(ModelMaterial, "model-material")                          \
  X(ModelMesh, "model-mesh")                                  \
  X(ModelNode, "model-node")                                  \
  X(ModelSection, "model-section")                            \
  X(ModelTruss, "model-truss")                                \
  X(PanelConsole, "panel-console")                            \
  X(PanelProperties, "panel-properties")                      \
  X(PanelTree, "panel-tree")                                  \
  X(PanelViewport, "panel-viewport")                          \
  X(ReleaseHinge, "release-hinge")                            \
  X(ReleaseTorsion, "release-torsion")                        \
  X(ResultDiagram, "result-diagram")                          \
  X(ResultDisplacement, "result-displacement")                \
  X(ResultStress, "result-stress")                            \
  X(SectionBox, "section-box")                                \
  X(SectionCircle, "section-circle")                          \
  X(SectionGeneral, "section-general")                        \
  X(SectionIShape, "section-i-shape")                         \
  X(SectionPipe, "section-pipe")                              \
  X(SectionRectangle, "section-rectangle")                    \
  X(StatusError, "status-error")                              \
  X(StatusInfo, "status-info")                                \
  X(StatusSuccess, "status-success")                          \
  X(StatusWarning, "status-warning")                          \
  X(SupportFixed, "support-fixed")                            \
  X(SupportInclined, "support-inclined")                      \
  X(SupportPin, "support-pin")                                \
  X(SupportRoller, "support-roller")                          \
  X(SupportSlider, "support-slider")                          \
  X(ViewAxes, "view-axes")                                    \
  X(ViewFit, "view-fit")                                      \
  X(ViewFront, "view-front")                                  \
  X(ViewGrid, "view-grid")                                    \
  X(ViewIso, "view-iso")                                      \
  X(ViewRight, "view-right")                                  \
  X(ViewTop, "view-top")

namespace anaf::GUI::ICONS {

#define ANAFINEN_ICON_ENUMERATOR(name, file) name,
  enum class E_IconId : std::uint8_t {
    ANAFINEN_ICON_LIST(ANAFINEN_ICON_ENUMERATOR)
    Count
  };
#undef ANAFINEN_ICON_ENUMERATOR

  inline constexpr std::size_t iconCount = static_cast<std::size_t>(E_IconId::Count);

  // Texture and UVs of one icon at one size.
  struct IconImage {
    ImTextureRef texture;
    ImVec2 uv0;
    ImVec2 uv1;
  };

  // Once per frame right after ImGui::NewFrame(): drops the textures when the framebuffer scale or the
  // dark / light set changed (nothing of the previous frame refers to them any more).
  void beginFrame();
  // Deletes the textures; call while the GL context is still current (end of initgui()'s GL scope).
  void release();

  // Icon at a size in logical pixels (rasterized at size x framebuffer scale), empty when the icon is missing.
  std::optional<IconImage> icon(E_IconId id, float size);

  // Sizes in logical pixels: toolbar buttons and tree rows / list entries (one text line).
  float toolbarSize();
  float rowSize();

  // Draws the icon into a draw list; false (nothing drawn) when it is missing.
  bool drawIcon(ImDrawList* drawList, E_IconId id, ImVec2 min, float size);

  // Flat toolbar button: transparent until hovered, a selected one has an accent tint and an accent line
  // along its bottom edge (no icon tint). Text button with fallbackLabel when the icon is missing.
  // dropdown draws a small corner triangle (the button opens a list of choices).
  bool iconButton(const char* strId, E_IconId id, bool selected, const char* fallbackLabel, bool dropdown = false);
  // Icon, then the text, on one line.
  void iconText(E_IconId id, const char* text);
  // Tree node with an icon before its label; the whole row toggles it.
  bool iconTreeNode(const char* strId, E_IconId id, const char* label, ImGuiTreeNodeFlags flags = 0);
  // Selectable (popup list entry) with an icon before its label.
  bool iconSelectable(E_IconId id, const char* label, bool selected);

} // namespace anaf::GUI::ICONS end
