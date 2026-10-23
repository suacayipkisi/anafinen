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

// Orthonormal bases for supports: the directions a node may move along (or rotate about) and
// their complement. Shared by every element type (truss, beam) and the GUI support editors.

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace FEM::SUPPORT {

  // Up to three 3D vectors stored inline (no heap), so a node holding its support bases is
  // trivially copyable. Used like a small std::vector: size(), empty(), operator[], range-for,
  // push_back(). Constructing it from more than three vectors throws std::invalid_argument.
  class Basis {
  private:
    std::array<std::array<double, 3>, 3> m_vectors{};
    std::uint8_t m_count{0};

  public:
    Basis() = default;
    Basis(std::initializer_list<std::array<double, 3>> vectors);
    Basis(std::span<const std::array<double, 3>> vectors);
    Basis(const std::vector<std::array<double, 3>>& vectors) : Basis(std::span<const std::array<double, 3>>(vectors)) {}

    // The three global axes (a free node).
    static Basis globalAxes() { return {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}; }

    std::size_t size() const { return m_count; }
    bool empty() const { return m_count == 0; }
    const std::array<double, 3>& operator[](const std::size_t index) const { return m_vectors[index]; }
    const std::array<double, 3>* begin() const { return m_vectors.data(); }
    const std::array<double, 3>* end() const { return m_vectors.data() + m_count; }
    // Throws std::invalid_argument when the basis already holds three vectors.
    void push_back(const std::array<double, 3>& vector); // NOLINT(readability-identifier-naming): std container name
    void clear() { m_count = 0; }

    std::vector<std::array<double, 3>> toVector() const { return {begin(), end()}; }

    // Compares the stored vectors only (unused slots do not count).
    friend bool operator==(const Basis& a, const Basis& b);
  };

  // Orthonormal basis (Gram-Schmidt) of the span of directions. Throws std::invalid_argument
  // when a direction is zero or (numerically) depends on the ones before it.
  Basis orthonormalize(const Basis& directions);

  // Orthonormal basis of the directions perpendicular to an orthonormal basis: the allowed
  // motion of a support from its restrained directions, and the other way round.
  Basis orthogonalComplement(const Basis& basis);

  // direction minus its components along an orthonormal basis.
  std::array<double, 3> componentOutside(std::array<double, 3> direction, const Basis& basis);

} // namespace FEM::SUPPORT end
