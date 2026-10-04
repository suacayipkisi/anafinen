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

#include "beamStress.hpp"
#include "beamDiagrams.hpp"
#include <beam/beamSection/sectionStress.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace FEM::BEAM {

  BeamStress elementStress(
    const BeamElement& element,
    const Eigen::Vector3d& localLoad,
    const double length,
    const SectionShape& shape,
    const double yieldStrength,
    const std::size_t samples
  ) {
    BeamStress result;
    if (std::holds_alternative<GeneralSection>(shape)) return result;

    std::vector<double> positions{0.0, length};
    for (std::size_t i = 1; i < samples; ++i) positions.push_back(length * static_cast<double>(i) / static_cast<double>(samples));
    // dMz/dx = -Vy0 + qy x = 0 and dMy/dx = Vz0 - qz x = 0.
    const auto& S0 = element.sectionForces;
    if (localLoad[1] != 0.0) positions.push_back(S0[1] / localLoad[1]);
    if (localLoad[2] != 0.0) positions.push_back(S0[2] / localLoad[2]);

    result.available = true;
    result.maxNormal = -std::numeric_limits<double>::infinity();
    result.minNormal = std::numeric_limits<double>::infinity();
    for (const double x : positions) {
      if (!(x >= 0.0 && x <= length)) continue;
      const auto stress = sectionStress(shape, sectionForcesAt(S0, localLoad, x));
      result.maxNormal = std::max(result.maxNormal, stress->maxNormal);
      result.minNormal = std::min(result.minNormal, stress->minNormal);
      result.maxShear = std::max(result.maxShear, stress->shearFromForce + stress->shearFromTorsion);
      if (stress->vonMises > result.maxVonMises) {
        result.maxVonMises = stress->vonMises;
        result.vonMisesPosition = x;
      }
    }
    result.isStressExceeded = result.maxVonMises > yieldStrength;
    return result;
  }

} // namespace FEM::BEAM end
