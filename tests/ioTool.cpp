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

// Command-line helper used by the VTK reference test (and a first sketch of a CLI front end):
//   anaf_io_tool dump <file>                      canonical text dump (VTK cell order / numbering)
//   anaf_io_tool convert <in> <out> [options]     --binary --compress --vtk42 --msh22 --no-tags

#include <io/detail/vtkCommon.hpp>
#include <io/meshIo.hpp>

#include <cstdio>
#include <format>
#include <string>
#include <string_view>

using namespace anaf::IO;

namespace {

  int dump(const char* path) {
    auto result = readMesh(path);
    if (!result) {
      std::fprintf(stderr, "error: %s\n", result.error().message.c_str());
      return 2;
    }
    const MeshModel& model = *result;
    std::string out = std::format("POINTS {}\n", model.nodes.size());
    for (const auto& node : model.nodes) out += std::format("{} {} {}\n", node.position[0], node.position[1], node.position[2]);

    const auto cells = anaf::IO::detail::buildVtkCells(model);
    out += std::format("CELLS {}\n", cells.types.size());
    std::int64_t begin = 0;
    for (std::size_t c = 0; c < cells.types.size(); ++c) {
      out += std::format("{} {}", cells.types[c], cells.offsets[c] - begin);
      for (auto k = begin; k < cells.offsets[c]; ++k) out += std::format(" {}", cells.connectivity[static_cast<std::size_t>(k)]);
      out += '\n';
      begin = cells.offsets[c];
    }
    for (const auto& field : model.fields) {
      const auto& values = field.steps.back();
      out += std::format("FIELD {} {} {} {}\n", field.location == FieldLocation::Node ? "N" : "E", field.components, values.size(), field.name);
      for (std::size_t i = 0; i < values.size(); ++i) out += std::format("{}{}", values[i], i + 1 == values.size() ? "\n" : " ");
      if (values.empty()) out += '\n';
    }
    for (const auto& warning : model.warnings) out += "WARNING " + warning + "\n";
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
  }

  int convert(int argc, char** argv) {
    auto input = readMesh(argv[2]);
    if (!input) {
      std::fprintf(stderr, "read error: %s\n", input.error().message.c_str());
      return 2;
    }
    WriteOptions options;
    for (int i = 4; i < argc; ++i) {
      const std::string_view flag = argv[i];
      if (flag == "--binary") options.encoding = Encoding::Binary;
      else if (flag == "--compress") options.compress = true;
      else if (flag == "--vtk42") options.vtkVersion = VtkLegacyVersion::V4_2;
      else if (flag == "--msh22") options.mshVersion = MshVersion::V2_2;
      else if (flag == "--no-tags") options.writeTags = false;
      else {
        std::fprintf(stderr, "unknown option %s\n", argv[i]);
        return 1;
      }
    }
    const auto written = writeMesh(argv[3], *input, options);
    if (!written) {
      std::fprintf(stderr, "write error: %s\n", written.error().message.c_str());
      return 2;
    }
    for (const auto& warning : written->warnings) std::fprintf(stderr, "warning: %s\n", warning.c_str());
    return 0;
  }

} // namespace end

int main(int argc, char** argv) {
  if (argc >= 3 && std::string_view(argv[1]) == "dump") return dump(argv[2]);
  if (argc >= 4 && std::string_view(argv[1]) == "convert") return convert(argc, argv);
  std::fprintf(stderr, "usage: %s dump <file> | convert <in> <out> [--binary] [--compress] [--vtk42] [--msh22] [--no-tags]\n", argv[0]);
  return 1;
}
