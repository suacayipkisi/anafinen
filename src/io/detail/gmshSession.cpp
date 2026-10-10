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

#include "gmshSession.hpp"

#include <gmsh.h>

namespace anaf::IO::detail {

  namespace {
    std::mutex& gmshMutex() {
      static std::mutex s_mutex;
      return s_mutex;
    }

    // Finalizes Gmsh once at process exit if any session initialized it.
    struct GmshFinalizer {
      ~GmshFinalizer() {
        if (gmsh::isInitialized()) gmsh::finalize();
      }
    };

    void applySessionDefaults() {
      gmsh::option::restoreDefaults();
      gmsh::option::setNumber("General.Terminal", 0);
      gmsh::option::setNumber("General.Verbosity", 1); // errors only
    }
  } // namespace end

  GmshSession::GmshSession() : m_lock(gmshMutex()) {
    static GmshFinalizer s_finalizer;
    if (!gmsh::isInitialized()) {
      // readConfigFiles = false: the user's gmshrc / gmsh-options must not change our output.
      gmsh::initialize(0, nullptr, false, false);
    }
    gmsh::clear();
    applySessionDefaults();
  }

  GmshSession::~GmshSession() {
    // Release model memory right away; the next session starts from a clean state anyway.
    try {
      gmsh::clear();
    } catch (...) {
    }
  }

} // namespace anaf::IO::detail end
