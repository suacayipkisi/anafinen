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

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace anaf::NUMERIC {

  // Unscaled forward DFT: X_k = sum_n x_n exp(-i 2 pi k n / N), k = 0..N-1, O(N^2).
  std::vector<std::complex<double>> dft(std::span<const std::complex<double>> x);

  // Real input, returns bins k = 0..N/2 (Hermitian symmetry).
  std::vector<std::complex<double>> dftReal(std::span<const double> x);

  // Inverse with 1/N scaling: x_n = (1/N) sum_k X_k exp(+i 2 pi k n / N).
  std::vector<std::complex<double>> inverseDft(std::span<const std::complex<double>> X);

  // Bin frequencies f_k = k / (N dt) for k = 0..N/2.
  std::vector<double> dftFrequencies(std::size_t N, double dt);

} // namespace anaf::NUMERIC end
