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

#include <guiMaterials/iPanel.hpp>
#include <fileDialogs/nativeFileDialog.hpp>

#include <io/service/ioService.hpp>
#include <beam/beamIO/beamMeshAdapter.hpp>
#include <truss_1D/trussIO/trussMeshAdapter.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace anaf::GUI {

  // Result of an import: a file with beam elements (ElementFormulation) becomes a beam model,
  // any other file a truss model.
  struct ImportedModel {
    std::optional<FEM::TRUSS::ADAPTER::ImportedTruss> truss;
    std::optional<FEM::BEAM::ADAPTER::ImportedBeam> beam;
  };

  // Import / export front end: native file chooser, CAD and export options, progress and
  // cancellation. All file work runs on the IoService thread; this panel only polls.
  class FileIoPanel : public IPanel {
  public:
    FileIoPanel();
    ~FileIoPanel() override;

    void requestImport();
    // Imports path without the file chooser (built-in library models).
    void importFile(const std::filesystem::path& path);
    void requestExport();
    // Cancels a running import (the object type is about to change). An import that finishes
    // anyway is discarded when the model was reset after it started (modelGeneration).
    void cancelImport();

    std::function<void()> onImported;     // a truss model was imported, e.g. show the model tree
    std::function<void()> onImportedBeam; // a beam model was imported

    void onImGuiRender() override;

  private:
    enum class Stage { Idle, ChoosingImport, CadOptions, Importing, ExportOptions, ChoosingExport, Exporting };

    bool busy() const { return m_stage != Stage::Idle; }
    void startImport(const std::filesystem::path& path);
    void startExport(std::filesystem::path path);
    void pollDialog();
    void pollTasks();
    void renderCadOptions();
    void renderExportOptions();
    void renderProgress();
    void notify(std::string message, bool error);
    void logNotes(const std::vector<std::string>& notes);
    // Publishes an imported beam model: switches to beam_frame and appends its new sections.
    void finishBeamImport(const FEM::BEAM::ADAPTER::ImportedBeam& imported);

    std::unique_ptr<anaf::IO::IoService> m_service;
    std::unique_ptr<NativeFileDialog> m_dialog;
    std::shared_ptr<anaf::IO::IoTask<ImportedModel>> m_importTask;
    std::shared_ptr<anaf::IO::IoTask<anaf::IO::WriteReport>> m_exportTask;
    Stage m_stage{Stage::Idle};
    std::filesystem::path m_pendingImport;
    std::uint64_t m_importGeneration{0}; // bridge.modelGeneration when the running import started
    std::vector<std::uint32_t> m_importSectionIDs; // section list the running import resolved against

    anaf::IO::ReadOptions m_cadOptions;
    int m_exportFormat{0};
    bool m_exportBinary{true};
    bool m_exportCompress{true};

    std::string m_notice;
    bool m_noticeIsError{false};
    std::chrono::steady_clock::time_point m_noticeUntil{};
  };

} // namespace anaf::GUI end
