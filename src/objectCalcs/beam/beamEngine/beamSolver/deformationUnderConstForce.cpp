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

#include "deformationUnderConstForce.hpp"
#include <log/anaf_info.hpp>
#include <solvers/solverPortfolio.hpp>

#include <Eigen/Geometry> // cross()
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <omp.h>
#include <stdexcept>

namespace FEM::BEAM {

  namespace {
    constexpr std::uint32_t dofsPerNode = 6;
    using Matrix6 = Eigen::Matrix<double, 6, 6>;

    // T = blockdiag(R, R, R, R): global element DOFs -> local element DOFs.
    Matrix12 elementTransformation(const Eigen::Matrix3d& axes) {
      Matrix12 transformation = Matrix12::Zero();
      for (Eigen::Index block = 0; block < 4; ++block) transformation.block<3, 3>(3 * block, 3 * block) = axes;
      return transformation;
    }

    // Writes a 4x4 bending block at the DOF indices of one bending plane.
    void placeBending(Matrix12& k, const std::array<Eigen::Index, 4>& dofs, const Eigen::Matrix4d& block) {
      for (Eigen::Index row = 0; row < 4; ++row) {
        for (Eigen::Index col = 0; col < 4; ++col) k(dofs[row], dofs[col]) = block(row, col);
      }
    }

    // Columns are the node's DOF slots: slot k < 3 = allowed motion direction k, slot 3 + k =
    // allowed rotation axis k, as 6-component global vectors. Unused slots are zero columns.
    Matrix6 nodeBasis(const Node& node) {
      Matrix6 basis = Matrix6::Zero();
      const auto& motion = node.getAllowedMotionDirections();
      const auto& rotation = node.getAllowedRotationAxes();
      for (std::size_t k = 0; k < motion.size(); ++k) {
        for (Eigen::Index axis = 0; axis < 3; ++axis) basis(axis, static_cast<Eigen::Index>(k)) = motion[k][axis];
      }
      for (std::size_t k = 0; k < rotation.size(); ++k) {
        for (Eigen::Index axis = 0; axis < 3; ++axis) basis(3 + axis, 3 + static_cast<Eigen::Index>(k)) = rotation[k][axis];
      }
      return basis;
    }

    Vector12 elementDisplacements(const Node& start, const Node& end) {
      Vector12 u;
      for (Eigen::Index axis = 0; axis < 3; ++axis) {
        u[axis] = start.getDisplacement()[axis];
        u[3 + axis] = start.getRotation()[axis];
        u[6 + axis] = end.getDisplacement()[axis];
        u[9 + axis] = end.getRotation()[axis];
      }
      return u;
    }
  } // namespace end

  Eigen::Matrix3d localAxes(
    const std::array<double, 3>& start,
    const std::array<double, 3>& end,
    const std::array<double, 3>& orientation
  ) {
    Eigen::Vector3d x(end[0] - start[0], end[1] - start[1], end[2] - start[2]);
    const double length = x.norm();
    if (!(length > 0.0)) throw std::invalid_argument("element length must be greater than zero");
    x /= length;

    Eigen::Vector3d v(orientation[0], orientation[1], orientation[2]);
    if (v.norm() == 0.0) {
      // Default: local y as close to global +Y (up) as possible; vertical members use +X.
      v = Eigen::Vector3d::UnitY();
      if (x.cross(v).norm() < 1e-6) v = Eigen::Vector3d::UnitX();
    }
    Eigen::Vector3d z = x.cross(v);
    if (z.norm() <= 1e-9 * v.norm()) {
      throw std::invalid_argument("the orientation vector is parallel to the element axis");
    }
    z.normalize();
    const Eigen::Vector3d y = z.cross(x);

    Eigen::Matrix3d axes;
    axes.row(0) = x;
    axes.row(1) = y;
    axes.row(2) = z;
    return axes;
  }

