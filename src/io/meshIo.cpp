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
#include "core/pathUtf8.hpp"
#include "detail/textIo.hpp"
#include "formats/formats.hpp"

#include <array>
#include <fstream>
#include <string>

namespace anaf::IO {

  namespace {

    const std::array<FormatDescriptor, 7>& formatTable() {
      static const std::array<FormatDescriptor, 7> table{{
        {E_FileFormat::Msh, "Gmsh MSH", {".msh"}, true, true},
        {E_FileFormat::Vtu, "VTK XML Unstructured Grid", {".vtu"}, true, true},
        {E_FileFormat::Pvd, "ParaView Collection", {".pvd"}, true, true},
        {E_FileFormat::VtkLegacy, "VTK Legacy", {".vtk"}, true, true},
        {E_FileFormat::Step, "STEP", {".step", ".stp"}, true, true},
        {E_FileFormat::Iges, "IGES", {".iges", ".igs"}, true, false},
        {E_FileFormat::Brep, "OpenCASCADE BREP", {".brep", ".brp"}, true, false},
      }};
      return table;
    }

    E_FileFormat sniff(const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) return E_FileFormat::Auto;
      std::string head(512, '\0');
      file.read(head.data(), static_cast<std::streamsize>(head.size()));
      head.resize(static_cast<std::size_t>(file.gcount()));
      if (head.find("$MeshFormat") != std::string::npos) return E_FileFormat::Msh;
      if (head.find("# vtk DataFile") != std::string::npos) return E_FileFormat::VtkLegacy;
      if (head.find("type=\"Collection\"") != std::string::npos) return E_FileFormat::Pvd;
      if (head.find("<VTKFile") != std::string::npos) return E_FileFormat::Vtu;
      if (head.find("ISO-10303-21") != std::string::npos) return E_FileFormat::Step;
      return E_FileFormat::Auto;
    }

    IoError toError(const std::exception& error, const IoError::E_Code code) {
      return IoError{code, error.what()};
    }

    template <typename Fn>
    auto guarded(Fn&& fn, const IoError::E_Code failureCode) -> std::expected<decltype(fn()), IoError> {
      try {
        return fn();
      } catch (const detail::CancelledFailure& error) {
        return std::unexpected(toError(error, IoError::E_Code::Cancelled));
      } catch (const detail::ParseFailure& error) {
        return std::unexpected(toError(error, IoError::E_Code::ParseError));
      } catch (const std::invalid_argument& error) {
        return std::unexpected(toError(error, IoError::E_Code::InvalidModel));
      } catch (const std::exception& error) {
        return std::unexpected(toError(error, failureCode));
      }
    }

  } // namespace end

  std::string_view formatName(const E_FileFormat format) noexcept {
    for (const auto& row : formatTable()) {
      if (row.format == format) return row.name;
    }
    return "auto";
  }

  std::span<const FormatDescriptor> supportedFormats() {
    return formatTable();
  }

  E_FileFormat detectFormat(const std::filesystem::path& path) {
    const std::string extension = detail::toLower(pathToUtf8(path.extension()));
    for (const auto& row : formatTable()) {
      for (const auto ext : row.extensions) {
        if (extension == ext) return row.format;
      }
    }
    return sniff(path);
  }

  std::expected<MeshModel, IoError> readMesh(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context) {
    if (!std::filesystem::exists(path)) {
      return std::unexpected(IoError{IoError::E_Code::FileNotFound, "file not found: " + pathToUtf8(path)});
    }
    E_FileFormat format = options.format == E_FileFormat::Auto ? detectFormat(path) : options.format;
    if (format == E_FileFormat::Auto) {
      return std::unexpected(IoError{IoError::E_Code::UnsupportedFormat, "cannot determine the format of " + pathToUtf8(path)});
    }
    return guarded([&]() -> MeshModel {
      MeshModel model;
      switch (format) {
        case E_FileFormat::Msh: model = formats::readMsh(path, options, context); break;
        case E_FileFormat::VtkLegacy: model = formats::readVtkLegacy(path, options, context); break;
        case E_FileFormat::Vtu: model = formats::readVtu(path, options, context); break;
        case E_FileFormat::Pvd: model = formats::readPvd(path, options, context); break;
        case E_FileFormat::Step:
        case E_FileFormat::Iges:
        case E_FileFormat::Brep: model = formats::readCad(path, options, context); break;
        case E_FileFormat::Auto: break;
      }
      if (const auto problems = model.validate(); !problems.empty()) {
        throw detail::ParseFailure("file produced an inconsistent model: " + problems.front());
      }
      return model;
    }, IoError::E_Code::BackendError);
  }

  std::expected<WriteReport, IoError> writeMesh(const std::filesystem::path& path, const MeshModel& model,
                                                const WriteOptions& options, const IoContext& context) {
    E_FileFormat format = options.format;
    if (format == E_FileFormat::Auto) {
      const std::string extension = detail::toLower(pathToUtf8(path.extension()));
      for (const auto& row : formatTable()) {
        for (const auto ext : row.extensions) {
          if (extension == ext) format = row.format;
        }
      }
    }
    if (format == E_FileFormat::Auto) {
      return std::unexpected(IoError{IoError::E_Code::UnsupportedFormat, "cannot determine the output format from " + pathToUtf8(path)});
    }
    return guarded([&]() -> WriteReport {
      switch (format) {
        case E_FileFormat::Msh: return formats::writeMsh(path, model, options, context);
        case E_FileFormat::VtkLegacy: return formats::writeVtkLegacy(path, model, options, context);
        case E_FileFormat::Vtu: return formats::writeVtu(path, model, options, context);
        case E_FileFormat::Pvd: return formats::writePvd(path, model, options, context);
        case E_FileFormat::Step: return formats::writeStep(path, model, options, context);
        case E_FileFormat::Iges:
        case E_FileFormat::Brep:
        case E_FileFormat::Auto: break;
      }
      throw std::runtime_error(std::string(formatName(format)) + " cannot be written");
    }, IoError::E_Code::WriteError);
  }

} // namespace anaf::IO end
