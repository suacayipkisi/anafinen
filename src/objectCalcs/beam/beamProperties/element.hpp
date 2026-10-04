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

  // Section values the solver uses, in the element's local axes (principal axes through the
  // centroid = shear center: doubly symmetric sections only). Names match the anaf_io
  // attributes (SecondMomentY/Z, TorsionConstant, ShearAreaY/Z). Computed from a BeamSection
  // (beamSection/beamSection.hpp) for each element's material.
  struct SectionProperties {
    double area{};            // A, m^2
    double secondMomentY{};   // Iy, m^4: bending about local y (deflection along local z)
    double secondMomentZ{};   // Iz, m^4: bending about local z (deflection along local y)
    double torsionConstant{}; // J, m^4: St. Venant torsion constant (not the polar moment)
    double shearAreaY{};      // Asy = kappa_y A, m^2: carries Vy; Timoshenko only
    double shearAreaZ{};      // Asz = kappa_z A, m^2: carries Vz; Timoshenko only
  };

  // Stress result of one element: extremes along the whole element (beamStress.hpp).
  struct BeamStress {
    bool available{false};        // false for a general section (no shape, no stress)
    double maxNormal{};           // Pa, largest sigma_x (tension > 0)
    double minNormal{};           // Pa, smallest sigma_x (most compressive)
    double maxShear{};            // Pa, largest shear stress (shear force + torsion)
    double maxVonMises{};         // Pa, largest equivalent stress (an upper bound)
    double vonMisesPosition{};    // m from node 1 where maxVonMises occurs
    bool isStressExceeded{false}; // maxVonMises > the material's yield strength
  };

  // End releases (hinges) of BeamElement::endReleases. Bit k frees local DOF k of an element
  // end from its node, DOF order {ux, uy, uz, rx, ry, rz}, so it names the section force that
  // becomes zero there: {N, Vy, Vz, T, My, Mz}. Bits 0..5 belong to node 1, bits 6..11 to node 2
  // (atNode2()). The released DOFs are condensed out of the element (static condensation): the
  // element end moves on its own in that direction, the node keeps its other connections.
  namespace RELEASE {
    inline constexpr std::uint16_t axial = 1U << 0;    // N  (ux)
    inline constexpr std::uint16_t shearY = 1U << 1;   // Vy (uy)
    inline constexpr std::uint16_t shearZ = 1U << 2;   // Vz (uz)
    inline constexpr std::uint16_t torsion = 1U << 3;  // T  (rx)
    inline constexpr std::uint16_t momentY = 1U << 4;  // My (ry)
    inline constexpr std::uint16_t momentZ = 1U << 5;  // Mz (rz)
    inline constexpr std::uint16_t hinge = momentY | momentZ; // bending hinge (pin), torsion kept
    inline constexpr std::uint16_t endMask = 0x3F;   // the six bits of one end
    inline constexpr std::uint16_t allMask = 0xFFF;  // both ends
    constexpr std::uint16_t atNode2(const std::uint16_t endBits) { return static_cast<std::uint16_t>((endBits & endMask) << 6); }
    constexpr std::uint16_t ofEnd(const std::uint16_t releases, const int end) {
      return static_cast<std::uint16_t>((releases >> (6 * end)) & endMask);
    }
  } // namespace RELEASE end

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
    std::uint32_t sectionID{};  // index into the section list (like materialID)
    Formulation formulation{Formulation::EulerBernoulli};
    std::array<double, 3> orientation{}; // v; zero = default rule above
    std::uint16_t endReleases{};         // RELEASE bits; 0 = rigidly connected at both ends

    // Result: end forces in section sign convention, local axes,
    // {N, Vy, Vz, T, My, Mz} at node1, then the same at node2 (N, m).
    // Node1 values are -(k u - f0), node2 values +(k u - f0), so N > 0 is tension at both ends.
    std::array<double, 12> sectionForces{};
    BeamStress stress{};
  };

} // namespace FEM::BEAM end
