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

// Unit tests of the 3D beam solver (FEM::BEAM) against closed-form results (Logan, 5th ed.,
// ch. 4-5; Przemieniecki, Theory of Matrix Structural Analysis, for Timoshenko). Linked
// against anaf_core only. The two-node elements are exact at the nodes for nodal loads and
// uniform loads (consistent load vector), so one or two elements must hit the formulas.

#include "testSupport.hpp"
#include <beam/beamEngine/beamDiagrams.hpp>
#include <beam/beamEngine/beamSolver.hpp>
#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp>

#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

using FEM::BEAM::BeamElement;
using FEM::BEAM::Formulation;
using FEM::BEAM::MeshData;
using FEM::BEAM::Node;
using Vec = std::array<double, 3>;

namespace {
  constexpr double kE = 210e9;  // Pa
  constexpr double kG = 80e9;   // Pa
  constexpr double kL = 3.0;    // m
  constexpr double kDensity = 7850.0;
  // Iy != Iz and Asy != Asz, so a swapped axis shows up as a wrong number.
  constexpr FEM::BEAM::Section kSection{
    .area = 0.01, .secondMomentY = 2e-5, .secondMomentZ = 8e-5, .torsionConstant = 1e-5,
    .shearAreaY = 0.008, .shearAreaZ = 0.007
  };

  // Index 0: massless steel (load-only tests), index 1: steel with its density.
  const std::vector<anaf::MATERIAL::Material>& materials() {
    static const std::vector<anaf::MATERIAL::Material> list{
      anaf::MATERIAL::Material{{.name = "Massless", .elasticityModulus = kE, .shearModulus = kG, .bulkModulus = 175e9,
                                .yieldTensileStrength = 250e6, .ultimateTensileStrength = 400e6, .density = 0.0,
                                .poissonsRatio = 0.3, .ductility = 0.2}},
      anaf::MATERIAL::Material{{.name = "Steel", .elasticityModulus = kE, .shearModulus = kG, .bulkModulus = 175e9,
                                .yieldTensileStrength = 250e6, .ultimateTensileStrength = 400e6, .density = kDensity,
                                .poissonsRatio = 0.3, .ductility = 0.2}},
    };
    return list;
  }

  bool near(const double actual, const double expected, const double relative, const double absolute = 0.0) {
    return std::abs(actual - expected) <= relative * std::abs(expected) + absolute;
  }

  bool nearVec(const Vec& actual, const Vec& expected, const double tolerance) {
    double scale = 0.0;
    for (const double v : expected) scale = std::max(scale, std::abs(v));
    for (std::size_t i = 0; i < 3; ++i) {
      if (std::abs(actual[i] - expected[i]) > tolerance * std::max(scale, 1e-300)) return false;
    }
    return true;
  }

  BeamElement beam(const std::uint32_t a, const std::uint32_t b, const Formulation formulation = Formulation::EulerBernoulli,
                   const Vec orientation = {}, const std::uint32_t material = 0) {
    BeamElement element;
    element.node1 = a;
    element.node2 = b;
    element.materialID = material;
    element.section = kSection;
    element.formulation = formulation;
    element.orientation = orientation;
    return element;
  }

  // Cantilever along +X: node 0 clamped at the origin, node 1 free at (L, 0, 0).
  MeshData cantilever(const Formulation formulation) {
    MeshData mesh;
    mesh.gravity = {0.0, 0.0, 0.0};
    mesh.nodes = {Node{0, 0.0, 0.0, 0.0}, Node{1, kL, 0.0, 0.0}};
    mesh.nodes[0].fixAll();
    mesh.elements = {beam(0, 1, formulation)};
    return mesh;
  }

  FEM::BEAM::StaticResult solve(const MeshData& mesh) {
    auto solved = FEM::BEAM::solveStatic(mesh, materials());
    if (!solved) {
      std::printf("      %s\n", solved.error().c_str());
      throw anaf::TESTING::RequireFailure{};
    }
    return std::move(*solved);
  }

  std::string errorOf(const MeshData& mesh) {
    const auto solved = FEM::BEAM::solveStatic(mesh, materials());
    return solved ? std::string{} : solved.error();
  }

