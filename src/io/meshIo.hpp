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

// Public, synchronous mesh I/O API of the anaf_io library. Safe to call from any thread:
// Gmsh-based formats serialize themselves internally. For non-blocking use (GUI) see
// ioService.hpp; a CLI can call these functions directly.

#include "core/ioTypes.hpp"
#include "model/meshModel.hpp"

#include <expected>
#include <filesystem>
#include <span>

namespace anaf::IO {

  // Extension first, then content sniffing. Returns FileFormat::Auto when unknown.
  FileFormat detectFormat(const std::filesystem::path& path);

  // Formats in the order they should be offered to users (dialog filters, CLI help).
  std::span<const FormatDescriptor> supportedFormats();

  std::expected<MeshModel, IoError> readMesh(const std::filesystem::path& path, const ReadOptions& options = {},
                                             const IoContext& context = {});

  std::expected<WriteReport, IoError> writeMesh(const std::filesystem::path& path, const MeshModel& model,
                                                const WriteOptions& options = {}, const IoContext& context = {});

} // namespace anaf::IO end
