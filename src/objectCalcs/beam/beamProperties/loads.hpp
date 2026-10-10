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

#include <array>
#include <cstdint>

namespace FEM::BEAM {

  // Concentrated force and moment on a node, global axes.
  struct NodalLoad {
    std::uint32_t node{};
    std::array<double, 3> force{};  // N
    std::array<double, 3> moment{}; // N m, moment vector (right-hand rule)
  };

  enum class E_LoadFrame : std::uint8_t {
    Global, // components along the global X / Y / Z axes
    Local   // components along the element's local x / y / z axes
  };

  // Uniform load per unit length over a whole element. It enters the solve as its work
  // equivalent nodal loads (wL/2 forces, wL^2/12 end moments), and the section forces are
  // corrected by the same fixed-end values.
  struct DistributedLoad {
    std::uint32_t element{};        // index into MeshData::elements
    std::array<double, 3> value{};  // N/m
    E_LoadFrame frame{E_LoadFrame::Global};
  };

} // namespace FEM::BEAM end
