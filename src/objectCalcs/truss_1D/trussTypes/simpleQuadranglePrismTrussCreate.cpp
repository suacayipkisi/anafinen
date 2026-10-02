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

#include "simpleQuadranglePrismTrussCreate.hpp"
#include <log/anaf_info.hpp>

#include <cmath>
#include <cstddef>
#include <format>
#include <limits>

namespace FEM::TRUSS {

  std::expected<anaf::BRIDGE::MeshData, std::string> buildSimpleTruss(
    const std::array<std::uint32_t, 3> cubeNum,
    const double edgeLength,
    const double areaM2,
    const std::uint32_t materialIndex
  ) {
    const auto [nx, ny, nz] = cubeNum;
    if (nx == 0 || ny == 0 || nz == 0) {
      return std::unexpected("cube numbers must be at least 1");
    }
    if (!(std::isfinite(edgeLength) && edgeLength > 0.0)) {
      return std::unexpected(std::format("element length must be > 0 (got {})", edgeLength));
    }
    if (!(std::isfinite(areaM2) && areaM2 > 0.0)) {
      return std::unexpected(std::format("cross-section area must be > 0 (got {} m^2)", areaM2));
    }
    // Node ids are 32-bit and the stiffness triplets index DOFs (3 per node) with int.
    const double nodeCount = (nx + 1.0) * (ny + 1.0) * (nz + 1.0);
    if (3.0 * nodeCount > static_cast<double>(std::numeric_limits<int>::max())) {
      return std::unexpected(std::format("{:.0f} nodes are too many", nodeCount));
    }

    const std::uint32_t rowX = nx + 1;
    const std::uint32_t layer = rowX * (ny + 1);
    const auto id = [&](const std::uint32_t i, const std::uint32_t j, const std::uint32_t k) { return i + j * rowX + k * layer; };

    anaf::BRIDGE::MeshData mesh;
    mesh.trussNodes.reserve(static_cast<std::size_t>(nodeCount));
    for (std::uint32_t k = 0; k <= nz; ++k) {
      for (std::uint32_t j = 0; j <= ny; ++j) {
        for (std::uint32_t i = 0; i <= nx; ++i) {
          mesh.trussNodes.emplace_back(id(i, j, k), edgeLength * i, edgeLength * j, edgeLength * k);
        }
      }
    }

    const auto bar = [&](const std::uint32_t a, const std::uint32_t b) {
      mesh.trussElements.push_back({a, b, 0.0f, false, materialIndex, areaM2, false});
    };
    // Edges along x, y and z, then both diagonals of the xy, xz and yz faces.
    for (std::uint32_t k = 0; k <= nz; ++k) {
      for (std::uint32_t j = 0; j <= ny; ++j) {
        for (std::uint32_t i = 0; i < nx; ++i) bar(id(i, j, k), id(i + 1, j, k));
      }
    }
    for (std::uint32_t k = 0; k <= nz; ++k) {
      for (std::uint32_t j = 0; j < ny; ++j) {
        for (std::uint32_t i = 0; i <= nx; ++i) bar(id(i, j, k), id(i, j + 1, k));
      }
    }
    for (std::uint32_t k = 0; k < nz; ++k) {
      for (std::uint32_t j = 0; j <= ny; ++j) {
        for (std::uint32_t i = 0; i <= nx; ++i) bar(id(i, j, k), id(i, j, k + 1));
      }
    }
    for (std::uint32_t k = 0; k <= nz; ++k) {
      for (std::uint32_t j = 0; j < ny; ++j) {
        for (std::uint32_t i = 0; i < nx; ++i) {
          bar(id(i, j, k), id(i + 1, j + 1, k));
          bar(id(i + 1, j, k), id(i, j + 1, k));
        }
      }
    }
    for (std::uint32_t k = 0; k < nz; ++k) {
      for (std::uint32_t j = 0; j <= ny; ++j) {
        for (std::uint32_t i = 0; i < nx; ++i) {
          bar(id(i, j, k), id(i + 1, j, k + 1));
          bar(id(i + 1, j, k), id(i, j, k + 1));
        }
      }
    }
    for (std::uint32_t k = 0; k < nz; ++k) {
      for (std::uint32_t j = 0; j < ny; ++j) {
        for (std::uint32_t i = 0; i <= nx; ++i) {
          bar(id(i, j, k), id(i, j + 1, k + 1));
          bar(id(i, j + 1, k), id(i, j, k + 1));
        }
      }
    }

    anaf::LOG::success("Simple truss created: {} nodes, {} bars", mesh.trussNodes.size(), mesh.trussElements.size());
    return mesh;
  }

} // namespace FEM::TRUSS end
