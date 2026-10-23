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

#include <Eigen/Core>
#include <array>
#include <numbers>

namespace CONSTANTS {
  
  inline constexpr std::array<double, 3> g_gravity {0.0, -9.80665, 0.0};
  inline constexpr double g_gravityAcceleration = 9.80665;

  inline constexpr double g_pi = std::numbers::pi;

} // namespace CONSTANTS end

namespace FUNCTIONS {
  
  enum CIRCLE {
    RADIUS,
    DIAMETER
  };
  
  // check is return equal zero
  inline double areaOfCircle(const double val, CIRCLE radius_diameter) {
    switch (radius_diameter) {
    case RADIUS:
      return (val * val * CONSTANTS::g_pi);
    case DIAMETER:
      return ((val * val / 4) * CONSTANTS::g_pi);
    default:
      return 0.0;
    }
  }
  
  // check is return equal zero
  inline double circumferenceOfCircle(const double val, CIRCLE radius_diameter) {
    switch (radius_diameter) {
    case RADIUS:
      return (2 * val * CONSTANTS::g_pi);
    case DIAMETER:
      return (val * CONSTANTS::g_pi);
    default:
      return 0.0;
    }
  }
} // namespace FUNCTIONS end
