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

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace FEM::TRUSS {

  // Time dependence of a nodal force F(t) = F0 * a(t); F0 is the force vector of ForceApplied.
  enum class E_ForceType {
    Constant, // a(t) = 1
    Harmonic, // F0 is the amplitude: F(t) = F0 sin(omega t + phase); omega belongs to the analysis
    Tabular   // a(t) piecewise linear through (times[i], factors[i])
  };

  constexpr std::string_view forceTypeName(const E_ForceType type) {
    switch (type) {
      case E_ForceType::Constant: return "constant";
      case E_ForceType::Harmonic: return "harmonic";
      case E_ForceType::Tabular: return "tabular";
    }
    return "unknown";
  }

  struct ConstantForce {
    bool operator==(const ConstantForce&) const = default;
  };

  struct HarmonicForce {
    double phase = 0.0; // rad
    bool operator==(const HarmonicForce&) const = default;
  };

  // Held constant before the first and after the last point, like anaf::IO::Amplitude.
  struct TabularForce {
    std::vector<double> times;   // s, non-decreasing
    std::vector<double> factors; // dimensionless, scales the force vector
    bool operator==(const TabularForce&) const = default;
  };

  class ForceApplied {
  private:
    std::uint32_t m_nodeID;
    std::array<double, 3> m_force;
    std::variant<ConstantForce, HarmonicForce, TabularForce> m_time; // alternatives in E_ForceType order

  public:
    ForceApplied(std::uint32_t nodeID, std::array<double, 3> force):
      m_nodeID(nodeID), m_force(force), m_time(ConstantForce{})
    {}

    ForceApplied(std::uint32_t nodeID, std::array<double, 3> amplitude, HarmonicForce harmonic):
      m_nodeID(nodeID), m_force(amplitude), m_time(harmonic)
    {}

    ForceApplied(std::uint32_t nodeID, std::array<double, 3> force, TabularForce tabular):
      m_nodeID(nodeID), m_force(force)
    {
      if (tabular.times.empty() || tabular.times.size() != tabular.factors.size()) {
        throw std::invalid_argument("tabular force needs the same number (at least one) of times and factors");
      }
      for (std::size_t i = 1; i < tabular.times.size(); ++i) {
        if (tabular.times[i] < tabular.times[i - 1]) throw std::invalid_argument("tabular force times must be non-decreasing");
      }
      m_time = std::move(tabular);
    }

    inline E_ForceType getForceType() const {return static_cast<E_ForceType>(m_time.index());}
    inline std::uint32_t getAppliedNode() const {return m_nodeID;}
    // The constant force, the harmonic amplitude or the vector the tabular factors scale.
    inline const std::array<double, 3>& getForce() const {return m_force;}
    // nullptr unless the force has that type.
    inline const HarmonicForce* getHarmonic() const {return std::get_if<HarmonicForce>(&m_time);}
    inline const TabularForce* getTabular() const {return std::get_if<TabularForce>(&m_time);}

    inline void setAppliedNode(std::uint32_t nodeID) {m_nodeID = nodeID;}

    bool operator==(const ForceApplied&) const = default;
  };

} // namespace FEM::TRUSS end
