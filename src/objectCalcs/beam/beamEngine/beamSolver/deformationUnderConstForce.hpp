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

#include <beam/beamProperties/meshData.hpp>
#include <beam/beamSection/beamSection.hpp>
#include <material/properties.hpp>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <array>
#include <expected>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace FEM::BEAM {

  using Matrix12 = Eigen::Matrix<double, 12, 12>;
  using Vector12 = Eigen::Matrix<double, 12, 1>;

  // Rows are the local x, y, z axes in global components (see BeamElement for the rule).
  // Throws std::invalid_argument for a zero length element or a v parallel to its axis.
  Eigen::Matrix3d localAxes(
    const std::array<double, 3>& start,
    const std::array<double, 3>& end,
    const std::array<double, 3>& orientation
  );

  // 12x12 local stiffness, DOF order per node {ux, uy, uz, rx, ry, rz} (Przemieniecki).
  // Timoshenko uses the interdependent interpolation element: phi = 12 E I / (G As L^2) per
  // bending plane; phi = 0 gives Euler-Bernoulli exactly, and there is no shear locking.
  Matrix12 localStiffness(double E, double G, const SectionProperties& section, Formulation formulation, double length);

  // Work equivalent nodal loads of a uniform load q (local axes, N/m) over the element. The
  // same for both formulations: the Timoshenko shape functions integrate to wL/2 and wL^2/12.
  Vector12 equivalentNodalLoads(const Eigen::Vector3d& localLoad, double length);

  // Section properties of every element: its section's shape with its material's Poisson's
  // ratio (the shear coefficients depend on it). Indices must be valid (checked by solveStatic).
  std::vector<SectionProperties> elementSectionProperties(
    std::span<const BeamElement> elements,
    std::span<const BeamSection> sections,
    std::span<const anaf::MATERIAL::Material> materials
  );

  // Total uniform load on every element in its local axes (N/m): the distributed loads on it
  // plus self weight (density * area * gravity). properties[e] belongs to elements[e]. Throws
  // std::invalid_argument for an element whose local axes cannot be built. Indices must be
  // valid (checked by solveStatic).
  std::vector<Eigen::Vector3d> elementLocalLoads(
    std::span<const Node> nodes,
    std::span<const BeamElement> elements,
    std::span<const SectionProperties> properties,
    std::span<const DistributedLoad> distributedLoads,
    const std::array<double, 3>& gravity,
    std::span<const anaf::MATERIAL::Material> materials
  );

  // Static solve of a beam model. Every node has 6 DOFs; each node's allowed motion and
  // rotation bases give u = T q, and the reduced system (T^T K T) q = T^T f is solved by the
  // FEM::SOLVER portfolio with 6 DOF slots per node.
  class Beam_3D_Container {
  private:
    struct ElementFrame {
      double length{};
      Eigen::Matrix3d axes;      // rows: local x, y, z
      Matrix12 localStiffness;   // k
      Matrix12 globalStiffness;  // T^T k T
      Vector12 fixedEndLoads;    // local equivalent nodal loads of the element's total uniform load
    };

    std::span<Node> m_nodes;
    std::span<BeamElement> m_elements;
    std::span<const SectionProperties> m_properties; // one per element
    std::vector<ElementFrame> m_frames;
    std::vector<double> m_force; // 6 per node, global: fx fy fz mx my mz

    bool m_isCalculationValid{false};
    double m_energyDiff{};
    double m_energyRelativeDiff{};
    double m_workDone_external{};
    double m_elasticDeformationEnergy_internal{};

  public:
    void set(std::span<Node> nodes, std::span<BeamElement> elements, std::span<const SectionProperties> properties) {
      m_nodes = nodes;
      m_elements = elements;
      m_properties = properties;
    }

    // Local axes and stiffness of every element. Fails on a zero length element or an
    // orientation vector parallel to the element axis.
    std::expected<void, std::string> buildElements(std::span<const anaf::MATERIAL::Material> materials);

    // Nodal loads, distributed loads and self weight (density * area * gravity) into the
    // global load vector. Indices must be valid (checked by the caller).
    void applyLoads(
      std::span<const NodalLoad> nodalLoads,
      std::span<const DistributedLoad> distributedLoads,
      const std::array<double, 3>& gravity,
      std::span<const anaf::MATERIAL::Material> materials
    );

    // False when the solve failed or was stopped; displacements and rotations are then zero.
    bool calculateDisplacements(std::stop_token stopToken = {});
    void calculateSectionForces();
    void runValidator();

    inline bool getIsCalculationValid() const {return m_isCalculationValid;}
    inline double getEnergyDiff() const {return m_energyDiff;}
    inline double getEnergyRelativeDiff() const {return m_energyRelativeDiff;}
    inline double getWorkDone_External() const {return m_workDone_external;}
    inline double getElasticDeformationEnergy_Internal() const {return m_elasticDeformationEnergy_internal;}
  };

} // namespace FEM::BEAM end
