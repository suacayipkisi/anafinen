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

#include "fileIoPanel.hpp"

#include <bridge/generalStatus.hpp>
#include <io/meshIo.hpp>
#include <log/anaf_info.hpp>

#include <imgui.h>

#include <array>
#include <format>
#include <limits>

namespace anaf::GUI {

  namespace {

    using anaf::IO::FileFormat;

    struct ExportChoice {
      const char* label;
      const char* description;
      FileFormat format;
      const char* extension;
      anaf::IO::MshVersion mshVersion;
      anaf::IO::VtkLegacyVersion vtkVersion;
      bool allowsBinary;
      bool allowsCompression;
    };

    constexpr std::array<ExportChoice, 6> kExportChoices{{
      {"Gmsh MSH 4.1", "Current Gmsh format: mesh, physical groups, all result steps.", FileFormat::Msh, ".msh",
       anaf::IO::MshVersion::V4_1, anaf::IO::VtkLegacyVersion::V5_1, true, false},
      {"Gmsh MSH 2.2", "Legacy Gmsh format, read by most other solvers.", FileFormat::Msh, ".msh",
       anaf::IO::MshVersion::V2_2, anaf::IO::VtkLegacyVersion::V5_1, true, false},
      {"VTK XML (.vtu)", "ParaView's modern format (one result step).", FileFormat::Vtu, ".vtu",
       anaf::IO::MshVersion::V4_1, anaf::IO::VtkLegacyVersion::V5_1, true, true},
      {"VTK Legacy 5.1 (.vtk)", "Legacy VTK for VTK >= 9 / ParaView >= 5.10 (one result step).", FileFormat::VtkLegacy, ".vtk",
       anaf::IO::MshVersion::V4_1, anaf::IO::VtkLegacyVersion::V5_1, true, false},
      {"VTK Legacy 4.2 (.vtk)", "Legacy VTK readable by every VTK / ParaView version.", FileFormat::VtkLegacy, ".vtk",
       anaf::IO::MshVersion::V4_1, anaf::IO::VtkLegacyVersion::V4_2, true, false},
      {"STEP (.step)", "CAD geometry (bars as edges); other data in a .anafFields sidecar.", FileFormat::Step, ".step",
       anaf::IO::MshVersion::V4_1, anaf::IO::VtkLegacyVersion::V5_1, false, false},
    }};

    std::vector<FileFilter> importFilters() {
      std::vector<FileFilter> filters;
      FileFilter all{"All supported", {}};
      for (const auto& format : anaf::IO::supportedFormats()) {
        if (!format.canRead) continue;
        FileFilter filter{std::string(format.name), {}};
        for (const auto extension : format.extensions) {
          filter.patterns.push_back("*" + std::string(extension));
          all.patterns.push_back("*" + std::string(extension));
        }
        filters.push_back(std::move(filter));
      }
      filters.insert(filters.begin(), std::move(all));
      return filters;
    }

    bool isCad(const std::filesystem::path& path) {
      const auto format = anaf::IO::detectFormat(path);
      return format == FileFormat::Step || format == FileFormat::Iges || format == FileFormat::Brep;
    }

  } // namespace end

  FileIoPanel::FileIoPanel() : m_service(std::make_unique<anaf::IO::IoService>()) {
    isOpen = true; // renders popups / progress only when needed
  }

  // The service destructor cancels queued jobs and joins the I/O thread.
  FileIoPanel::~FileIoPanel() = default;

  void FileIoPanel::notify(std::string message, const bool error) {
    m_notice = std::move(message);
    m_noticeIsError = error;
    m_noticeUntil = std::chrono::steady_clock::now() + std::chrono::seconds(error ? 8 : 4);
  }

  void FileIoPanel::requestImport() {
    if (busy()) {
      notify("A file operation is already in progress", true);
      return;
    }
    if (!NativeFileDialog::available()) {
      notify("No native file dialog available (install zenity or kdialog)", true);
      anaf::LOG::error("No native file dialog backend found; install zenity or kdialog");
      return;
    }
    m_dialog = NativeFileDialog::openFile("Import mesh or CAD model", importFilters());
    m_stage = Stage::ChoosingImport;
  }

  void FileIoPanel::requestExport() {
    if (busy()) {
      notify("A file operation is already in progress", true);
      return;
    }
    auto& bridge = BRIDGE::buildBridge();
    {
      std::lock_guard lock(bridge.dataMutex);
      if (!bridge.activeMesh) {
        notify("Nothing to export: create or import a model first", true);
        return;
      }
    }
    m_stage = Stage::ExportOptions;
  }

