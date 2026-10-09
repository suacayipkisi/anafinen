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

// Text output and small parsers shared by the command handlers (internal to the CLI).

#include <beam/beamProperties/element.hpp>
#include <beam/beamProperties/node.hpp>
#include <truss_1D/trussProperties/node.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace anaf::CLI::OUTPUT {

  // "(x, y, z)", 6 significant digits.
  std::string vector3(const std::array<double, 3>& v);

  // "free", "fixed", "held ux,uz" or "inclined, 1 free direction".
  std::string supportLabel(const FEM::TRUSS::Node& node);
  // The same for a beam node; translations and rotations ("pinned" = translations held).
  std::string supportLabel(const FEM::BEAM::Node& node);

  // "", "hinge at 1", "N,T at 2", ...
  std::string releaseLabel(std::uint16_t releases);

  std::string_view formulationName(FEM::BEAM::Formulation formulation);
  // "eb" / "euler-bernoulli" / "timoshenko" / "ti".
  std::expected<FEM::BEAM::Formulation, std::string> parseFormulation(std::string_view token);

  // v with -0 (and denormals) shown as 0. Not "v + 0.0": -ffast-math drops it (no signed zeros).
  double tidy(double v);

  // Euclidean length.
  double norm(const std::array<double, 3>& v);
  double distance(const std::array<double, 3>& a, const std::array<double, 3>& b);

} // namespace anaf::CLI::OUTPUT end