  // A 3D frame with every feature: mixed formulations, nodal force and moment, global and
  // local distributed loads, self weight, an inclined roller and an inclined rotation
  // support. R turns the whole model (local loads stay as they are).
  MeshData frameModel(const Eigen::Matrix3d& R) {
    const auto turn = [&](const Vec& v) {
      const Eigen::Vector3d t = R * Eigen::Vector3d(v[0], v[1], v[2]);
      return Vec{t[0], t[1], t[2]};
    };
    MeshData mesh;
    mesh.gravity = turn({0.0, -9.80665, 0.0});
    const std::vector<Vec> points{{0, 0, 0}, {0, 3, 0}, {4, 3, 0}, {4, 3, 2.5}, {4, 0, 2.5}};
    for (std::uint32_t i = 0; i < points.size(); ++i) {
      const auto p = turn(points[i]);
      mesh.nodes.emplace_back(i, p[0], p[1], p[2]);
    }
    mesh.nodes[0].fixAll();
    // Roller at node 4: free along an inclined rail, rotation only about one skewed axis.
    mesh.nodes[4].setAllowedMotionDirections({turn({1.0, 0.0, 1.0}), turn({0.0, 0.3, 1.0})});
    mesh.nodes[4].setAllowedRotationAxes({turn({0.0, 1.0, 1.0})});
    mesh.elements = {
      beam(0, 1, Formulation::Timoshenko, turn({1, 0, 0}), 1),
      beam(1, 2, Formulation::EulerBernoulli, turn({0, 1, 0}), 1),
      beam(2, 3, Formulation::Timoshenko, turn({1, 1, 0}), 0),
      beam(3, 4, Formulation::EulerBernoulli, turn({0, 0, 1}), 1),
    };
    mesh.nodalLoads = {{2, turn({1e3, -2e3, 5e2}), turn({3e2, 0.0, -1e3})}, {3, turn({0.0, 0.0, 4e3}), {}}};
    mesh.distributedLoads = {
      {1, turn({0.0, -1.5e3, 4e2}), FEM::BEAM::LoadFrame::Global},
      {2, {2e2, -8e2, 3e2}, FEM::BEAM::LoadFrame::Local}, // local loads do not turn
    };
    return mesh;
  }

  bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

  template <typename Callable>
  bool throws(Callable&& callable) {
    try {
      callable();
    } catch (const std::exception&) {
      return true;
    }
    return false;
  }
} // namespace end

// ---- element building blocks ------------------------------------------------------------------

TEST(localAxesFollowTheOrientationRule) {
  using FEM::BEAM::localAxes;
  // Horizontal member, default v = +Y: local y = +Y, z = +Z.
  auto axes = localAxes({0, 0, 0}, {2, 0, 0}, {});
  CHECK(axes.row(1).isApprox(Eigen::RowVector3d(0, 1, 0)) && axes.row(2).isApprox(Eigen::RowVector3d(0, 0, 1)));
  // Vertical member (along Y), default v = +X: z = Y x X = -Z, y = z x x = +X.
  axes = localAxes({0, 0, 0}, {0, 2, 0}, {});
  CHECK(axes.row(1).isApprox(Eigen::RowVector3d(1, 0, 0)) && axes.row(2).isApprox(Eigen::RowVector3d(0, 0, -1)));
  // Explicit v not perpendicular to the axis: only its part normal to x counts.
  axes = localAxes({0, 0, 0}, {1, 0, 0}, {1, 0, 1});
  CHECK(axes.row(1).isApprox(Eigen::RowVector3d(0, 0, 1)) && axes.row(2).isApprox(Eigen::RowVector3d(0, -1, 0)));
  CHECK((axes * axes.transpose()).isIdentity(1e-14) && axes.determinant() > 0.0);
  CHECK(throws([] { (void)localAxes({0, 0, 0}, {1, 1, 0}, {2, 2, 0}); })); // v parallel to the axis
  CHECK(throws([] { (void)localAxes({1, 1, 1}, {1, 1, 1}, {}); }));       // zero length
}