  void FileIoPanel::startImport(const std::filesystem::path& path) {
    auto& bridge = BRIDGE::buildBridge();
    if (bridge.m_isRunning || bridge.m_isGeneratingPreview) {
      notify("Wait for the running calculation to finish before importing", true);
      m_stage = Stage::Idle;
      return;
    }
    std::vector<anaf::MATERIAL::Material> materials;
    {
      std::lock_guard lock(bridge.dataMutex);
      materials = bridge.allMaterials;
    }
    const anaf::IO::ReadOptions options = isCad(path) ? m_cadOptions : anaf::IO::ReadOptions{};
    m_importTask = m_service->runAsync<FEM::TRUSS::ADAPTER::ImportedTruss>(
      "Import " + path.filename().string(),
      [path, options, materials = std::move(materials)](const anaf::IO::IoContext& context)
        -> std::expected<FEM::TRUSS::ADAPTER::ImportedTruss, anaf::IO::IoError> {
        auto model = anaf::IO::readMesh(path, options, context);
        if (!model) return std::unexpected(model.error());
        context.progress(0.97f, "preparing view");
        auto imported = FEM::TRUSS::ADAPTER::toMeshData(*model, materials);
        for (const auto& warning : model->warnings) imported.notes.push_back("warning: " + warning);
        return imported;
      });
    m_stage = Stage::Importing;
  }

  void FileIoPanel::startExport(std::filesystem::path path) {
    const auto& choice = kExportChoices[static_cast<std::size_t>(m_exportFormat)];
    if (path.extension().empty()) path += choice.extension;

    auto& bridge = BRIDGE::buildBridge();
    std::shared_ptr<const BRIDGE::MeshData> mesh;
    BRIDGE::FixedDOFMap fixity;
    {
      std::lock_guard lock(bridge.dataMutex);
      mesh = bridge.activeMesh;
      fixity = bridge.fixedDOFsByNode;
    }
    if (!mesh) {
      notify("Nothing to export", true);
      m_stage = Stage::Idle;
      return;
    }
    anaf::IO::WriteOptions options;
    options.format = choice.format;
    options.mshVersion = choice.mshVersion;
    options.vtkVersion = choice.vtkVersion;
    options.encoding = (choice.allowsBinary && m_exportBinary) ? anaf::IO::Encoding::Binary : anaf::IO::Encoding::Ascii;
    options.compress = choice.allowsCompression && m_exportBinary && m_exportCompress;

    // The snapshot is immutable and the conversion runs on the I/O thread as well.
    m_exportTask = m_service->runAsync<anaf::IO::WriteReport>(
      "Export " + path.filename().string(),
      [mesh = std::move(mesh), fixity = std::move(fixity), path, options](const anaf::IO::IoContext& context) {
        const auto model = FEM::TRUSS::ADAPTER::toMeshModel(*mesh, fixity);
        context.progress(0.1f, "writing");
        return anaf::IO::writeMesh(path, model, options, context);
      });
    m_stage = Stage::Exporting;
  }

  void FileIoPanel::pollDialog() {
    if (!m_dialog || !m_dialog->ready()) return;
    const auto path = m_dialog->result();
    m_dialog.reset();
    if (!path) {
      m_stage = Stage::Idle;
      return;
    }
    if (m_stage == Stage::ChoosingImport) {
      if (isCad(*path)) {
        m_pendingImport = *path;
        m_stage = Stage::CadOptions;
      } else {
        startImport(*path);
      }
    } else if (m_stage == Stage::ChoosingExport) {
      startExport(*path);
    }
  }

  void FileIoPanel::pollTasks() {
    auto& bridge = BRIDGE::buildBridge();
    if (m_importTask && m_importTask->ready()) {
      const auto& result = m_importTask->wait();
      if (result) {
        {
          std::lock_guard lock(bridge.dataMutex);
          bridge.activeMesh = result->mesh;
          bridge.fixedDOFsByNode = result->fixity;
          bridge.hasTrussPreview = true;
          bridge.selectedNodeId = std::numeric_limits<std::uint32_t>::max();
          bridge.m_objectType = BRIDGE::ObjectType::truss_imported_or_entered;
        }
        bridge.dataVersion.fetch_add(1, std::memory_order_release);
        anaf::LOG::success("{} finished", m_importTask->description());
        for (const auto& note : result->notes) {
          if (note.starts_with("warning: ")) anaf::LOG::warn("{}", note.substr(9));
          else anaf::LOG::info("{}", note);
        }
        notify(m_importTask->description() + " finished", false);
        if (onImported) onImported();
      } else if (result.error().code == anaf::IO::IoError::Code::Cancelled) {
        anaf::LOG::warn("{} cancelled", m_importTask->description());
        notify("Import cancelled", false);
      } else {
        anaf::LOG::error("{} failed: {}", m_importTask->description(), result.error().message);
        notify("Import failed: " + result.error().message, true);
      }
      m_importTask.reset();
      m_stage = Stage::Idle;
    }
    if (m_exportTask && m_exportTask->ready()) {
      const auto& result = m_exportTask->wait();
      if (result) {
        anaf::LOG::success("Exported '{}'", result->path);
        for (const auto& extra : result->extraFiles) anaf::LOG::info("Also written: '{}'", extra);
        for (const auto& warning : result->warnings) anaf::LOG::warn("{}", warning);
        notify("Exported " + std::filesystem::path(result->path).filename().string(), false);
      } else if (result.error().code == anaf::IO::IoError::Code::Cancelled) {
        anaf::LOG::warn("{} cancelled", m_exportTask->description());
        notify("Export cancelled", false);
      } else {
        anaf::LOG::error("{} failed: {}", m_exportTask->description(), result.error().message);
        notify("Export failed: " + result.error().message, true);
      }
      m_exportTask.reset();
      m_stage = Stage::Idle;
    }
  }

