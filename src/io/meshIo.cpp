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

#include "meshIo.hpp"
#include "detail/textIo.hpp"
#include "formats/formats.hpp"

#include <array>
#include <fstream>
#include <string>

namespace anaf::IO {

  namespace {

    const std::array<FormatDescriptor, 7>& formatTable() {
      static const std::array<FormatDescriptor, 7> table{{
        {FileFormat::Msh, "Gmsh MSH", {".msh"}, true, true},
        {FileFormat::Vtu, "VTK XML Unstructured Grid", {".vtu"}, true, true},
        {FileFormat::Pvd, "ParaView Collection", {".pvd"}, true, true},
        {FileFormat::VtkLegacy, "VTK Legacy", {".vtk"}, true, true},
        {FileFormat::Step, "STEP", {".step", ".stp"}, true, true},
        {FileFormat::Iges, "IGES", {".iges", ".igs"}, true, false},
        {FileFormat::Brep, "OpenCASCADE BREP", {".brep", ".brp"}, true, false},
      }};
      return table;
    }

    FileFormat sniff(const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) return FileFormat::Auto;
      std::string head(512, '\0');
      file.read(head.data(), static_cast<std::streamsize>(head.size()));
      head.resize(static_cast<std::size_t>(file.gcount()));
      if (head.find("$MeshFormat") != std::string::npos) return FileFormat::Msh;
      if (head.find("# vtk DataFile") != std::string::npos) return FileFormat::VtkLegacy;
      if (head.find("type=\"Collection\"") != std::string::npos) return FileFormat::Pvd;
      if (head.find("<VTKFile") != std::string::npos) return FileFormat::Vtu;
      if (head.find("ISO-10303-21") != std::string::npos) return FileFormat::Step;
      return FileFormat::Auto;
    }

    IoError toError(const std::exception& error, const IoError::Code code) {
      return IoError{code, error.what()};
    }

    template <typename Fn>
    auto guarded(Fn&& fn, const IoError::Code failureCode) -> std::expected<decltype(fn()), IoError> {
      try {
        return fn();
      } catch (const detail::CancelledFailure& error) {
        return std::unexpected(toError(error, IoError::Code::Cancelled));
      } catch (const detail::ParseFailure& error) {
        return std::unexpected(toError(error, IoError::Code::ParseError));
      } catch (const std::invalid_argument& error) {
        return std::unexpected(toError(error, IoError::Code::InvalidModel));
      } catch (const std::exception& error) {
        return std::unexpected(toError(error, failureCode));
      }
    }

  } // namespace end

  std::string_view formatName(const FileFormat format) noexcept {
    for (const auto& row : formatTable()) {
      if (row.format == format) return row.name;
    }
    return "auto";
  }

  std::span<const FormatDescriptor> supportedFormats() {
    return formatTable();
  }

  FileFormat detectFormat(const std::filesystem::path& path) {
    const std::string extension = detail::toLower(path.extension().string());
    for (const auto& row : formatTable()) {
      for (const auto ext : row.extensions) {
        if (extension == ext) return row.format;
      }
    }
    return sniff(path);
  }

  std::expected<MeshModel, IoError> readMesh(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context) {
    if (!std::filesystem::exists(path)) {
      return std::unexpected(IoError{IoError::Code::FileNotFound, "file not found: " + path.string()});
    }
    FileFormat format = options.format == FileFormat::Auto ? detectFormat(path) : options.format;
    if (format == FileFormat::Auto) {
      return std::unexpected(IoError{IoError::Code::UnsupportedFormat, "cannot determine the format of " + path.string()});
    }
    return guarded([&]() -> MeshModel {
      MeshModel model;
      switch (format) {
        case FileFormat::Msh: model = formats::readMsh(path, options, context); break;
        case FileFormat::VtkLegacy: model = formats::readVtkLegacy(path, options, context); break;
        case FileFormat::Vtu: model = formats::readVtu(path, options, context); break;
        case FileFormat::Pvd: model = formats::readPvd(path, options, context); break;
        case FileFormat::Step:
        case FileFormat::Iges:
        case FileFormat::Brep: model = formats::readCad(path, options, context); break;
        case FileFormat::Auto: break;
      }
      if (const auto problems = model.validate(); !problems.empty()) {
        throw detail::ParseFailure("file produced an inconsistent model: " + problems.front());
      }
      return model;
    }, IoError::Code::BackendError);
  }

  std::expected<WriteReport, IoError> writeMesh(const std::filesystem::path& path, const MeshModel& model,
                                                const WriteOptions& options, const IoContext& context) {
    FileFormat format = options.format;
    if (format == FileFormat::Auto) {
      const std::string extension = detail::toLower(path.extension().string());
      for (const auto& row : formatTable()) {
        for (const auto ext : row.extensions) {
          if (extension == ext) format = row.format;
        }
      }
    }
    if (format == FileFormat::Auto) {
      return std::unexpected(IoError{IoError::Code::UnsupportedFormat, "cannot determine the output format from " + path.string()});
    }
    return guarded([&]() -> WriteReport {
      switch (format) {
        case FileFormat::Msh: return formats::writeMsh(path, model, options, context);
        case FileFormat::VtkLegacy: return formats::writeVtkLegacy(path, model, options, context);
        case FileFormat::Vtu: return formats::writeVtu(path, model, options, context);
        case FileFormat::Pvd: return formats::writePvd(path, model, options, context);
        case FileFormat::Step: return formats::writeStep(path, model, options, context);
        case FileFormat::Iges:
        case FileFormat::Brep:
        case FileFormat::Auto: break;
      }
      throw std::runtime_error(std::string(formatName(format)) + " cannot be written");
    }, IoError::Code::WriteError);
  }

} // namespace anaf::IO end