  Matrix12 localStiffness(
    const double E,
    const double G,
    const SectionProperties& section,
    const Formulation formulation,
    const double length
  ) {
    const double L = length;
    const double L2 = L * L;
    const double L3 = L2 * L;
    const bool timoshenko = formulation == Formulation::Timoshenko;
    // phi = ratio of shear to bending flexibility; the x-y plane (v, rz) bends about z and
    // carries Vy, the x-z plane (w, ry) bends about y and carries Vz.
    const double phiY = timoshenko ? 12.0 * E * section.secondMomentZ / (G * section.shearAreaY * L2) : 0.0;
    const double phiZ = timoshenko ? 12.0 * E * section.secondMomentY / (G * section.shearAreaZ * L2) : 0.0;

    Matrix12 k = Matrix12::Zero();
    const double axial = E * section.area / L;
    k(0, 0) = axial;  k(0, 6) = -axial;
    k(6, 0) = -axial; k(6, 6) = axial;
    const double torsion = G * section.torsionConstant / L;
    k(3, 3) = torsion;  k(3, 9) = -torsion;
    k(9, 3) = -torsion; k(9, 9) = torsion;

    // x-y plane: {v1, rz1, v2, rz2}; rz = +dv/dx.
    const double a = E * section.secondMomentZ / ((1.0 + phiY) * L3);
    Eigen::Matrix4d bendingY;
    bendingY << 12.0,      6.0 * L,                -12.0,     6.0 * L,
                6.0 * L,   (4.0 + phiY) * L2,      -6.0 * L,  (2.0 - phiY) * L2,
                -12.0,     -6.0 * L,               12.0,      -6.0 * L,
                6.0 * L,   (2.0 - phiY) * L2,      -6.0 * L,  (4.0 + phiY) * L2;
    placeBending(k, {1, 5, 7, 11}, a * bendingY);

    // x-z plane: {w1, ry1, w2, ry2}; ry = -dw/dx, so the coupling terms change sign.
    const double b = E * section.secondMomentY / ((1.0 + phiZ) * L3);
    Eigen::Matrix4d bendingZ;
    bendingZ << 12.0,      -6.0 * L,               -12.0,     -6.0 * L,
                -6.0 * L,  (4.0 + phiZ) * L2,      6.0 * L,   (2.0 - phiZ) * L2,
                -12.0,     6.0 * L,                12.0,      6.0 * L,
                -6.0 * L,  (2.0 - phiZ) * L2,      6.0 * L,   (4.0 + phiZ) * L2;
    placeBending(k, {2, 4, 8, 10}, b * bendingZ);
    return k;
  }

  Vector12 equivalentNodalLoads(const Eigen::Vector3d& localLoad, const double length) {
    const double half = 0.5 * length;
    const double moment = length * length / 12.0;
    Vector12 f = Vector12::Zero();
    for (Eigen::Index axis = 0; axis < 3; ++axis) {
      f[axis] = localLoad[axis] * half;
      f[6 + axis] = localLoad[axis] * half;
    }
    f[5] = localLoad[1] * moment;   // rz1
    f[11] = -localLoad[1] * moment; // rz2
    f[4] = -localLoad[2] * moment;  // ry1 (ry = -dw/dx)
    f[10] = localLoad[2] * moment;  // ry2
    return f;
  }

  std::vector<SectionProperties> elementSectionProperties(
    const std::span<const BeamElement> elements,
    const std::span<const BeamSection> sections,
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    std::vector<SectionProperties> properties;
    properties.reserve(elements.size());
    for (const auto& element : elements) {
      properties.push_back(computeProperties(sections[element.sectionID].getShape(), materials[element.materialID].getPoisson()));
    }
    return properties;
  }

