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

#include "supportBasis.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace FEM::SUPPORT {

  namespace {
    double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) {
      return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }
  } // namespace end

  Basis::Basis(const std::initializer_list<std::array<double, 3>> vectors) :
    Basis(std::span<const std::array<double, 3>>(vectors.begin(), vectors.size()))
  {}

  Basis::Basis(const std::span<const std::array<double, 3>> vectors) {
    if (vectors.size() > m_vectors.size()) throw std::invalid_argument("A 3D node can have at most three independent motion directions");
    std::ranges::copy(vectors, m_vectors.begin());
    m_count = static_cast<std::uint8_t>(vectors.size());
  }

  void Basis::push_back(const std::array<double, 3>& vector) {
    if (m_count == m_vectors.size()) throw std::invalid_argument("A 3D node can have at most three independent motion directions");
    m_vectors[m_count++] = vector;
  }

  bool operator==(const Basis& a, const Basis& b) {
    return std::ranges::equal(a, b);
  }

  std::array<double, 3> componentOutside(std::array<double, 3> direction, const Basis& basis) {
    for (const auto& b : basis) {
      const double projection = dot(direction, b);
      for (std::size_t axis = 0; axis < 3; ++axis) direction[axis] -= projection * b[axis];
    }
    return direction;
  }

  Basis orthonormalize(const Basis& directions) {
    Basis basis;
    for (const auto& direction : directions) {
      const double length = std::sqrt(dot(direction, direction));
      const auto residual = componentOutside(direction, basis);
      const double norm = std::sqrt(dot(residual, residual));
      if (!(length > 0.0) || norm <= 1e-9 * length) {
        throw std::invalid_argument("Directions must be non-zero and linearly independent");
      }
      basis.push_back({residual[0] / norm, residual[1] / norm, residual[2] / norm});
    }
    return basis;
  }

  Basis orthogonalComplement(const Basis& basis) {
    Basis span = basis;
    Basis complement;
    while (span.size() < 3) {
      // The global axis with the largest part outside the span gives the best-conditioned vector.
      std::array<double, 3> best{};
      double bestNorm = 0.0;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        std::array<double, 3> unit{};
        unit[axis] = 1.0;
        const auto residual = componentOutside(unit, span);
        const double norm = std::sqrt(dot(residual, residual));
        if (norm > bestNorm) {
          bestNorm = norm;
          best = residual;
        }
      }
      for (double& value : best) value /= bestNorm;
      span.push_back(best);
      complement.push_back(best);
    }
    return complement;
  }

} // namespace FEM::SUPPORT end
