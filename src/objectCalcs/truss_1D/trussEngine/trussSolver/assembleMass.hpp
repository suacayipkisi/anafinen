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

#include "trussProperties/element.hpp"
#include "trussEngine/trussSolver/reducedDofs.hpp"

#include <material/properties.hpp>

#include <Eigen/SparseCore>
#include <cstddef>
#include <span>
#include <vector>

namespace FEM::TRUSS {

  // Reduced mass matrices T^T M T (support reduction of ReducedDofs), upper triangle,
  // compressed column-major, the same layout as assembleStiffness().

  // Consistent mass (assembleReduced()).
  Eigen::SparseMatrix<double> assembleMassConsistent(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    std::size_t nodeCount,
    const ReducedDofs& dofs
  );

  // Lumped mass: diagonal, one entry per reduced DOF.
  Eigen::SparseMatrix<double> assembleMassLumped(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials,
    std::size_t nodeCount,
    const ReducedDofs& dofs
  );

} // namespace FEM::TRUSS end
