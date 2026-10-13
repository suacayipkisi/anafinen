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


#include "rootsOfEquation.hpp"
#include "log/anaf_info.hpp"
#include "numericalMethods/error.hpp"
#include <cmath>
#include <functional>
#include <optional>

namespace anaf::NUMERIC {

  namespace {
    // Makes function to finding root problem with shifting the curve at y axis according to the wanted y
    double theFunction(const std::function<double(double)>& givenFunction, double inputValue, double difference) {
      return (givenFunction(inputValue) - difference);
    }

    // Common start of the bracketing methods: an end that already is the root, or a bracket without a sign change
    std::optional<double> checkBracket(double lowerX, double upperX, double theXl, double theXu, double wantedY, bool& bracketValid) {
      bracketValid = false;
      if (theXl == 0.0) return lowerX;
      if (theXu == 0.0) return upperX;
      if (theXl * theXu > 0) {
        anaf::LOG::warn("f(x_l={}) - y and f(x_u={}) - y have the same sign, the bracket does not contain wanted_y = {}", lowerX, upperX, wantedY);
        return std::nullopt;
      }
      bracketValid = true;
      return std::nullopt;
    }

    void warnNotConverged(const char* method, int maxIterations, double lastIterate) {
      anaf::LOG::warn("{} did not converge in {} iterations (last iterate = {})", method, maxIterations, lastIterate);
    }
  }

  //////////////////////////////////////////
  ///////////Bracketing Methods/////////////
  //////////////////////////////////////////

  std::optional<double> bisectionMethod(const std::function<double(double)>& givenFunction, double lowerX, double upperX, double wantedY,
    int significantFigures, int maxIterations) {
    double theXl = theFunction(givenFunction, lowerX, wantedY);
    const double theXu = theFunction(givenFunction, upperX, wantedY);
    bool bracketValid{};
    if (auto early = checkBracket(lowerX, upperX, theXl, theXu, wantedY, bracketValid); !bracketValid) return early;

    const double tolerance = errorTolerance(significantFigures);
    double xLower{lowerX};
    double xUpper{upperX};
    double xRoot{xLower};
    for (int iter = 0; iter < maxIterations; ++iter) {
      const double xRootPrevious = xRoot;
      xRoot = (xLower + xUpper) / 2;
      const double theXr = theFunction(givenFunction, xRoot, wantedY);

      // the sign test is against the new estimate, f(x_l) * f(x_r), not the two ends
      const double test = theXl * theXr;
      if (test < 0) {
        xUpper = xRoot;
      }
      else if (test > 0) {
        xLower = xRoot;
        theXl = theXr;
      }
      else {
        return xRoot; // f(x_r) is exactly the wanted value
      }
      if (iter > 0 && shouldIterationStop(xRoot, xRootPrevious, tolerance)) {
        return xRoot;
      }
    }
    warnNotConverged("bisection", maxIterations, xRoot);
    return std::nullopt;
  }

  std::optional<double> falsePositionMethod(const std::function<double(double)>& givenFunction, double lowerX, double upperX, double wantedY,
    int significantFigures, int maxIterations) {
    double theXl = theFunction(givenFunction, lowerX, wantedY);
    double theXu = theFunction(givenFunction, upperX, wantedY);
    bool bracketValid{};
    if (auto early = checkBracket(lowerX, upperX, theXl, theXu, wantedY, bracketValid); !bracketValid) return early;

    const double tolerance = errorTolerance(significantFigures);
    double xLower{lowerX};
    double xUpper{upperX};
    double xRoot{xLower};
    for (int iter = 0; iter < maxIterations; ++iter) {
      const double xRootPrevious = xRoot;
      // x_r = x_u - f(x_u) (x_l - x_u) / (f(x_l) - f(x_u)); the bracket keeps f(x_l) != f(x_u)
      xRoot = xUpper - ( (theXu * (xLower - xUpper)) / (theXl - theXu) );
      const double theXr = theFunction(givenFunction, xRoot, wantedY);

      const double test = theXl * theXr;
      if (test < 0) {
        xUpper = xRoot;
        theXu = theXr;
      }
      else if (test > 0) {
        xLower = xRoot;
        theXl = theXr;
      }
      else {
        return xRoot;
      }
      if (iter > 0 && shouldIterationStop(xRoot, xRootPrevious, tolerance)) {
        return xRoot;
      }
    }
    warnNotConverged("false position", maxIterations, xRoot);
    return std::nullopt;
  }

