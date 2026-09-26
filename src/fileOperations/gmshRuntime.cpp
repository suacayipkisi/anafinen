// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include "gmshRuntime.hpp"

#include <gmsh.h>

namespace anaf::FILE {
  
  namespace {
    struct GmshSessionManager {
      ~GmshSessionManager() {
        if (gmsh::isInitialized()) {
          gmsh::finalize();
        }
      }
    };
  }

  void resetGmshSession() {
    static GmshSessionManager sessionGuard; // Exits cleanly when program terminates

    if (!gmsh::isInitialized()) {
      gmsh::initialize();
      gmsh::option::setNumber("General.Terminal", 0);
    }
    gmsh::clear();
  }

  void finalizeGmshSession() {
    if (gmsh::isInitialized()) {
      gmsh::finalize();
    }
  }

} // namespace anaf::FILE end
