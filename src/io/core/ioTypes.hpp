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

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace anaf::IO {

  enum class FileFormat {
    Auto,       // detect from extension, then from file content
    Msh,        // Gmsh MSH 1.0 / 2.x / 4.x
    VtkLegacy,  // legacy VTK 2.0 ... 5.1
    Vtu,        // VTK XML UnstructuredGrid
    Step,       // ISO 10303-21 (AP203 / AP214 / AP242)
    Iges,       // IGES (read only)
    Brep        // OpenCASCADE BREP (read only)
  };

  enum class Encoding { Ascii, Binary };

  enum class MshVersion { V2_2, V4_1 };

  enum class VtkLegacyVersion {
    V4_2, // classic CELLS layout, readable by every VTK / ParaView version
    V5_1  // OFFSETS / CONNECTIVITY layout (VTK >= 9)
  };

  struct ReadOptions {
    FileFormat format{FileFormat::Auto};
    // CAD formats (STEP / IGES / BREP) are meshed on import.
    int cadMeshDimension{1};       // 1 = curves only (trusses/frames), 2 = surfaces, 3 = volumes
    double cadMeshSize{0.0};       // target element size in model units; <= 0 = one element per curve
    int cadElementOrder{1};        // 1 = linear, 2 = quadratic
    bool cadKeepLowerDimensions{false}; // also keep boundary elements (e.g. triangles of a tet mesh)
    bool readSidecar{true};        // merge `<file>.anafFields` if present (CAD formats)
  };

  struct WriteOptions {
    FileFormat format{FileFormat::Auto};
    Encoding encoding{Encoding::Ascii};
    MshVersion mshVersion{MshVersion::V4_1};
    VtkLegacyVersion vtkVersion{VtkLegacyVersion::V5_1};
    bool compress{false};          // VTU: zlib-compress binary arrays
    int timeStep{-1};              // single-step formats (VTK, VTU): step to write, -1 = last
    bool writeSidecar{true};       // CAD formats: write `<file>.anafFields` with non-geometric data
    bool writeTags{true};          // VTK / VTU: store original node / element tags as arrays
  };

  struct IoError {
    enum class Code { Cancelled, FileNotFound, UnsupportedFormat, ParseError, WriteError, InvalidModel, BackendError };
    Code code{Code::ParseError};
    std::string message;
  };

  struct WriteReport {
    std::string path;
    std::vector<std::string> warnings;
    std::vector<std::string> extraFiles; // sidecar files written next to `path`
  };

  // Cooperative cancellation + progress reporting for a single read/write.
  struct IoContext {
    std::function<bool()> isCancelled;                          // may be empty
    std::function<void(float, std::string_view)> onProgress;    // fraction 0..1, stage label; may be empty

    bool cancelled() const { return isCancelled && isCancelled(); }
    void progress(const float fraction, const std::string_view stage) const {
      if (onProgress) onProgress(fraction, stage);
    }
  };

  struct FormatDescriptor {
    FileFormat format;
    std::string_view name;
    std::vector<std::string_view> extensions; // lower case, with leading dot
    bool canRead;
    bool canWrite;
  };

  std::string_view formatName(FileFormat format) noexcept;

} // namespace anaf::IO end
