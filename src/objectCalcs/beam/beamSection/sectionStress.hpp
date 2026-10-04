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

// Stresses on one cross-section from its internal forces {N, Vy, Vz, T, My, Mz} (section sign
// convention, local axes, see BeamElement::sectionForces).
//
//   normal   sigma_x(y, z) = N / A - Mz y / Iz + My z / Iy        (M = E I dtheta / dx)
//            linear over the section, so its extremes lie on the convex hull: max = N / A +
//            h(a, b) with the support function h of the shape and a = -Mz / Iz, b = My / Iy;
//            every shape here is centrally symmetric, so min = N / A - h(a, b). Exact.
//   shear    from Vy and Vz: Jourawski tau = V Q / (I t) at the neutral axis (I-section,
//            weak axis: 1.5 Vz / (2 b tf) over the two flanges); the two directions are added.
//            from T: circle / pipe T r / J (exact), box T / (2 A_h t) (Bredt), rectangle
//            T (3a + 1.8c) / (a^2 c^2) (Roark), I T t_max / J (thin-walled open section).
//   von Mises  sqrt(max|sigma|^2 + 3 (tau_V + tau_T)^2): the largest normal and shear stresses
//            taken at one point, an upper bound of the true equivalent stress.

#include "beamSection.hpp"

#include <array>
#include <optional>

namespace FEM::BEAM {

  struct SectionStress {
    double maxNormal{};                     // Pa, tension > 0
    double minNormal{};                     // Pa
    std::array<double, 2> maxNormalPoint{}; // {y, z} where maxNormal acts (on the outline)
    std::array<double, 2> minNormalPoint{};
    double shearFromForce{};                // Pa, |tau| from Vy and Vz
    double shearFromTorsion{};              // Pa, |tau| from T
    double vonMises{};                      // Pa, upper bound (see above)
  };

  // Stresses of a valid shape under the given internal forces; nothing for a GeneralSection,
  // which has no shape.
  std::optional<SectionStress> sectionStress(const SectionShape& shape, const std::array<double, 6>& forces);

} // namespace FEM::BEAM end
