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
#include <cstdint>

namespace FEM::BEAM {

  enum class Formulation : std::uint8_t {
    EulerBernoulli, // no shear deformation; shear areas are ignored
    Timoshenko      // shear deformation through ShearAreaY / ShearAreaZ
  };

  // Section values in the element's local axes (principal axes, no product of inertia).
  // Names match the anaf_io attributes (SecondMomentY/Z, TorsionConstant, ShearAreaY/Z).
  struct Section {
    double area{};            // A, m^2
    double secondMomentY{};   // Iy, m^4: bending about local y (deflection along local z)
    double secondMomentZ{};   // Iz, m^4: bending about local z (deflection along local y)
    double torsionConstant{}; // J, m^4: St. Venant torsion constant (not the polar moment)
    double shearAreaY{};      // Asy = kappa_y A, m^2: carries Vy; Timoshenko only
    double shearAreaZ{};      // Asz = kappa_z A, m^2: carries Vz; Timoshenko only
  };

  // Two-node 3D beam as the snapshot keeps it.
  //
  // Local axes (Nastran CBEAM / ANSYS convention, the same as anaf_io BeamOrientation):
  //   x = node1 -> node2, z = normalize(x cross v), y = z cross x
  // so the orientation vector v (global axes) lies in the local x-y plane. A zero v selects
  // the default: v = global +Y (the project's up axis), or global +X when the element is
  // parallel to Y. A v parallel to the element axis is an error.
  struct BeamElement {
    std::uint32_t node1{};
    std::uint32_t node2{};
    std::uint32_t materialID{}; // index into the material list
    Section section{};
    Formulation formulation{Formulation::EulerBernoulli};
    std::array<double, 3> orientation{}; // v; zero = default rule above

    // Result: end forces in section sign convention, local axes,
    // {N, Vy, Vz, T, My, Mz} at node1, then the same at node2 (N, m).
    // Node1 values are -(k u - f0), node2 values +(k u - f0), so N > 0 is tension at both ends.
    std::array<double, 12> sectionForces{};
  };

} // namespace FEM::BEAM end
