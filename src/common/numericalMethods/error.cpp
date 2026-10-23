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


#include "error.hpp"
#include <cmath>
#include <limits>

namespace anaf::NUMERIC {

  double trueError(double trueValue, double approximation) {
    return ( trueValue - approximation );
  }

  double trueRelativeError(double trueValue, double approximation) {
    if (trueValue == 0.0) {
      return approximation == 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
    }
    return ( trueError(trueValue, approximation) / trueValue );
  }

  // eps_t
  double trueRelativePercentError(double trueValue, double approximation) {
    return ( trueRelativeError(trueValue, approximation) * 100 );
  }

  // eps_a
  double relativePercentApproximationError(double currentAppr, double previousAppr) {
    if (currentAppr == previousAppr) return 0.0; // also stops an iteration that lands exactly on a root at 0
    if (currentAppr == 0.0) return 100.0;
    return ( ((currentAppr - previousAppr) / currentAppr) * 100 );
  }

  // eps_s
  double errorTolerance(int significantFigures) {
    return ( 0.5 * std::pow(10.0, 2 - significantFigures) );
  }

  // |eps_a| < eps_s
  bool isTolerated(double relativePercentApprErr, double tolerance) {
    return std::abs(relativePercentApprErr) < tolerance;
  }

  bool shouldIterationStop(double currentAppr, double previousAppr, double tolerance) {
    return isTolerated(relativePercentApproximationError(currentAppr, previousAppr), tolerance);
  }

} // namespace anaf::NUMERIC end
