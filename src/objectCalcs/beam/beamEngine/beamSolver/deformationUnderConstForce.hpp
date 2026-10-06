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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace FEM::BEAM {

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
  Eigen::Matrix<double, 12, 12> localStiffness(double E, double G, const SectionProperties& section, Formulation formulation, double length);

  // Work equivalent nodal loads of a uniform load q (local axes, N/m) over the element. The
  // same for both formulations: the Timoshenko shape functions integrate to wL/2 and wL^2/12.
  Eigen::Matrix<double, 12, 1> equivalentNodalLoads(const Eigen::Vector3d& localLoad, double length);

  // Static condensation of end releases (RELEASE bits). With the end forces p of the nodes,
  // k u = p + f0 and p_r = 0 on the released DOFs r give
  //   u_r = k_rr^-1 (f0_r - k_rc u_c),  k* = k_cc - k_cr k_rr^-1 k_rc,  f0* = f0_c - k_cr k_rr^-1 f0_r
  // for the kept DOFs c. k and f0 become k* and f0*, with zero rows / columns and entries at the
  // released DOFs. Returns false (k and f0 unchanged) when k_rr is singular: the released DOFs
  // alone let the element move (e.g. N or T released at both ends, a pin at both ends plus a
  // shear release). releases = 0 changes nothing.
  bool condenseReleases(std::uint16_t releases, Eigen::Matrix<double, 12, 12>& k, Eigen::Matrix<double, 12, 1>& f0);

  // Fills the released entries of u (local end displacements, kept entries given) with the
  // element end's own motion u_r above. k and f0 are the uncondensed element values; k_rr must
  // be regular (condenseReleases() returned true).
  void recoverReleasedDisplacements(std::uint16_t releases, const Eigen::Matrix<double, 12, 12>& k, const Eigen::Matrix<double, 12, 1>& f0, Eigen::Matrix<double, 12, 1>& u);

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
      // Zero-initialized: m_frames.assign() copies a default frame, and GCC 14 warns
      // (-Wmaybe-uninitialized) about copying uninitialized Eigen storage.
      Eigen::Matrix3d axes{Eigen::Matrix3d::Zero()};       // rows: local x, y, z
      Eigen::Matrix<double, 12, 12> localStiffness{Eigen::Matrix<double, 12, 12>::Zero()};           // k
      Eigen::Matrix<double, 12, 12> globalStiffness{Eigen::Matrix<double, 12, 12>::Zero()};          // T^T k T
      Eigen::Matrix<double, 12, 1> fixedEndLoads{Eigen::Matrix<double, 12, 1>::Zero()};            // local equivalent nodal loads of the element's total uniform load
    };

    // DOF slots of one node: columns 0..motion-1 and 3..3+rotation-1 of basis (6-component
    // global vectors), the node's allowed directions minus those without any stiffness.
    struct NodeDofs {
      Eigen::Matrix<double, 6, 6> basis;
      std::uint32_t motion{};
      std::uint32_t rotation{};
    };

    std::span<Node> m_nodes;
    std::span<BeamElement> m_elements;
    std::span<const SectionProperties> m_properties; // one per element
    std::vector<ElementFrame> m_frames;
    std::vector<double> m_force; // 6 per node, global: fx fy fz mx my mz
    std::vector<NodeDofs> m_nodeDofs;
    std::size_t m_heldFreeDirections{};

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

    // Local axes and stiffness of every element (end releases condensed). Fails on a zero
    // length element, an orientation vector parallel to the element axis or releases that make
    // the element a mechanism.
    std::expected<void, std::string> buildElements(std::span<const anaf::MATERIAL::Material> materials);

    // Nodal loads, distributed loads and self weight (density * area * gravity) into the
    // global load vector. Indices must be valid (checked by the caller).
    void applyLoads(
      std::span<const NodalLoad> nodalLoads,
      std::span<const DistributedLoad> distributedLoads,
      const std::array<double, 3>& gravity,
      std::span<const anaf::MATERIAL::Material> materials
    );

    // DOF slots of every node (after buildElements() and applyLoads()). A node direction in
    // which every element end at the node is released (e.g. all beams meeting there are hinged
    // about the same axis) has no stiffness; it is held at zero, which changes nothing, unless a
    // load acts along it: then the model is a mechanism and this fails.
    std::expected<void, std::string> buildNodeDofs();
    inline std::size_t getHeldFreeDirections() const {return m_heldFreeDirections;}

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
