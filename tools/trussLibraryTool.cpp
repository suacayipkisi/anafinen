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

// Writes the built-in truss library (FEM::TRUSS::LIBRARY::writeLibrary) as MSH 4.1 ASCII files
// plus index.json. Default output: <source>/assets/objects/truss/truss1D.
//   anaf_truss_library_tool [output-directory]

#include <io/core/pathUtf8.hpp>
#include <truss_1D/trussTypes/trussLibrary.hpp>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
namespace LIBRARY = FEM::TRUSS::LIBRARY;

int main(int argc, char** argv) {
  const fs::path dir = argc > 1 ? anaf::IO::pathFromUtf8(argv[1])
                                : anaf::IO::pathFromUtf8(MAIN_DIR) / "assets" / fs::path(LIBRARY::kLibrarySubdir);
  const auto written = LIBRARY::writeLibrary(dir);
  if (!written) {
    std::fprintf(stderr, "%s\n", written.error().c_str());
    return 1;
  }
  for (const auto& entry : *written) std::printf("%s\n", entry.id.c_str());
  std::printf("%zu models written to %s\n", written->size(), anaf::IO::pathToUtf8(dir).c_str());
  return 0;
}