  std::vector<Eigen::Vector3d> elementLocalLoads(
    const std::span<const Node> nodes,
    const std::span<const BeamElement> elements,
    const std::span<const SectionProperties> properties,
    const std::span<const DistributedLoad> distributedLoads,
    const std::array<double, 3>& gravity,
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    std::vector<Eigen::Matrix3d> axes(elements.size());
    std::vector<Eigen::Vector3d> loads(elements.size(), Eigen::Vector3d::Zero());
    const Eigen::Vector3d g(gravity[0], gravity[1], gravity[2]);
    for (std::size_t index = 0; index < elements.size(); ++index) {
      const auto& element = elements[index];
      axes[index] = localAxes(nodes[element.node1].getLocation(), nodes[element.node2].getLocation(), element.orientation);
      const double massPerLength = materials[element.materialID].getDensity() * properties[index].area;
      if (massPerLength != 0.0) loads[index] = axes[index] * (massPerLength * g);
    }
    // Several loads may act on one element, so they are added serially.
    for (const auto& load : distributedLoads) {
      const Eigen::Vector3d value(load.value[0], load.value[1], load.value[2]);
      loads[load.element] += load.frame == LoadFrame::Local ? value : Eigen::Vector3d(axes[load.element] * value);
    }
    return loads;
  }

  std::expected<void, std::string> Beam_3D_Container::buildElements(
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    const auto elementCount = static_cast<long long>(m_elements.size());
    m_frames.assign(m_elements.size(), ElementFrame{});
    std::vector<std::string> errors(m_elements.size());

    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < elementCount; ++index) {
      const auto& element = m_elements[index];
      auto& frame = m_frames[index];
      const auto& start = m_nodes[element.node1].getLocation();
      const auto& end = m_nodes[element.node2].getLocation();
      try {
        frame.axes = localAxes(start, end, element.orientation);
      } catch (const std::exception& exception) {
        errors[index] = exception.what();
        continue;
      }
      const double dx = end[0] - start[0];
      const double dy = end[1] - start[1];
      const double dz = end[2] - start[2];
      frame.length = std::sqrt(dx * dx + dy * dy + dz * dz);
      const auto& material = materials[element.materialID];
      frame.localStiffness = localStiffness(
        material.getElasticityModulus(), material.getShearModulus(), m_properties[index], element.formulation, frame.length
      );
      const Matrix12 transformation = elementTransformation(frame.axes);
      frame.globalStiffness = transformation.transpose() * frame.localStiffness * transformation;
      frame.fixedEndLoads = Vector12::Zero();
    }

