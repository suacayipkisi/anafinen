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

// Roots of equations of one variable (Chapra & Canale, Numerical Methods for Engineers, ch. 5-6).
// Every method finds x with f(x) = wantedY (the root of f(x) - wantedY). It stops when
// |eps_a| < eps_s, eps_s from significantFigures (error.hpp), and gives std::nullopt with a
// warning when it cannot go on or does not converge within maxIterations.

#include <functional>
#include <optional>

namespace anaf::NUMERIC {

  inline constexpr int defaultSignificantFigures = 8;
  inline constexpr int defaultMaxIterations = 200;

  // Bracketing methods: f(lowerX) - wantedY and f(upperX) - wantedY must have opposite signs.
  std::optional<double> bisectionMethod(const std::function<double(double)>& givenFunction, double lowerX, double upperX, double wantedY,
    int significantFigures = defaultSignificantFigures, int maxIterations = defaultMaxIterations);
  std::optional<double> falsePositionMethod(const std::function<double(double)>& givenFunction, double lowerX, double upperX, double wantedY,
    int significantFigures = defaultSignificantFigures, int maxIterations = defaultMaxIterations);

  // Open methods: may diverge, then std::nullopt.
  // Fixed point uses x_{i+1} = g(x_i) with g(x) = f(x) - wantedY + x; it converges only where
  // |g'(x)| < 1, i.e. -2 < f'(x) < 0 near the root.
  std::optional<double> simpleFixedPointIteration(const std::function<double(double)>& givenFunction, double startIterate, double wantedY,
    int significantFigures = defaultSignificantFigures, int maxIterations = defaultMaxIterations);
  std::optional<double> newtonRaphsonMethod(const std::function<double(double)>& givenFunction, const std::function<double(double)>& firstDerivative,
    double startIterate, double wantedY, int significantFigures = defaultSignificantFigures, int maxIterations = defaultMaxIterations);
  // Secant needs two start values, x_{-1} (firstIterate) and x_0 (secondIterate); they need not bracket the root.
  std::optional<double> secantMethod(const std::function<double(double)>& givenFunction, double firstIterate, double secondIterate, double wantedY,
    int significantFigures = defaultSignificantFigures, int maxIterations = defaultMaxIterations);

} // namespace anaf::NUMERIC end