TEST(stiffnessIsSymmetricAndTimoshenkoTendsToEulerBernoulli) {
  const auto eb = FEM::BEAM::localStiffness(kE, kG, kSection, Formulation::EulerBernoulli, kL);
  const auto ti = FEM::BEAM::localStiffness(kE, kG, kSection, Formulation::Timoshenko, kL);
  CHECK(eb.isApprox(eb.transpose()) && ti.isApprox(ti.transpose()));
  // Six rigid body modes: rank 6.
  Eigen::SelfAdjointEigenSolver<FEM::BEAM::Matrix12> eigen(ti);
  int zero = 0;
  for (Eigen::Index i = 0; i < 12; ++i) zero += std::abs(eigen.eigenvalues()[i]) < 1e-6 * eigen.eigenvalues().maxCoeff() ? 1 : 0;
  CHECK(zero == 6 && eigen.eigenvalues().minCoeff() > -1e-6 * eigen.eigenvalues().maxCoeff());
  // Huge shear areas: phi -> 0.
  auto stiff = kSection;
  stiff.shearAreaY = stiff.shearAreaZ = 1e9;
  const auto limit = FEM::BEAM::localStiffness(kE, kG, stiff, Formulation::Timoshenko, kL);
  CHECK((limit - eb).norm() <= 1e-7 * eb.norm());
  // Euler-Bernoulli ignores the shear areas.
  auto noShear = kSection;
  noShear.shearAreaY = noShear.shearAreaZ = 0.0;
  CHECK(FEM::BEAM::localStiffness(kE, kG, noShear, Formulation::EulerBernoulli, kL) == eb);
}

// ---- closed-form solutions --------------------------------------------------------------------

TEST(cantileverTipLoadsMatchTheHandSolution) {
  // Tip force (Px, Py, Pz) and torque T; superposition of the classic cantilever results.
  const double Px = 5e3, Py = -12e3, Pz = 7e3, T = 2e3;
  for (const auto formulation : {Formulation::EulerBernoulli, Formulation::Timoshenko}) {
    auto mesh = cantilever(formulation);
    mesh.nodalLoads = {{1, {Px, Py, Pz}, {T, 0.0, 0.0}}};
    const auto solved = solve(mesh);
    const bool timoshenko = formulation == Formulation::Timoshenko;
    const double shearY = timoshenko ? Py * kL / (kG * kSection.shearAreaY) : 0.0;
    const double shearZ = timoshenko ? Pz * kL / (kG * kSection.shearAreaZ) : 0.0;
    const double L3 = kL * kL * kL;
    const auto& u = solved.mesh->nodes[1].getDisplacement();
    const auto& r = solved.mesh->nodes[1].getRotation();
    CHECK(near(u[0], Px * kL / (kE * kSection.area), 1e-10));
    CHECK(near(u[1], Py * L3 / (3.0 * kE * kSection.secondMomentZ) + shearY, 1e-10));
    CHECK(near(u[2], Pz * L3 / (3.0 * kE * kSection.secondMomentY) + shearZ, 1e-10));
    CHECK(near(r[0], T * kL / (kG * kSection.torsionConstant), 1e-10));
    CHECK(near(r[1], -Pz * kL * kL / (2.0 * kE * kSection.secondMomentY), 1e-10)); // ry = -dw/dx
    CHECK(near(r[2], Py * kL * kL / (2.0 * kE * kSection.secondMomentZ), 1e-10));
    CHECK(solved.mesh->nodes[0].getDisplacement() == (Vec{0, 0, 0}) && solved.mesh->nodes[0].getRotation() == (Vec{0, 0, 0}));

    // Section forces {N, Vy, Vz, T, My, Mz}: N, V, T constant; the moment grows to the clamp.
    const auto& s = solved.mesh->elements[0].sectionForces;
    for (const std::size_t end : {0U, 6U}) {
      CHECK(near(s[end + 0], Px, 1e-9));
      CHECK(near(s[end + 1], Py, 1e-9));
      CHECK(near(s[end + 2], Pz, 1e-9));
      CHECK(near(s[end + 3], T, 1e-9));
    }
    CHECK(near(s[4], -Pz * kL, 1e-9) && near(s[5], Py * kL, 1e-9));
    CHECK(near(s[10], 0.0, 0.0, 1e-6) && near(s[11], 0.0, 0.0, 1e-6));
    CHECK(solved.energyCheckPassed && solved.mesh->hasResults);
  }
}

