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

// Internal format implementations. They throw (detail::ParseFailure, detail::CancelledFailure,
// std::exception); the public API in meshIo.hpp converts that into std::expected results.

#include "../core/ioTypes.hpp"
#include "../model/meshModel.hpp"

#include <filesystem>

namespace anaf::IO::formats {

  MeshModel readMsh(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context);
  WriteReport writeMsh(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context);

  MeshModel readVtkLegacy(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context);
  WriteReport writeVtkLegacy(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context);

  MeshModel readVtu(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context);
  WriteReport writeVtu(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context);

  // ParaView collection: a .pvd file plus one .vtu per time step (implemented in vtuFormat.cpp).
  MeshModel readPvd(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context);
  WriteReport writePvd(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context);

  // STEP / IGES / BREP through OpenCASCADE (Gmsh OCC kernel).
  MeshModel readCad(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context);
  WriteReport writeStep(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context);

} // namespace anaf::IO::formats end
