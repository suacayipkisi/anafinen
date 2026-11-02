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

#include "discreteFourierTransform.hpp"
#include "globalConstantsAndFunctions.hpp"

#include <cmath>

namespace anaf::NUMERIC {

  namespace {

    // Shared kernel of the forward and inverse transform: sum_n x_n exp(sign * i 2 pi k n / N).
    // (k * n) % N keeps the angle in [0, 2 pi), so sin / cos do not lose precision for large k * n.
    std::vector<std::complex<double>> transform(std::span<const std::complex<double>> x, double sign) {
      const std::size_t size = x.size();
      std::vector<std::complex<double>> result(size);
      const double angleFactor = (2.0 * CONSTANTS::g_pi) / static_cast<double>(size);

      for (std::size_t k = 0; k < size; ++k) {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t n = 0; n < size; ++n) {
          const double theta = angleFactor * static_cast<double>((k * n) % size);
          sum += x[n] * std::polar(1.0, sign * theta);
        }
        result[k] = sum;
      }
      return result;
    }

  } // namespace end

  std::vector<std::complex<double>> dft(std::span<const std::complex<double>> x) {
    if (x.empty()) {
      return {};
    }
    return transform(x, -1.0);
  }

  std::vector<std::complex<double>> dftReal(std::span<const double> x) {
    const std::size_t size = x.size();
    if (size == 0) {
      return {};
    }

    const std::size_t numBins = (size / 2) + 1;
    std::vector<std::complex<double>> result(numBins);
    const double angleFactor = (2.0 * CONSTANTS::g_pi) / static_cast<double>(size);

    for (std::size_t k = 0; k < numBins; ++k) {
      double realSum{0.0};
      double imagSum{0.0};
      for (std::size_t n = 0; n < size; ++n) {
        const double theta = angleFactor * static_cast<double>((k * n) % size);
        realSum += x[n] * std::cos(theta);
        imagSum -= x[n] * std::sin(theta); // negative sign from exp(-i theta)
      }
      result[k] = {realSum, imagSum};
    }
    return result;
  }

  std::vector<std::complex<double>> inverseDft(std::span<const std::complex<double>> X) {
    if (X.empty()) {
      return {};
    }
    std::vector<std::complex<double>> result = transform(X, 1.0);
    const double scale = 1.0 / static_cast<double>(X.size());
    for (std::complex<double>& value : result) {
      value *= scale;
    }
    return result;
  }

  std::vector<double> dftFrequencies(std::size_t N, double dt) {
    if (N == 0 || dt <= 0.0) {
      return {};
    }
    const std::size_t numBins = (N / 2) + 1;
    const double frequencyStep = 1.0 / (static_cast<double>(N) * dt);
    std::vector<double> frequencies(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
      frequencies[k] = static_cast<double>(k) * frequencyStep;
    }
    return frequencies;
  }

} // namespace anaf::NUMERIC end