TEST(clampedBeamUnderUniformLoad) {
  // Clamped-clamped span 2L, two elements, uniform q downward: w_mid = q (2L)^4 / (384 E I)
  // plus q (2L)^2 / (8 G As) for Timoshenko; M = q (2L)^2 / 12 at the clamps, / 24 at mid span.
  const double q = 4e3;
  const double span = 2.0 * kL;
  for (const auto formulation : {Formulation::EulerBernoulli, Formulation::Timoshenko}) {
    for (const auto frame : {FEM::BEAM::LoadFrame::Global, FEM::BEAM::LoadFrame::Local}) { // local y = global Y here
      MeshData mesh;
      mesh.gravity = {0.0, 0.0, 0.0};
      mesh.nodes = {Node{0, 0, 0, 0}, Node{1, kL, 0, 0}, Node{2, span, 0, 0}};
      mesh.nodes[0].fixAll();
      mesh.nodes[2].fixAll();
      mesh.elements = {beam(0, 1, formulation), beam(1, 2, formulation)};
      mesh.distributedLoads = {{0, {0.0, -q, 0.0}, frame}, {1, {0.0, -q, 0.0}, frame}};
      const auto solved = solve(mesh);
      const double shear = formulation == Formulation::Timoshenko ? q * span * span / (8.0 * kG * kSection.shearAreaY) : 0.0;
      const double expected = -(q * std::pow(span, 4) / (384.0 * kE * kSection.secondMomentZ) + shear);
      CHECK(near(solved.mesh->nodes[1].getDisplacement()[1], expected, 1e-10));
      CHECK(near(solved.mesh->nodes[1].getRotation()[2], 0.0, 0.0, 1e-12));

      const auto& left = solved.mesh->elements[0].sectionForces;
      const auto& right = solved.mesh->elements[1].sectionForces;
      CHECK(near(std::abs(left[5]), q * span * span / 12.0, 1e-9));   // clamp
      CHECK(near(std::abs(left[11]), q * span * span / 24.0, 1e-9));  // mid span
      CHECK(left[5] * left[11] < 0.0);                                // hogging at the clamp, sagging at mid span
      CHECK(near(std::abs(left[1]), q * span / 2.0, 1e-9));           // support shear
      CHECK(near(left[7], 0.0, 0.0, 1e-6));                           // zero shear at mid span
      for (std::size_t i = 0; i < 6; ++i) CHECK(near(left[6 + i], right[i], 1e-9, 1e-6)); // continuous at node 1
      CHECK(solved.energyCheckPassed);
    }
  }
}

TEST(cantileverUnderUniformLoadInBothPlanes) {
  // q = (0, qy, qz) per length, one element: tip v = qy L^4 / (8 E Iz), w = qz L^4 / (8 E Iy),
  // rz = qy L^3 / (6 E Iz), ry = -qz L^3 / (6 E Iy); Timoshenko adds q L^2 / (2 G As) to the
  // deflections (the section rotations do not change).
  const double qy = -3e3, qz = 2e3;
  for (const auto formulation : {Formulation::EulerBernoulli, Formulation::Timoshenko}) {
    auto mesh = cantilever(formulation);
    mesh.distributedLoads = {{0, {0.0, qy, qz}, FEM::BEAM::LoadFrame::Local}};
    const auto solved = solve(mesh);
    const bool timoshenko = formulation == Formulation::Timoshenko;
    const double L2 = kL * kL;
    const auto& u = solved.mesh->nodes[1].getDisplacement();
    const auto& r = solved.mesh->nodes[1].getRotation();
    CHECK(near(u[1], qy * L2 * L2 / (8.0 * kE * kSection.secondMomentZ) + (timoshenko ? qy * L2 / (2.0 * kG * kSection.shearAreaY) : 0.0), 1e-10));
    CHECK(near(u[2], qz * L2 * L2 / (8.0 * kE * kSection.secondMomentY) + (timoshenko ? qz * L2 / (2.0 * kG * kSection.shearAreaZ) : 0.0), 1e-10));
    CHECK(near(r[2], qy * L2 * kL / (6.0 * kE * kSection.secondMomentZ), 1e-10));
    CHECK(near(r[1], -qz * L2 * kL / (6.0 * kE * kSection.secondMomentY), 1e-10));
    // Clamp moments from statics: Mz = qy L^2 / 2, My = -qz L^2 / 2 (same signs as a tip load).
    const auto& s = solved.mesh->elements[0].sectionForces;
    CHECK(near(s[5], qy * L2 / 2.0, 1e-9) && near(s[4], -qz * L2 / 2.0, 1e-9));
    CHECK(near(s[10], 0.0, 0.0, 1e-6) && near(s[11], 0.0, 0.0, 1e-6));
    CHECK(solved.energyCheckPassed);
  }
}

TEST(cantileverUnderItsOwnWeight) {
  // w = rho A g per length: tip deflection w L^4 / (8 E I), clamp moment w L^2 / 2.
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.gravity = {0.0, -9.80665, 0.0};
  mesh.elements[0].materialID = 1;
  const auto solved = solve(mesh);
  const double w = kDensity * kSection.area * 9.80665;
  CHECK(near(solved.mesh->nodes[1].getDisplacement()[1], -w * std::pow(kL, 4) / (8.0 * kE * kSection.secondMomentZ), 1e-10));
  CHECK(near(solved.mesh->elements[0].sectionForces[5], -w * kL * kL / 2.0, 1e-9));
  CHECK(near(solved.mesh->elements[0].sectionForces[11], 0.0, 0.0, 1e-6));
  CHECK(solved.energyCheckPassed);
}

