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

#include "output.hpp"

#include <objectCalcs/common/supportBasis.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>

namespace anaf::CLI::OUTPUT {

  namespace {

    bool isGlobalAxis(const std::array<double, 3>& v) {
      return std::ranges::count(v, 0.0) == 2;
    }

    // Global axes outside the span of an allowed (orthonormal) basis, i.e. the held ones.
    std::vector<std::size_t> heldAxes(const std::vector<std::array<double, 3>>& allowed) {
      std::vector<std::size_t> held;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        std::array<double, 3> unit{};
        unit[axis] = 1.0;
        if (norm(FEM::SUPPORT::componentOutside(unit, allowed)) > 1e-12) held.push_back(axis);
      }
      return held;
    }

    // Appends the held DOFs of one basis ("ux,uz") or an inclined note; returns false when the
    // basis is inclined.
    bool appendHeld(std::string& text, const std::vector<std::array<double, 3>>& allowed, const std::string_view prefix) {
      if (!std::ranges::all_of(allowed, isGlobalAxis)) return false;
      for (const auto axis : heldAxes(allowed)) {
        text += std::format("{}{}{}", text.empty() ? "" : ",", prefix, static_cast<char>('x' + axis));
      }
      return true;
    }

    std::string inclinedNote(const std::string_view what, const std::size_t freeCount) {
      return std::format("inclined {}, {} free direction{}", what, freeCount, freeCount == 1 ? "" : "s");
    }

  } // namespace end

  std::string vector3(const std::array<double, 3>& v) {
    return std::format("({:.6g}, {:.6g}, {:.6g})", tidy(v[0]), tidy(v[1]), tidy(v[2]));
  }

  std::string supportLabel(const FEM::TRUSS::Node& node) {
    const auto& allowed = node.getAllowedMotionDirections();
    if (allowed.size() == 3) return "free";
    if (allowed.empty()) return "fixed";
    std::string held;
    if (!appendHeld(held, allowed, "u")) return inclinedNote("support", allowed.size());
    return "held " + held;
  }

  std::string supportLabel(const FEM::BEAM::Node& node) {
    const auto& motion = node.getAllowedMotionDirections();
    const auto& rotation = node.getAllowedRotationAxes();
    if (motion.size() == 3 && rotation.size() == 3) return "free";
    if (motion.empty() && rotation.empty()) return "fixed";
    if (motion.empty() && rotation.size() == 3) return "pinned";
    std::string held;
    std::string notes;
    if (!appendHeld(held, motion, "u")) notes = inclinedNote("motion", motion.size());
    if (!appendHeld(held, rotation, "r")) notes += (notes.empty() ? "" : "; ") + inclinedNote("rotation", rotation.size());
    if (held.empty()) return notes;
    return notes.empty() ? "held " + held : std::format("held {}; {}", held, notes);
  }

  std::string releaseLabel(const std::uint16_t releases) {
    constexpr std::array<std::string_view, 6> kNames{"N", "Vy", "Vz", "T", "My", "Mz"};
    const auto endLabel = [&](const int end) {
      const auto bits = FEM::BEAM::RELEASE::ofEnd(releases, end);
      if (bits == 0) return std::string{};
      if (bits == FEM::BEAM::RELEASE::hinge) return std::format("hinge at {}", end + 1);
      std::string names;
      for (std::size_t k = 0; k < kNames.size(); ++k) {
        if ((bits >> k) & 1U) names += std::format("{}{}", names.empty() ? "" : ",", kNames[k]);
      }
      return std::format("{} at {}", names, end + 1);
    };
    const auto first = endLabel(0);
    const auto second = endLabel(1);
    if (first.empty()) return second;
    return second.empty() ? first : first + "; " + second;
  }

  std::string_view formulationName(const FEM::BEAM::Formulation formulation) {
    return formulation == FEM::BEAM::Formulation::Timoshenko ? "Timoshenko" : "Euler-Bernoulli";
  }

  std::expected<FEM::BEAM::Formulation, std::string> parseFormulation(const std::string_view token) {
    std::string lower(token);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "eb" || lower == "euler-bernoulli" || lower == "euler") return FEM::BEAM::Formulation::EulerBernoulli;
    if (lower == "timoshenko" || lower == "ti" || lower == "timo") return FEM::BEAM::Formulation::Timoshenko;
    return std::unexpected(std::format("formulation: '{}' is neither eb nor timoshenko", token));
  }

  double tidy(const double v) {
    return std::abs(v) < std::numeric_limits<double>::min() ? 0.0 : v;
  }

  double norm(const std::array<double, 3>& v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  }

  double distance(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return norm({b[0] - a[0], b[1] - a[1], b[2] - a[2]});
  }

} // namespace anaf::CLI::OUTPUT end
