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

#include <mutex>

namespace anaf::IO::detail {

  // Exclusive access to the process-wide Gmsh API state. Gmsh is not thread-safe, so every
  // Gmsh-based reader/writer holds one GmshSession for its whole duration. On entry the model
  // is cleared and all options are restored to defaults, so no state leaks between operations
  // (for example mesh size limits set by a previous CAD import).
  class GmshSession {
  public:
    GmshSession();
    ~GmshSession();

    GmshSession(const GmshSession&) = delete;
    GmshSession& operator=(const GmshSession&) = delete;

  private:
    std::unique_lock<std::mutex> m_lock;
  };

} // namespace anaf::IO::detail end
