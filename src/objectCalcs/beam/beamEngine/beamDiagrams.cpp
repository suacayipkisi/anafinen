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

#include "beamDiagrams.hpp"
#include "beamSolver/deformationUnderConstForce.hpp"

#include <stdexcept>

namespace FEM::BEAM {

  namespace {
    // Homogeneous bending shape functions for {v1, r1, v2, r2} in one plane (Przemieniecki):
    // phi = 0 gives the Hermite cubics.
    Eigen::Vector4d bendingShape(const double xi, const double length, const double phi) {
      const double xi2 = xi * xi;
      const double xi3 = xi2 * xi;
      const double scale = 1.0 / (1.0 + phi);
      return scale * Eigen::Vector4d(
        1.0 - 3.0 * xi2 + 2.0 * xi3 + phi * (1.0 - xi),
        length * (xi - 2.0 * xi2 + xi3 + 0.5 * phi * (xi - xi2)),
        3.0 * xi2 - 2.0 * xi3 + phi * xi,
        length * (-xi2 + xi3 - 0.5 * phi * (xi - xi2))
      );
    }
  } // namespace end

  SectionState sectionAt(
    const MeshData& solved,
    const std::size_t element,
    const double xi,
    const Eigen::Vector3d& localLoad,
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    if (!solved.hasResults) throw std::invalid_argument("the beam model has no results");
    if (element >= solved.elements.size()) throw std::invalid_argument("element index out of range");
    if (!(xi >= 0.0 && xi <= 1.0)) throw std::invalid_argument("xi must be in [0, 1]");

    const auto& beam = solved.elements[element];
    const auto& start = solved.nodes[beam.node1];
    const auto& end = solved.nodes[beam.node2];
    const Eigen::Vector3d p1(start.getLocation().data());
    const Eigen::Vector3d p2(end.getLocation().data());
    const double L = (p2 - p1).norm();
    const double x = xi * L;
    const Eigen::Matrix3d axes = localAxes(start.getLocation(), end.getLocation(), beam.orientation);

    // Nodal values in local axes.
    const Eigen::Vector3d u1 = axes * Eigen::Vector3d(start.getDisplacement().data());
    const Eigen::Vector3d r1 = axes * Eigen::Vector3d(start.getRotation().data());
    const Eigen::Vector3d u2 = axes * Eigen::Vector3d(end.getDisplacement().data());
    const Eigen::Vector3d r2 = axes * Eigen::Vector3d(end.getRotation().data());

    const auto& material = materials[beam.materialID];
    const double E = material.getElasticityModulus();
    const double G = material.getShearModulus();
    const auto& s = beam.section;
    const bool timoshenko = beam.formulation == Formulation::Timoshenko;
    const double phiY = timoshenko ? 12.0 * E * s.secondMomentZ / (G * s.shearAreaY * L * L) : 0.0;
    const double phiZ = timoshenko ? 12.0 * E * s.secondMomentY / (G * s.shearAreaZ * L * L) : 0.0;

    const double clamped = x * x * (L - x) * (L - x) / 24.0; // x^2 (L - x)^2 / 24
    const double shearShape = x * (L - x) / 2.0;
    const Eigen::Vector4d shapeY = bendingShape(xi, L, phiY);
    const Eigen::Vector4d shapeZ = bendingShape(xi, L, phiZ);

    Eigen::Vector3d local;
    local[0] = (1.0 - xi) * u1[0] + xi * u2[0] + localLoad[0] * shearShape / (E * s.area);
    local[1] = shapeY.dot(Eigen::Vector4d(u1[1], r1[2], u2[1], r2[2]))
      + localLoad[1] * (clamped / (E * s.secondMomentZ) + (timoshenko ? shearShape / (G * s.shearAreaY) : 0.0));
    // ry = -dw/dx: the rotation terms enter with a minus sign.
    local[2] = shapeZ.dot(Eigen::Vector4d(u1[2], -r1[1], u2[2], -r2[1]))
      + localLoad[2] * (clamped / (E * s.secondMomentY) + (timoshenko ? shearShape / (G * s.shearAreaZ) : 0.0));

    SectionState state;
    state.position = x;
    const Eigen::Vector3d location = p1 + xi * (p2 - p1);
    const Eigen::Vector3d global = axes.transpose() * local;
    for (Eigen::Index i = 0; i < 3; ++i) {
      const auto k = static_cast<std::size_t>(i);
      state.location[k] = location[i];
      state.displacement[k] = global[i];
      state.localDisplacement[k] = local[i];
    }

    const auto& S0 = beam.sectionForces;
    state.forces = {
      S0[0] - localLoad[0] * x,
      S0[1] - localLoad[1] * x,
      S0[2] - localLoad[2] * x,
      S0[3],
      S0[4] + S0[2] * x - localLoad[2] * x * x / 2.0,
      S0[5] - S0[1] * x + localLoad[1] * x * x / 2.0,
    };
    return state;
  }

  std::vector<SectionState> sampleElement(
    const MeshData& solved,
    const std::size_t element,
    const std::size_t count,
    const Eigen::Vector3d& localLoad,
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    if (count < 2) throw std::invalid_argument("an element needs at least two sample points");
    std::vector<SectionState> states;
    states.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const double xi = static_cast<double>(i) / static_cast<double>(count - 1);
      states.push_back(sectionAt(solved, element, xi, localLoad, materials));
    }
    return states;
  }

  std::vector<std::vector<SectionState>> sampleAllElements(
    const MeshData& solved,
    const std::size_t countPerElement,
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    // Checked here: an exception must not leave the parallel loop below.
    if (!solved.hasResults) throw std::invalid_argument("the beam model has no results");
    if (countPerElement < 2) throw std::invalid_argument("an element needs at least two sample points");
    const auto loads = elementLocalLoads(solved.nodes, solved.elements, solved.distributedLoads, solved.gravity, materials);
    std::vector<std::vector<SectionState>> all(solved.elements.size());
    const auto elementCount = static_cast<long long>(solved.elements.size());
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < elementCount; ++index) {
      all[index] = sampleElement(solved, static_cast<std::size_t>(index), countPerElement, loads[index], materials);
    }
    return all;
  }

} // namespace FEM::BEAM end