  void FileIoPanel::renderCadOptions() {
    constexpr const char* popup = "CAD Import Options";
    if (m_stage == Stage::CadOptions && !ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted(m_pendingImport.filename().string().c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Mesh the CAD geometry as:");
    ImGui::RadioButton("Bars: one element per CAD edge (truss / frame)", &m_cadOptions.cadMeshDimension, 1);
    ImGui::RadioButton("Surfaces: triangles", &m_cadOptions.cadMeshDimension, 2);
    ImGui::RadioButton("Volumes: tetrahedra", &m_cadOptions.cadMeshDimension, 3);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputDouble("Element size [m]", &m_cadOptions.cadMeshSize, 0.0, 0.0, "%.6g");
    if (m_cadOptions.cadMeshSize < 0.0) m_cadOptions.cadMeshSize = 0.0;
    ImGui::TextDisabled(m_cadOptions.cadMeshDimension == 1 ? "0 = one bar per CAD edge" : "0 = automatic size");
    ImGui::RadioButton("Linear", &m_cadOptions.cadElementOrder, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Quadratic", &m_cadOptions.cadElementOrder, 2);
    ImGui::Checkbox("Keep boundary elements", &m_cadOptions.cadKeepLowerDimensions);
    if (m_cadOptions.cadMeshDimension > 1) {
      ImGui::TextDisabled("The truss solver uses bars; surfaces and volumes are shown as wireframe.");
    }
    ImGui::Separator();
    if (ImGui::Button("Import", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
      startImport(m_pendingImport);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
      m_stage = Stage::Idle;
    }
    ImGui::EndPopup();
  }

  void FileIoPanel::renderExportOptions() {
    constexpr const char* popup = "Export Model";
    if (m_stage == Stage::ExportOptions && !ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    for (int i = 0; i < static_cast<int>(kExportChoices.size()); ++i) {
      ImGui::RadioButton(kExportChoices[static_cast<std::size_t>(i)].label, &m_exportFormat, i);
    }
    const auto& choice = kExportChoices[static_cast<std::size_t>(m_exportFormat)];
    ImGui::TextDisabled("%s", choice.description);
    ImGui::Separator();
    ImGui::BeginDisabled(!choice.allowsBinary);
    ImGui::Checkbox("Binary (smaller, faster, exact)", &m_exportBinary);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!choice.allowsCompression || !m_exportBinary);
    ImGui::Checkbox("zlib compression", &m_exportCompress);
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::Button("Choose file...", ImVec2(140, 0))) {
      ImGui::CloseCurrentPopup();
      const std::vector<FileFilter> filters{{choice.label, {std::string("*") + choice.extension}}};
      if (!NativeFileDialog::available()) {
        notify("No native file dialog available (install zenity or kdialog)", true);
        m_stage = Stage::Idle;
      } else {
        m_dialog = NativeFileDialog::saveFile("Export model", std::filesystem::path("anafinen_model") += choice.extension, filters);
        m_stage = Stage::ChoosingExport;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
      m_stage = Stage::Idle;
    }
    ImGui::EndPopup();
  }

  void FileIoPanel::renderProgress() {
    const bool showNotice = std::chrono::steady_clock::now() < m_noticeUntil;
    const bool working = m_stage == Stage::Importing || m_stage == Stage::Exporting;
    const bool choosing = m_stage == Stage::ChoosingImport || m_stage == Stage::ChoosingExport;
    if (!working && !choosing && !showNotice) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 16.0f, viewport->WorkPos.y + viewport->WorkSize.y - 16.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
      | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##FileIoStatus", nullptr, flags)) {
      if (choosing) ImGui::TextUnformatted("Waiting for the file dialog...");
      auto progressRow = [](const auto& task) {
        ImGui::TextUnformatted(task->description().c_str());
        const std::string stage = task->stage();
        ImGui::ProgressBar(task->progress(), ImVec2(260.0f, 0.0f), stage.c_str());
        if (ImGui::Button("Cancel")) task->cancel();
      };
      if (m_importTask) progressRow(m_importTask);
      if (m_exportTask) progressRow(m_exportTask);
      if (showNotice) {
        const ImVec4 color = m_noticeIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.55f, 0.95f, 0.6f, 1.0f);
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextColored(color, "%s", m_notice.c_str());
        ImGui::PopTextWrapPos();
      }
    }
    ImGui::End();
  }

  void FileIoPanel::onImGuiRender() {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) requestImport();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E)) requestExport();
    pollDialog();
    pollTasks();
    renderCadOptions();
    renderExportOptions();
    renderProgress();
  }

} // namespace anaf::GUI end