TEST(orientationSelectsTheBendingInertia) {
  // v = +Z on a member along X: local y = +Z, local z = -Y, so a load along Y bends about
  // local y and uses Iy.
  const double P = 1e3;
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.elements[0].orientation = {0.0, 0.0, 1.0};
  mesh.nodalLoads = {{1, {0.0, -P, 0.0}, {}}};
  auto solved = solve(mesh);
  CHECK(near(solved.mesh->nodes[1].getDisplacement()[1], -P * std::pow(kL, 3) / (3.0 * kE * kSection.secondMomentY), 1e-10));

  // Column along Y with the default v = +X: local y = +X, so a load along X uses Iz.
  MeshData column;
  column.gravity = {0.0, 0.0, 0.0};
  column.nodes = {Node{0, 0, 0, 0}, Node{1, 0, kL, 0}};
  column.nodes[0].fixAll();
  column.elements = {beam(0, 1)};
  column.nodalLoads = {{1, {P, 0.0, 0.0}, {}}};
  solved = solve(column);
  CHECK(near(solved.mesh->nodes[1].getDisplacement()[0], P * std::pow(kL, 3) / (3.0 * kE * kSection.secondMomentZ), 1e-10));
}

// ---- general 3D behaviour ---------------------------------------------------------------------

TEST(rotatedFrameGivesRotatedResults) {
  // frameModel() solved as is and turned by R: displacements and rotations must turn with
  // it, section forces (local axes) must not change.
  const Eigen::Matrix3d R = Eigen::AngleAxisd(0.7, Eigen::Vector3d(1.0, 2.0, -0.5).normalized()).toRotationMatrix();
  const auto base = solve(frameModel(Eigen::Matrix3d::Identity()));
  const auto turned = solve(frameModel(R));
  CHECK(base.energyCheckPassed && turned.energyCheckPassed);
  for (std::size_t n = 0; n < base.mesh->nodes.size(); ++n) {
    const auto rotate = [&](const Vec& v) {
      const Eigen::Vector3d t = R * Eigen::Vector3d(v[0], v[1], v[2]);
      return Vec{t[0], t[1], t[2]};
    };
    CHECK(nearVec(turned.mesh->nodes[n].getDisplacement(), rotate(base.mesh->nodes[n].getDisplacement()), 1e-9));
    CHECK(nearVec(turned.mesh->nodes[n].getRotation(), rotate(base.mesh->nodes[n].getRotation()), 1e-9));
  }
  for (std::size_t e = 0; e < base.mesh->elements.size(); ++e) {
    const auto& a = base.mesh->elements[e].sectionForces;
    const auto& b = turned.mesh->elements[e].sectionForces;
    double scale = 0.0;
    for (const double v : a) scale = std::max(scale, std::abs(v));
    for (std::size_t i = 0; i < 12; ++i) CHECK(std::abs(a[i] - b[i]) <= 1e-9 * scale);
  }
  // The roller moves only along its rail plane and turns only about its axis.
  const auto& u = base.mesh->nodes[4].getDisplacement();
  const Eigen::Vector3d normal = Eigen::Vector3d(1, 0, 1).cross(Eigen::Vector3d(0, 0.3, 1)).normalized();
  CHECK(std::abs(normal.dot(Eigen::Vector3d(u[0], u[1], u[2]))) <= 1e-12);
  const auto& r = base.mesh->nodes[4].getRotation();
  CHECK(Eigen::Vector3d(r[0], r[1], r[2]).cross(Eigen::Vector3d(0, 1, 1)).norm() <= 1e-12);
}