  //////////////////////////////////////////
  //////////////Open Methods////////////////
  //////////////////////////////////////////

  std::optional<double> simpleFixedPointIteration(const std::function<double(double)>& givenFunction, double startIterate, double wantedY,
    int significantFigures, int maxIterations) {
    const double tolerance = errorTolerance(significantFigures);
    double iteration{startIterate};
    for (int iter = 0; iter < maxIterations; ++iter) {
      const double iterPrevious = iteration;
      iteration = theFunction(givenFunction, iteration, wantedY) + iteration; // g(x) = f(x) - y + x
      if (!std::isfinite(iteration)) {
        anaf::LOG::warn("fixed point iteration diverged from start = {} (|g'(x)| >= 1 near the root?)", startIterate);
        return std::nullopt;
      }
      if (shouldIterationStop(iteration, iterPrevious, tolerance)) {
        return iteration;
      }
    }
    warnNotConverged("fixed point iteration", maxIterations, iteration);
    return std::nullopt;
  }

  std::optional<double> newtonRaphsonMethod(const std::function<double(double)>& givenFunction, const std::function<double(double)>& firstDerivative,
    double startIterate, double wantedY, int significantFigures, int maxIterations) {
    const double tolerance = errorTolerance(significantFigures);
    double iteration{startIterate};
    for (int iter = 0; iter < maxIterations; ++iter) {
      const double iterPrevious = iteration;
      const double theX = theFunction(givenFunction, iteration, wantedY);
      const double theXfirstDeriv = firstDerivative(iteration); // (f(x) - y)' = f'(x), the shift does not change the slope
      if (theXfirstDeriv == 0.0 || !std::isfinite(theXfirstDeriv)) {
        anaf::LOG::warn("the first derivative is {} at x = {}, Newton-Raphson cannot go on (start = {})", theXfirstDeriv, iteration, startIterate);
        return std::nullopt;
      }
      iteration = iteration - (theX / theXfirstDeriv);
      if (!std::isfinite(iteration)) {
        anaf::LOG::warn("Newton-Raphson diverged from start = {}", startIterate);
        return std::nullopt;
      }
      if (shouldIterationStop(iteration, iterPrevious, tolerance)) {
        return iteration;
      }
    }
    warnNotConverged("Newton-Raphson", maxIterations, iteration);
    return std::nullopt;
  }

  std::optional<double> secantMethod(const std::function<double(double)>& givenFunction, double firstIterate, double secondIterate, double wantedY,
    int significantFigures, int maxIterations) {
    const double tolerance = errorTolerance(significantFigures);
    double iterPrevious{firstIterate};
    double iteration{secondIterate};
    double theXprevious = theFunction(givenFunction, iterPrevious, wantedY);
    for (int iter = 0; iter < maxIterations; ++iter) {
      const double theX = theFunction(givenFunction, iteration, wantedY);
      if (theX == 0.0) return iteration;
      if (theXprevious == theX) {
        anaf::LOG::warn("f(x_(i-1)) = f(x_i) at x = {} and {}, the secant is flat and cannot go on", iterPrevious, iteration);
        return std::nullopt;
      }
      // x_(i+1) = x_i - f(x_i) (x_(i-1) - x_i) / (f(x_(i-1)) - f(x_i))
      const double next = iteration - ((theX * (iterPrevious - iteration)) / (theXprevious - theX));
      if (!std::isfinite(next)) {
        anaf::LOG::warn("secant method diverged from start = {}, {}", firstIterate, secondIterate);
        return std::nullopt;
      }
      iterPrevious = iteration;
      theXprevious = theX;
      iteration = next;
      if (shouldIterationStop(iteration, iterPrevious, tolerance)) {
        return iteration;
      }
    }
    warnNotConverged("secant method", maxIterations, iteration);
    return std::nullopt;
  }

} // namespace anaf::NUMERIC end