    for (std::size_t index = 0; index < errors.size(); ++index) {
      if (!errors[index].empty()) {
        const auto& element = m_elements[index];
        return std::unexpected(std::format("element {} (nodes {} - {}): {}", index, element.node1, element.node2, errors[index]));
      }
    }
    anaf::LOG::info("Beam elements built: {} (12x12 local stiffness each)", m_elements.size());
    return {};
  }

  void Beam_3D_Container::applyLoads(
    const std::span<const NodalLoad> nodalLoads,
    const std::span<const DistributedLoad> distributedLoads,
    const std::array<double, 3>& gravity,
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    m_force.assign(m_nodes.size() * dofsPerNode, 0.0);
    for (const auto& load : nodalLoads) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        m_force[dofsPerNode * load.node + axis] += load.force[axis];
        m_force[dofsPerNode * load.node + 3 + axis] += load.moment[axis];
      }
    }

    const auto localLoads = elementLocalLoads(m_nodes, m_elements, m_properties, distributedLoads, gravity, materials);
    const auto elementCount = static_cast<long long>(m_elements.size());
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < elementCount; ++index) {
      const auto& element = m_elements[index];
      auto& frame = m_frames[index];
      frame.fixedEndLoads = equivalentNodalLoads(localLoads[index], frame.length);
      if (frame.fixedEndLoads.isZero(0.0)) continue;

      const Vector12 global = elementTransformation(frame.axes).transpose() * frame.fixedEndLoads;
      const std::array<std::size_t, 2> base{dofsPerNode * element.node1, dofsPerNode * element.node2};
      for (std::size_t end = 0; end < 2; ++end) {
        for (std::size_t dof = 0; dof < dofsPerNode; ++dof) {
          #pragma omp atomic
          m_force[base[end] + dof] += global[static_cast<Eigen::Index>(dofsPerNode * end + dof)];
        }
      }
    }
    anaf::LOG::info("Applied {} nodal loads and {} distributed loads (plus self weight)", nodalLoads.size(), distributedLoads.size());
  }

  bool Beam_3D_Container::calculateDisplacements(const std::stop_token stopToken) {
    const auto nodeCount = static_cast<std::uint32_t>(m_nodes.size());

    // Reduced DOF of slot s of node n at dofsPerNode * n + s, -1 when unused (Block-CG node blocks).
    std::vector<std::int32_t> nodeDofSlots(static_cast<std::size_t>(nodeCount) * dofsPerNode, -1);
    std::vector<Matrix6> bases(nodeCount);
    std::vector<std::uint32_t> usedSlots(nodeCount, 0);
    std::int32_t activeDofCount = 0;
    for (std::uint32_t node = 0; node < nodeCount; ++node) {
      bases[node] = nodeBasis(m_nodes[node]);
      const std::size_t motion = m_nodes[node].getAllowedMotionDirections().size();
      const std::size_t rotation = m_nodes[node].getAllowedRotationAxes().size();
      for (std::size_t k = 0; k < motion; ++k) nodeDofSlots[dofsPerNode * node + k] = activeDofCount++;
      for (std::size_t k = 0; k < rotation; ++k) nodeDofSlots[dofsPerNode * node + 3 + k] = activeDofCount++;
      usedSlots[node] = static_cast<std::uint32_t>(motion + rotation);
    }

    // Element e contributes B^T K_e B, B = blockdiag(B_n1, B_n2); with n used slots it has
    // n (n + 1) / 2 upper-triangle entries (the reduced DOFs of two different nodes are distinct).
    const auto elementCount = static_cast<long long>(m_elements.size());
    std::vector<std::size_t> offsets(m_elements.size() + 1, 0);
    for (std::size_t index = 0; index < m_elements.size(); ++index) {
      const std::size_t used = usedSlots[m_elements[index].node1] + usedSlots[m_elements[index].node2];
      offsets[index + 1] = offsets[index] + used * (used + 1) / 2;
    }
    std::vector<Eigen::Triplet<double>> reducedTriplets(offsets.back());

    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < elementCount; ++index) {
      const auto& element = m_elements[index];
      Matrix12 basis = Matrix12::Zero();
      basis.block<6, 6>(0, 0) = bases[element.node1];
      basis.block<6, 6>(6, 6) = bases[element.node2];
      const Matrix12 reduced = basis.transpose() * m_frames[index].globalStiffness * basis;

      std::array<std::int32_t, 12> map{};
      for (std::size_t slot = 0; slot < dofsPerNode; ++slot) {
        map[slot] = nodeDofSlots[dofsPerNode * element.node1 + slot];
        map[dofsPerNode + slot] = nodeDofSlots[dofsPerNode * element.node2 + slot];
      }
      std::size_t output = offsets[index];
      for (Eigen::Index row = 0; row < 12; ++row) {
        if (map[row] < 0) continue;
        for (Eigen::Index col = 0; col < 12; ++col) {
          if (map[col] < 0 || map[row] > map[col]) continue;
          reducedTriplets[output++] = {map[row], map[col], reduced(row, col)};
        }
      }
    }

    Eigen::SparseMatrix<double> reducedStiffnessMatrix(activeDofCount, activeDofCount);
    reducedStiffnessMatrix.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
    reducedStiffnessMatrix.makeCompressed();
    reducedTriplets.clear();
    reducedTriplets.shrink_to_fit();
    anaf::LOG::info("Reduced beam stiffness: {} DOFs ({} nodes x 6, minus supports), {} stored upper entries",
                    activeDofCount, nodeCount, reducedStiffnessMatrix.nonZeros());

    Eigen::VectorXd reducedForce = Eigen::VectorXd::Zero(activeDofCount);
    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < nodeCount; ++node) {
      const Eigen::Map<const Eigen::Matrix<double, 6, 1>> force(m_force.data() + dofsPerNode * node);
      const Eigen::Matrix<double, 6, 1> projected = bases[node].transpose() * force;
      for (std::size_t slot = 0; slot < dofsPerNode; ++slot) {
        const auto reduced = nodeDofSlots[dofsPerNode * node + slot];
        if (reduced >= 0) reducedForce[reduced] = projected[static_cast<Eigen::Index>(slot)];
      }
    }

    Eigen::VectorXd reducedDisplacements(activeDofCount);
    const auto solverResult = FEM::SOLVER::solveSelected(
      reducedStiffnessMatrix,
      reducedForce,
      nodeCount,
      dofsPerNode,
      nodeDofSlots,
      stopToken,
      reducedDisplacements
    );

    if (!solverResult.converged) {
      anaf::LOG::error(
        "Stiffness solve failed using {}: {}",
        FEM::SOLVER::toString(solverResult.type), solverResult.message
      );
      for (auto& node : m_nodes) {
        node.setDisplacement({0.0, 0.0, 0.0});
        node.setRotation({0.0, 0.0, 0.0});
      }
      return false;
    }

    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < nodeCount; ++node) {
      Eigen::Matrix<double, 6, 1> q = Eigen::Matrix<double, 6, 1>::Zero();
      for (std::size_t slot = 0; slot < dofsPerNode; ++slot) {
        const auto reduced = nodeDofSlots[dofsPerNode * node + slot];
        if (reduced >= 0) q[static_cast<Eigen::Index>(slot)] = reducedDisplacements[reduced];
      }
      const Eigen::Matrix<double, 6, 1> u = bases[node] * q;
      m_nodes[node].setDisplacement({u[0], u[1], u[2]});
      m_nodes[node].setRotation({u[3], u[4], u[5]});
    }
    return true;
  }

  void Beam_3D_Container::calculateSectionForces() {
    const auto elementCount = static_cast<long long>(m_elements.size());
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < elementCount; ++index) {
      auto& element = m_elements[index];
      const auto& frame = m_frames[index];
      const Vector12 local = elementTransformation(frame.axes)
        * elementDisplacements(m_nodes[element.node1], m_nodes[element.node2]);
      // End forces the nodes apply to the element: k u = p + f0  ->  p = k u - f0.
      const Vector12 endForces = frame.localStiffness * local - frame.fixedEndLoads;
      for (Eigen::Index i = 0; i < 6; ++i) {
        element.sectionForces[static_cast<std::size_t>(i)] = -endForces[i];
        element.sectionForces[static_cast<std::size_t>(6 + i)] = endForces[6 + i];
      }
    }
  }

  void Beam_3D_Container::runValidator() {
    double internalEnergy = 0.0;
    const auto elementCount = static_cast<long long>(m_elements.size());
    #pragma omp parallel for schedule(static) reduction(+:internalEnergy)
    for (long long index = 0; index < elementCount; ++index) {
      const auto& element = m_elements[index];
      const Vector12 u = elementDisplacements(m_nodes[element.node1], m_nodes[element.node2]);
      internalEnergy += 0.5 * u.dot(m_frames[index].globalStiffness * u);
    }
    m_elasticDeformationEnergy_internal = internalEnergy;

    double externalWork = 0.0;
    const auto nodeCount = static_cast<long long>(m_nodes.size());
    #pragma omp parallel for schedule(static) reduction(+:externalWork)
    for (long long node = 0; node < nodeCount; ++node) {
      const std::size_t base = dofsPerNode * static_cast<std::size_t>(node);
      const auto& displacement = m_nodes[node].getDisplacement();
      const auto& rotation = m_nodes[node].getRotation();
      for (std::size_t axis = 0; axis < 3; ++axis) {
        externalWork += m_force[base + axis] * displacement[axis] + m_force[base + 3 + axis] * rotation[axis];
      }
    }
    m_workDone_external = externalWork;

    const double externalEnergy = 0.5 * externalWork;
    m_energyDiff = std::abs(internalEnergy - externalEnergy);
    const double scale = std::max({std::abs(internalEnergy), std::abs(externalEnergy), 1.0});
    m_energyRelativeDiff = m_energyDiff / scale;
    m_isCalculationValid = m_energyDiff <= 1e-12 || m_energyRelativeDiff <= 1e-7;
  }

} // namespace FEM::BEAM end