TEST(formulationForAllThenSingleElements) {
  // Two-element cantilever: all Timoshenko, then the clamp element back to Euler-Bernoulli.
  // Shear deformation of the tip element only must be added.
  const double P = -10e3;
  MeshData mesh;
  mesh.gravity = {0.0, 0.0, 0.0};
  mesh.nodes = {Node{0, 0, 0, 0}, Node{1, kL / 2.0, 0, 0}, Node{2, kL, 0, 0}};
  mesh.nodes[0].fixAll();
  mesh.elements = {beam(0, 1), beam(1, 2)};
  mesh.nodalLoads = {{2, {0.0, P, 0.0}, {}}};
  FEM::BEAM::setFormulationForAll(mesh, Formulation::Timoshenko);
  CHECK(mesh.elements[0].formulation == Formulation::Timoshenko && mesh.elements[1].formulation == Formulation::Timoshenko);
  mesh.elements[0].formulation = Formulation::EulerBernoulli;
  const auto solved = solve(mesh);
  const double bending = P * std::pow(kL, 3) / (3.0 * kE * kSection.secondMomentZ);
  const double shearOfTipHalf = P * (kL / 2.0) / (kG * kSection.shearAreaY);
  CHECK(near(solved.mesh->nodes[2].getDisplacement()[1], bending + shearOfTipHalf, 1e-10));
}

// ---- results along the element ----------------------------------------------------------------

TEST(diagramsAlongACantileverMatchTheHandSolution) {
  // Tip force: u = Px x / EA, v = Py x^2 (3L - x) / (6 E Iz) [+ Py x / (G Asy)], w likewise
  // with Iy / Asz; N, V, T constant, Mz = Py (L - x), My = -Pz (L - x).
  const double Px = 5e3, Py = -12e3, Pz = 7e3, T = 2e3;
  for (const auto formulation : {Formulation::EulerBernoulli, Formulation::Timoshenko}) {
    auto mesh = cantilever(formulation);
    mesh.nodalLoads = {{1, {Px, Py, Pz}, {T, 0.0, 0.0}}};
    const auto solved = solve(mesh);
    const bool timoshenko = formulation == Formulation::Timoshenko;
    const auto states = FEM::BEAM::sampleElement(*solved.mesh, 0, 7, Eigen::Vector3d::Zero(), materials());
    REQUIRE(states.size() == 7);
    for (const auto& state : states) {
      const double x = state.position;
      const double bend = x * x * (3.0 * kL - x) / (6.0 * kE);
      CHECK(near(state.displacement[0], Px * x / (kE * kSection.area), 1e-9, 1e-18));
      CHECK(near(state.displacement[1], Py * bend / kSection.secondMomentZ + (timoshenko ? Py * x / (kG * kSection.shearAreaY) : 0.0), 1e-9, 1e-18));
      CHECK(near(state.displacement[2], Pz * bend / kSection.secondMomentY + (timoshenko ? Pz * x / (kG * kSection.shearAreaZ) : 0.0), 1e-9, 1e-18));
      CHECK(state.localDisplacement == state.displacement); // local axes = global axes here
      CHECK(near(state.forces[0], Px, 1e-9) && near(state.forces[1], Py, 1e-9) && near(state.forces[2], Pz, 1e-9));
      CHECK(near(state.forces[3], T, 1e-9));
      CHECK(near(state.forces[4], -Pz * (kL - x), 1e-9, 1e-6) && near(state.forces[5], Py * (kL - x), 1e-9, 1e-6));
    }
    CHECK(states.front().position == 0.0 && near(states.back().position, kL, 1e-15));
    CHECK(near(states[3].location[0], kL / 2.0, 1e-15));
  }
}

TEST(diagramsOfAClampedBeamUnderUniformLoad) {
  // One clamped-clamped element under q = (qx, qy, qz): the exact fields, so the mid span
  // matches the closed form without a node there. u = qx x (L - x) / (2 E A), N = qx (L/2 - x).
  const double qx = 1e3, qy = -4e3, qz = 2.5e3;
  for (const auto formulation : {Formulation::EulerBernoulli, Formulation::Timoshenko}) {
    MeshData mesh;
    mesh.gravity = {0.0, 0.0, 0.0};
    mesh.nodes = {Node{0, 0, 0, 0}, Node{1, kL, 0, 0}};
    mesh.nodes[0].fixAll();
    mesh.nodes[1].fixAll();
    mesh.elements = {beam(0, 1, formulation)};
    mesh.distributedLoads = {{0, {qx, qy, qz}, FEM::BEAM::LoadFrame::Local}};
    const auto solved = solve(mesh);
    const bool timoshenko = formulation == Formulation::Timoshenko;
    const auto all = FEM::BEAM::sampleAllElements(*solved.mesh, 5, materials());
    REQUIRE(all.size() == 1 && all[0].size() == 5);
    const auto& mid = all[0][2];
    const double L2 = kL * kL;
    CHECK(near(mid.displacement[0], qx * L2 / (8.0 * kE * kSection.area), 1e-9));
    CHECK(near(mid.displacement[1], qy * L2 * L2 / (384.0 * kE * kSection.secondMomentZ) + (timoshenko ? qy * L2 / (8.0 * kG * kSection.shearAreaY) : 0.0), 1e-9));
    CHECK(near(mid.displacement[2], qz * L2 * L2 / (384.0 * kE * kSection.secondMomentY) + (timoshenko ? qz * L2 / (8.0 * kG * kSection.shearAreaZ) : 0.0), 1e-9));
    CHECK(near(mid.forces[0], 0.0, 0.0, 1e-6) && near(all[0][0].forces[0], qx * kL / 2.0, 1e-9));
    CHECK(near(mid.forces[1], 0.0, 0.0, 1e-6) && near(mid.forces[2], 0.0, 0.0, 1e-6));
    CHECK(near(std::abs(mid.forces[5]), std::abs(qy) * L2 / 24.0, 1e-9) && near(std::abs(mid.forces[4]), qz * L2 / 24.0, 1e-9));
    // Both ends: the node section forces, and zero displacement.
    const auto& s = solved.mesh->elements[0].sectionForces;
    for (std::size_t i = 0; i < 6; ++i) {
      CHECK(near(all[0][0].forces[i], s[i], 1e-12, 1e-9));
      CHECK(near(all[0][4].forces[i], s[6 + i], 1e-9, 1e-6));
    }
    CHECK(all[0][0].displacement == (Vec{0, 0, 0}));
    CHECK(near(all[0][4].displacement[1], 0.0, 0.0, 1e-18));
  }
}

