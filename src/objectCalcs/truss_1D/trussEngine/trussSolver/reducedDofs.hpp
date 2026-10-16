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

#include "trussProperties/node.hpp"

#include <Eigen/SparseCore>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace FEM::TRUSS {

  // Support reduction u = T q. Every node owns one reduced DOF per allowed motion direction
  // b_k (orthonormal), T(3 n + axis, k) = b_k[axis], and a global matrix A becomes T^T A T
  // (the stiffness now, the mass for modal analysis). For supports along the global axes T
  // only selects columns, the classic fixed-DOF removal; inclined supports (Logan, 5th ed.,
  // ch. 3) need the full product.
  class ReducedDofs {
  public:
    ReducedDofs() = default;
    explicit ReducedDofs(std::span<const Node> nodes);

    std::int32_t count() const { return m_count; }
    std::uint32_t inclinedNodes() const { return m_inclinedNodes; }
    // Reduced DOF of direction k of node n at 3 n + k, -1 when unused (Block-CG node blocks).
    const std::vector<std::int32_t>& nodeDofSlots() const { return m_nodeDofSlots; }

    // Calls emit(p, q, value) for every upper-triangle entry (p <= q) of T^T A T that the upper
    // entry A(row, col) (global DOFs, row <= col) and its mirror A(col, row) produce.
    template <class Emit>
    void forEachReduced(const std::uint32_t row, const std::uint32_t col, const double value, Emit&& emit) const {
      const auto visit = [&](const std::uint32_t from, const std::uint32_t to) {
        for (std::uint8_t a = 0; a < m_linkCount[from]; ++a) {
          for (std::uint8_t b = 0; b < m_linkCount[to]; ++b) {
            const auto& rowLink = m_links[from][a];
            const auto& colLink = m_links[to][b];
            if (rowLink.reduced <= colLink.reduced) emit(rowLink.reduced, colLink.reduced, rowLink.factor * colLink.factor * value);
          }
        }
      };
      visit(row, col);
      if (row != col) visit(col, row);
    }

  private:
    struct DofLink {
      std::int32_t reduced;
      double factor; // b_k[axis]
    };
    std::vector<std::array<DofLink, 3>> m_links; // per global DOF, up to three reduced DOFs
    std::vector<std::uint8_t> m_linkCount;
    std::vector<std::int32_t> m_nodeDofSlots;
    std::int32_t m_count{0};
    std::uint32_t m_inclinedNodes{0};
  };

  // Sparsity of the reduced matrix, upper triangle, compressed column-major with zero values:
  // reduced DOFs p <= q are coupled when their nodes share an element (or are the same node);
  // T is block diagonal per node, so T^T A T has no other entries. The elements are given as
  // node lists, element e at elementNodes[elementStart[e] .. elementStart[e + 1]). Rows come out
  // sorted because reduced DOFs are numbered node by node.
  Eigen::SparseMatrix<double> reducedPattern(
    const ReducedDofs& dofs,
    std::size_t nodeCount,
    std::span<const std::size_t> elementStart,
    std::span<const std::uint32_t> elementNodes
  );

  // Upper triangle of T^T A T for a global matrix A given element by element, built directly
  // as a compressed column-major matrix: nodesOf(e) gives the nodes of element e (any range of
  // node indices), entries(e, add) calls add(row, col, value) for the upper-triangle entries
  // (row <= col, global DOFs) of element e. No triplet list exists at any point: the pattern
  // comes from the node connectivity, then each reduced entry is added at its place in the
  // column (binary search, atomic add; elements run in parallel). Memory: the matrix plus the
  // element and node connectivity.
  template <class NodesOf, class Entries>
  Eigen::SparseMatrix<double> assembleReduced(
    const std::size_t elementCount,
    const std::size_t nodeCount,
    const ReducedDofs& dofs,
    NodesOf&& nodesOf,
    Entries&& entries
  ) {
    std::vector<std::size_t> elementStart(elementCount + 1, 0);
    for (std::size_t element = 0; element < elementCount; ++element) {
      elementStart[element + 1] = elementStart[element] + std::ranges::size(nodesOf(element));
    }
    std::vector<std::uint32_t> elementNodes(elementStart.back());
    #pragma omp parallel for schedule(static)
    for (long long element = 0; element < static_cast<long long>(elementCount); ++element) {
      std::size_t output = elementStart[static_cast<std::size_t>(element)];
      for (const auto node : nodesOf(static_cast<std::size_t>(element))) elementNodes[output++] = static_cast<std::uint32_t>(node);
    }

    Eigen::SparseMatrix<double> matrix = reducedPattern(dofs, nodeCount, elementStart, elementNodes);
    elementNodes = {};
    elementStart = {};

    const auto* outer = matrix.outerIndexPtr();
    const auto* inner = matrix.innerIndexPtr();
    double* values = matrix.valuePtr();
    #pragma omp parallel for schedule(static)
    for (long long element = 0; element < static_cast<long long>(elementCount); ++element) {
      entries(static_cast<std::size_t>(element), [&](const std::uint32_t row, const std::uint32_t col, const double value) {
        dofs.forEachReduced(row, col, value, [&](const std::int32_t p, const std::int32_t q, const double reduced) {
          const auto* first = inner + outer[q];
          const auto* position = std::lower_bound(first, inner + outer[q + 1], p);
          double& target = values[position - inner];
          #pragma omp atomic
          target += reduced;
        });
      });
    }
    return matrix;
  }

} // namespace FEM::TRUSS end
