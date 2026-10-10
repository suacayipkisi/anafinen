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

// Writes the built-in beam library (FEM::BEAM::LIBRARY::writeLibrary): every model as <id>.msh and,
// solved, as <id>_solved.msh (MSH 4.1 ASCII), plus index.json. Uses the built-in material list
// and section catalogue of the source tree. Default output: <source>/assets/objects/beam/beam3D.
//   anaf_beam_library_tool [output-directory]

#include <beam/beamSection/sectionLibrary.hpp>
#include <beam/beamTypes/beamLibrary.hpp>
#include <directory/getExecutableDirectory.hpp>
#include <io/core/pathUtf8.hpp>
#include <log/anaf_info.hpp>
#include <material/materialLibrary.hpp>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
namespace LIBRARY = FEM::BEAM::LIBRARY;

int main(int argc, char** argv) {
  anaf::LOG::setConsoleOutput(false);
  const fs::path dir = argc > 1 ? anaf::IO::pathFromUtf8(argv[1])
                                : anaf::IO::pathFromUtf8(MAIN_DIR) / "assets" / fs::path(LIBRARY::librarySubdir);
  const auto materials = anaf::MATERIAL::loadMaterialLibrary(anaf::IO::pathFromUtf8(MAIN_DIR) / "assets" / "bridge" / "materialProperties.json");
  const auto sections = FEM::BEAM::loadSectionLibrary(anaf::IO::pathFromUtf8(MAIN_DIR) / "assets" / fs::path(FEM::BEAM::sectionCatalogAsset));
  if (!materials || !sections) {
    std::fprintf(stderr, "%s\n", !materials ? materials.error().c_str() : sections.error().c_str());
    return 1;
  }
  const auto written = LIBRARY::writeLibrary(dir, *materials, *sections);
  if (!written) {
    std::fprintf(stderr, "%s\n", written.error().c_str());
    return 1;
  }
  for (const auto& entry : *written) std::printf("%s\n", entry.id.c_str());
  std::printf("%zu models (and solved results) written to %s\n", written->size(), anaf::IO::pathToUtf8(dir).c_str());
  return 0;
}