TEST(diagramsOfACantileverUnderItsOwnWeight) {
  // v = -w x^2 (6 L^2 - 4 L x + x^2) / (24 E I); the self weight enters through the material.
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.gravity = {0.0, -9.80665, 0.0};
  mesh.elements[0].materialID = 1;
  const auto solved = solve(mesh);
  const double w = kDensity * kSection.area * 9.80665;
  const auto states = FEM::BEAM::sampleAllElements(*solved.mesh, 4, materials())[0];
  for (const auto& state : states) {
    const double x = state.position;
    CHECK(near(state.displacement[1], -w * x * x * (6.0 * kL * kL - 4.0 * kL * x + x * x) / (24.0 * kE * kSection.secondMomentZ), 1e-9, 1e-18));
    CHECK(near(state.forces[5], -w * (kL - x) * (kL - x) / 2.0, 1e-9, 1e-6));
    CHECK(near(state.forces[1], -w * (kL - x), 1e-9, 1e-6));
  }
}

TEST(diagramsTurnWithTheFrame) {
  // Along every element of frameModel(): forces equal, displacements turned, the ends equal
  // the nodal displacements, and dMz/dx = -Vy, dMy/dx = Vz (central differences).
  const Eigen::Matrix3d R = Eigen::AngleAxisd(-1.1, Eigen::Vector3d(0.3, -1.0, 2.0).normalized()).toRotationMatrix();
  const auto base = solve(frameModel(Eigen::Matrix3d::Identity()));
  const auto turned = solve(frameModel(R));
  const auto a = FEM::BEAM::sampleAllElements(*base.mesh, 9, materials());
  const auto b = FEM::BEAM::sampleAllElements(*turned.mesh, 9, materials());
  for (std::size_t e = 0; e < a.size(); ++e) {
    const auto& element = base.mesh->elements[e];
    CHECK(nearVec(a[e].front().displacement, base.mesh->nodes[element.node1].getDisplacement(), 1e-12));
    CHECK(nearVec(a[e].back().displacement, base.mesh->nodes[element.node2].getDisplacement(), 1e-9));
    for (std::size_t i = 0; i < a[e].size(); ++i) {
      const Eigen::Vector3d d = R * Eigen::Vector3d(a[e][i].displacement.data());
      CHECK(nearVec(b[e][i].displacement, {d[0], d[1], d[2]}, 1e-9));
      for (std::size_t k = 0; k < 6; ++k) CHECK(near(b[e][i].forces[k], a[e][i].forces[k], 1e-8, 1e-6));
    }
    const auto loads = FEM::BEAM::elementLocalLoads(base.mesh->nodes, base.mesh->elements, base.mesh->distributedLoads,
                                                    base.mesh->gravity, materials());
    const double h = 1e-4;
    const auto left = FEM::BEAM::sectionAt(*base.mesh, e, 0.5 - h, loads[e], materials());
    const auto centre = FEM::BEAM::sectionAt(*base.mesh, e, 0.5, loads[e], materials());
    const auto right = FEM::BEAM::sectionAt(*base.mesh, e, 0.5 + h, loads[e], materials());
    const double dx = right.position - left.position;
    CHECK(near((right.forces[5] - left.forces[5]) / dx, -centre.forces[1], 1e-6, 1e-6));
    CHECK(near((right.forces[4] - left.forces[4]) / dx, centre.forces[2], 1e-6, 1e-6));
  }
}

