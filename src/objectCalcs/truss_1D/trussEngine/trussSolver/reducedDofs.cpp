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

#include "reducedDofs.hpp"

#include <algorithm>
#include <vector>

namespace FEM::TRUSS {

  ReducedDofs::ReducedDofs(const std::span<const Node> nodes) :
    m_links(3 * nodes.size()),
    m_linkCount(3 * nodes.size(), 0),
    m_nodeDofSlots(3 * nodes.size(), -1)
  {
    for (std::size_t node = 0; node < nodes.size(); ++node) {
      const auto& directions = nodes[node].getAllowedMotionDirections(); // b_k
      if (nodes[node].hasInclinedSupport()) ++m_inclinedNodes;
      for (std::size_t k = 0; k < directions.size(); ++k) { // one reduced DOF per orthonormal direction b_k
        const std::int32_t reduced = m_count++;
        m_nodeDofSlots[3 * node + k] = reduced;

        // Link every global x / y / z DOF to this reduced DOF with the direction cosine b_k[axis].
        for (std::size_t axis = 0; axis < 3; ++axis) {
          if (directions[k][axis] == 0.0) continue;
          const std::size_t dof = 3 * node + axis;
          m_links[dof][m_linkCount[dof]++] = {reduced, directions[k][axis]};
        }
      }
    }
  }

  Eigen::SparseMatrix<double> reducedPattern(
    const ReducedDofs& dofs,
    const std::size_t nodeCount,
    const std::span<const std::size_t> elementStart,
    const std::span<const std::uint32_t> elementNodes
  ) {
    const std::size_t elementCount = elementStart.size() - 1;
    const auto& slots = dofs.nodeDofSlots();
    const auto dofsOf = [&](const std::size_t node) {
      std::size_t count = 0;
      while (count < 3 && slots[3 * node + count] >= 0) ++count;
      return count;
    };

    // Node -> elements (compressed).
    std::vector<std::size_t> nodeStart(nodeCount + 1, 0);
    for (const auto node : elementNodes) ++nodeStart[node + 1];
    for (std::size_t node = 0; node < nodeCount; ++node) nodeStart[node + 1] += nodeStart[node];
    std::vector<std::uint32_t> nodeElements(nodeStart.back());
    {
      std::vector<std::size_t> fill(nodeStart.begin(), nodeStart.end() - 1);
      for (std::size_t element = 0; element < elementCount; ++element) {
        for (std::size_t k = elementStart[element]; k < elementStart[element + 1]; ++k) {
          nodeElements[fill[elementNodes[k]]++] = static_cast<std::uint32_t>(element);
        }
      }
    }

    // Neighbors of a node with a lower or equal index (the node itself included), sorted:
    // their reduced DOFs are the rows of the node's columns (p <= q).
    const auto lowerNeighbors = [&](const std::size_t node, std::vector<std::uint32_t>& neighbors) {
      neighbors.clear();
      neighbors.push_back(static_cast<std::uint32_t>(node));
      for (std::size_t e = nodeStart[node]; e < nodeStart[node + 1]; ++e) {
        const std::size_t element = nodeElements[e];
        for (std::size_t k = elementStart[element]; k < elementStart[element + 1]; ++k) {
          if (elementNodes[k] < node) neighbors.push_back(elementNodes[k]);
        }
      }
      std::ranges::sort(neighbors);
      const auto [last, end] = std::ranges::unique(neighbors);
      neighbors.erase(last, end);
    };

    // Pass 1: entries per column. Column q = slots[3 n + k] has the DOFs of the lower
    // neighbors (all of them, they come first) plus the node's own slots 0..k.
    const std::int32_t count = dofs.count();
    std::vector<std::int64_t> columnStart(static_cast<std::size_t>(count) + 1, 0);
    #pragma omp parallel
    {
      std::vector<std::uint32_t> neighbors;
      #pragma omp for schedule(dynamic, 256)
      for (long long node = 0; node < static_cast<long long>(nodeCount); ++node) {
        const std::size_t own = dofsOf(static_cast<std::size_t>(node));
        if (own == 0) continue;
        lowerNeighbors(static_cast<std::size_t>(node), neighbors);
        std::size_t lower = 0;
        for (const auto neighbor : neighbors) {
          if (neighbor != node) lower += dofsOf(neighbor);
        }
        for (std::size_t k = 0; k < own; ++k) {
          columnStart[static_cast<std::size_t>(slots[3 * static_cast<std::size_t>(node) + k]) + 1] = static_cast<std::int64_t>(lower + k + 1);
        }
      }
    }
    for (std::size_t q = 0; q < static_cast<std::size_t>(count); ++q) columnStart[q + 1] += columnStart[q];

    Eigen::SparseMatrix<double> matrix(count, count);
    matrix.resizeNonZeros(static_cast<Eigen::Index>(columnStart.back()));
    auto* outer = matrix.outerIndexPtr();
    for (std::size_t q = 0; q <= static_cast<std::size_t>(count); ++q) outer[q] = static_cast<int>(columnStart[q]);
    columnStart = {};
    auto* inner = matrix.innerIndexPtr();
    std::fill(matrix.valuePtr(), matrix.valuePtr() + matrix.nonZeros(), 0.0);

    // Pass 2: the rows, in node order (ascending reduced DOFs).
    #pragma omp parallel
    {
      std::vector<std::uint32_t> neighbors;
      #pragma omp for schedule(dynamic, 256)
      for (long long node = 0; node < static_cast<long long>(nodeCount); ++node) {
        const std::size_t own = dofsOf(static_cast<std::size_t>(node));
        if (own == 0) continue;
        lowerNeighbors(static_cast<std::size_t>(node), neighbors);
        for (std::size_t k = 0; k < own; ++k) {
          const std::int32_t q = slots[3 * static_cast<std::size_t>(node) + k];
          int output = outer[q];
          for (const auto neighbor : neighbors) {
            const std::size_t rows = neighbor == node ? k + 1 : dofsOf(neighbor);
            for (std::size_t r = 0; r < rows; ++r) inner[output++] = slots[3 * static_cast<std::size_t>(neighbor) + r];
          }
        }
      }
    }
    return matrix;
  }

} // namespace FEM::TRUSS end
