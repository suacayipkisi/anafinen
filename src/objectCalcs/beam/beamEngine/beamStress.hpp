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

// Stress extremes along a solved beam element. The internal forces follow sectionForcesAt()
// (beamDiagrams.hpp); sectionStress() (beamSection/sectionStress.hpp) gives the stresses of
// one cross-section. Candidates along the element: both ends, the stationary points of My and
// Mz (where the shear force changes sign under a distributed load, e.g. mid span) and
// `samples` evenly spaced points in between.

#include <beam/beamProperties/element.hpp>
#include <beam/beamSection/beamSection.hpp>

#include <Eigen/Core>
#include <cstddef>

namespace FEM::BEAM {

  // localLoad is the element's total uniform load in local axes (elementLocalLoads()).
  // Returns available = false for a GeneralSection.
  BeamStress elementStress(
    const BeamElement& element,
    const Eigen::Vector3d& localLoad,
    double length,
    const SectionShape& shape,
    double yieldStrength,
    std::size_t samples = 16
  );

} // namespace FEM::BEAM end