TEST(diagramArgumentsAreChecked) {
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.nodalLoads = {{1, {0, -1e3, 0}, {}}};
  const auto zero = Eigen::Vector3d::Zero();
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(mesh, 0, 0.5, zero, materials()); })); // not solved
  const auto solved = solve(mesh);
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(*solved.mesh, 1, 0.5, zero, materials()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(*solved.mesh, 0, 1.5, zero, materials()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(*solved.mesh, 0, std::nan(""), zero, materials()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sampleElement(*solved.mesh, 0, 1, zero, materials()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sampleAllElements(*solved.mesh, 1, materials()); }));
}

// ---- errors -----------------------------------------------------------------------------------

TEST(invalidModelsAreReported) {
  CHECK(contains(errorOf(MeshData{}), "no nodes"));
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.elements.clear();
  CHECK(contains(errorOf(mesh), "no beam elements"));

  const auto broken = [](auto change) {
    auto model = cantilever(Formulation::EulerBernoulli);
    change(model);
    return errorOf(model);
  };
  CHECK(contains(broken([](MeshData& m) { m.elements[0].section.torsionConstant = 0.0; }), "torsion constant"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].section.secondMomentY = -1.0; }), "Iy and Iz"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].section.area = std::nan(""); }), "area"));
  CHECK(contains(broken([](MeshData& m) {
    m.elements[0].formulation = Formulation::Timoshenko;
    m.elements[0].section.shearAreaZ = 0.0;
  }), "shear areas"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].materialID = 9; }), "material"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].node2 = 5; }), "references node 5"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].node2 = 0; }), "starts and ends"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].orientation = {-2.0, 0.0, 0.0}; }), "parallel"));
  CHECK(contains(broken([](MeshData& m) { m.nodalLoads = {{4, {1, 0, 0}, {}}}; }), "missing node 4"));
  CHECK(contains(broken([](MeshData& m) { m.distributedLoads = {{3, {0, 1, 0}, FEM::BEAM::LoadFrame::Global}}; }),
                 "missing element 3"));
  CHECK(contains(broken([](MeshData& m) { m.nodes[1] = Node{7, kL, 0, 0}; }), "node ids"));
  // No support: a mechanism.
  CHECK(contains(broken([](MeshData& m) {
    m.nodes[0] = Node{0, 0, 0, 0};
    m.nodalLoads = {{1, {0, 1, 0}, {}}};
  }), "stiffness solve failed"));
  // A beam pinned at both ends that may spin about its own axis, loaded by a torque: torsion
  // mechanism. (A load that does not excite the mechanism still gets a finite answer: the
  // referee does not check for a singular matrix, the same as for trusses.)
  CHECK(contains(broken([](MeshData& m) {
    m.nodes[0] = Node{0, 0, 0, 0};
    m.nodes[0].setMovable({false, false, false});
    m.nodes[1].setMovable({true, false, false});
    m.nodalLoads = {{1, {1, 0, 0}, {100, 0, 0}}};
  }), "stiffness solve failed"));
}

TEST(cancelledSolveAndProgress) {
  auto mesh = cantilever(Formulation::Timoshenko);
  mesh.nodalLoads = {{1, {0, -1e3, 0}, {}}};
  std::stop_source source;
  source.request_stop();
  const auto cancelled = FEM::BEAM::solveStatic(mesh, materials(), source.get_token());
  CHECK(!cancelled && cancelled.error() == "cancelled");

  std::vector<float> reported;
  const auto solved = FEM::BEAM::solveStatic(mesh, materials(), {}, [&](const float f) { reported.push_back(f); });
  REQUIRE(solved.has_value() && !reported.empty());
  bool increasing = true;
  for (std::size_t i = 1; i < reported.size(); ++i) increasing = increasing && reported[i] >= reported[i - 1];
  CHECK(increasing && reported.back() == 1.0f);
  // The input is not changed by the solve.
  CHECK(mesh.nodes[1].getDisplacement() == (Vec{0, 0, 0}) && !mesh.hasResults);
}

int main(int argc, char** argv) {
  return anaf::TESTING::runAll(argc > 1 ? argv[1] : "");
}
