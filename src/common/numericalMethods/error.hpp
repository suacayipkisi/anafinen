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

// Error definitions of iterative methods (Chapra & Canale, Numerical Methods for Engineers, ch. 3).

namespace anaf::NUMERIC {

  // E_t = true - approximation
  double trueError(double trueValue, double approximation);
  // E_t / true; 0 when both are 0, infinity when only the true value is 0
  double trueRelativeError(double trueValue, double approximation);

  // eps_t
  double trueRelativePercentError(double trueValue, double approximation);

  // eps_a = (current - previous) / current * 100; 0 when both are 0, 100 when only current is 0
  double relativePercentApproximationError(double currentAppr, double previousAppr);

  // eps_s = (0.5 * 10^(2 - n)) %, result correct to at least n significant figures
  double errorTolerance(int significantFigures);

  // |eps_a| < eps_s
  bool isTolerated(double relativePercentApprErr, double tolerance);

  // tolerance is eps_s in percent
  bool shouldIterationStop(double currentAppr, double previousAppr, double tolerance);

} // namespace anaf::NUMERIC end
