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
#include <beam/beamEngine/beamStress.hpp>
#include <beam/beamIO/beamMeshAdapter.hpp>
#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp>
#include <beam/beamSection/sectionLibrary.hpp>
#include <beam/beamSection/sectionStress.hpp>
#include <beam/beamSection/sectionTriangulation.hpp>
#include <beam/beamTypes/beamLibrary.hpp>
#include <material/materialLibrary.hpp>
#include <io/core/pathUtf8.hpp>
#include <io/meshIo.hpp>
#include <truss_1D/trussIO/trussMeshAdapter.hpp>

#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

using FEM::BEAM::BeamElement;
using FEM::BEAM::Formulation;
using FEM::BEAM::MeshData;
using FEM::BEAM::Node;

namespace {
  constexpr double kE = 210e9;  // Pa
  constexpr double kG = 80e9;   // Pa
  constexpr double kL = 3.0;    // m
  constexpr double kDensity = 7850.0;
  // Iy != Iz and Asy != Asz, so a swapped axis shows up as a wrong number.
  constexpr FEM::BEAM::SectionProperties kSection{
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

  // Index 0: kSection as a general section (most tests), index 1: a solid rectangle.
  const std::vector<FEM::BEAM::BeamSection>& sections() {
    static const std::vector<FEM::BEAM::BeamSection> list{
      FEM::BEAM::BeamSection{"Test general", FEM::BEAM::GeneralSection{kSection}},
      FEM::BEAM::BeamSection{"Rectangle 300x100", FEM::BEAM::RectangleSection{0.3, 0.1}},
    };
    return list;
  }

  bool near(const double actual, const double expected, const double relative, const double absolute = 0.0) {
    return std::abs(actual - expected) <= relative * std::abs(expected) + absolute;
  }

  bool nearVec(const std::array<double, 3>& actual, const std::array<double, 3>& expected, const double tolerance) {
    double scale = 0.0;
    for (const double v : expected) scale = std::max(scale, std::abs(v));
    for (std::size_t i = 0; i < 3; ++i) {
      if (std::abs(actual[i] - expected[i]) > tolerance * std::max(scale, 1e-300)) return false;
    }
    return true;
  }

  BeamElement beam(const std::uint32_t a, const std::uint32_t b, const Formulation formulation = Formulation::EulerBernoulli,
                   const std::array<double, 3> orientation = {}, const std::uint32_t material = 0) {
    BeamElement element;
    element.node1 = a;
    element.node2 = b;
    element.materialID = material;
    element.sectionID = 0;
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

  FEM::BEAM::StaticResult solve(const MeshData& mesh, std::span<const FEM::BEAM::BeamSection> list = sections()) {
    auto solved = FEM::BEAM::solveStatic(mesh, materials(), list);
    if (!solved) {
      std::printf("      %s\n", solved.error().c_str());
      throw anaf::TESTING::RequireFailure{};
    }
    return std::move(*solved);
  }

  std::string errorOf(const MeshData& mesh, std::span<const FEM::BEAM::BeamSection> list = sections()) {
    const auto solved = FEM::BEAM::solveStatic(mesh, materials(), list);
    return solved ? std::string{} : solved.error();
  }

  // A 3D frame with every feature: mixed formulations, nodal force and moment, global and
  // local distributed loads, self weight, an inclined roller and an inclined rotation
  // support. R turns the whole model (local loads stay as they are).
  MeshData frameModel(const Eigen::Matrix3d& R) {
    const auto turn = [&](const std::array<double, 3>& v) {
      const Eigen::Vector3d t = R * Eigen::Vector3d(v[0], v[1], v[2]);
      return std::array<double, 3>{t[0], t[1], t[2]};
    };
    MeshData mesh;
    mesh.gravity = turn({0.0, -9.80665, 0.0});
    const std::vector<std::array<double, 3>> points{{0, 0, 0}, {0, 3, 0}, {4, 3, 0}, {4, 3, 2.5}, {4, 0, 2.5}};
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

  // A, Iy, Iz and the centroid of a section from its outline (Green's theorem over the
  // polygon loops; holes are clockwise and subtract themselves).
  struct OutlineIntegrals { double area{}, Iy{}, Iz{}, centroidY{}, centroidZ{}; };
  OutlineIntegrals integrateOutline(const FEM::BEAM::SectionShape& shape) {
    OutlineIntegrals result;
    double firstY = 0.0, firstZ = 0.0;
    for (const auto& loop : FEM::BEAM::sectionOutline(shape, 512)) {
      for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto& a = loop[i];
        const auto& b = loop[(i + 1) % loop.size()];
        const double u0 = a[1], v0 = a[0], u1 = b[1], v1 = b[0]; // (z, y) plane
        const double cross = u0 * v1 - u1 * v0;
        result.area += cross / 2.0;
        firstZ += cross * (u0 + u1) / 6.0;
        firstY += cross * (v0 + v1) / 6.0;
        result.Iy += cross * (u0 * u0 + u0 * u1 + u1 * u1) / 12.0;
        result.Iz += cross * (v0 * v0 + v0 * v1 + v1 * v1) / 12.0;
      }
    }
    result.centroidY = firstY / result.area;
    result.centroidZ = firstZ / result.area;
    return result;
  }

  // First moment about the z axis (Q = integral of y dA) of the part of the section with y > 0:
  // every outline loop clipped at y = 0 (Sutherland-Hodgman against one half plane), then
  // Green's theorem. With swapAxes the roles of y and z change (Q about the y axis).
  double halfSectionFirstMoment(const FEM::BEAM::SectionShape& shape, const bool swapAxes = false) {
    double q = 0.0;
    for (auto loop : FEM::BEAM::sectionOutline(shape, 512)) {
      if (swapAxes) {
        for (auto& point : loop) point = {point[1], -point[0]}; // turn by 90 degrees, orientation kept
      }
      std::vector<std::array<double, 2>> clipped;
      for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto& a = loop[i];
        const auto& b = loop[(i + 1) % loop.size()];
        const bool inA = a[0] >= 0.0, inB = b[0] >= 0.0;
        if (inA) clipped.push_back(a);
        if (inA != inB) {
          const double t = a[0] / (a[0] - b[0]);
          clipped.push_back({0.0, a[1] + t * (b[1] - a[1])});
        }
      }
      for (std::size_t i = 0; i < clipped.size(); ++i) {
        const auto& a = clipped[i];
        const auto& b = clipped[(i + 1) % clipped.size()];
        const double cross = a[1] * b[0] - b[1] * a[0]; // (z, y) plane
        q += cross * (a[0] + b[0]) / 6.0;
      }
    }
    return q;
  }

  bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

  // The repository's assets, not findAssetPath(): that prefers an installed package
  // (/usr/share/anafinen/assets), which may hold an older library or catalogue.
  std::filesystem::path sourceAsset(const std::filesystem::path& subpath) {
    return anaf::IO::pathFromUtf8(MAIN_DIR) / "assets" / subpath;
  }

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
  Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 12, 12>> eigen(ti);
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
    CHECK(solved.mesh->nodes[0].getDisplacement() == (std::array<double, 3>{0, 0, 0}) && solved.mesh->nodes[0].getRotation() == (std::array<double, 3>{0, 0, 0}));

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
    const auto rotate = [&](const std::array<double, 3>& v) {
      const Eigen::Vector3d t = R * Eigen::Vector3d(v[0], v[1], v[2]);
      return std::array<double, 3>{t[0], t[1], t[2]};
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

// ---- end releases (hinges) --------------------------------------------------------------------

namespace {
  namespace R = FEM::BEAM::RELEASE;

  // Beam along +X through nodes at x = 0, L, 2L, ... with only the x-y plane loaded; ux and the
  // twist are held at node 0, the x-z plane is held by w = 0 at every node.
  MeshData beamLine(const std::size_t spans, const Formulation formulation) {
    MeshData mesh;
    mesh.gravity = {0.0, 0.0, 0.0};
    for (std::uint32_t i = 0; i <= spans; ++i) mesh.nodes.emplace_back(i, kL * i, 0.0, 0.0);
    for (std::uint32_t i = 0; i < spans; ++i) mesh.elements.push_back(beam(i, i + 1, formulation));
    return mesh;
  }
} // namespace end

TEST(releaseCondensationKeepsTheElementConsistent) {
  // Condensed k* is symmetric, has zero rows / columns at the released DOFs, and a recovered
  // released DOF makes the released end force vanish: k u - f0 = 0 there.
  const auto k = FEM::BEAM::localStiffness(kE, kG, kSection, Formulation::Timoshenko, kL);
  const auto f0 = FEM::BEAM::equivalentNodalLoads(Eigen::Vector3d(1e3, -4e3, 2e3), kL);
  const std::uint16_t releases = R::hinge | R::torsion | R::atNode2(R::momentZ);
  auto condensed = k;
  auto loads = f0;
  REQUIRE(FEM::BEAM::condenseReleases(releases, condensed, loads));
  CHECK((condensed - condensed.transpose()).norm() <= 1e-12 * condensed.norm());
  for (Eigen::Index dof = 0; dof < 12; ++dof) {
    if (!(releases & (1U << dof))) continue;
    CHECK(condensed.row(dof).isZero(0.0) && condensed.col(dof).isZero(0.0) && loads[dof] == 0.0);
  }
  Eigen::Matrix<double, 12, 1> u;
  u << 1e-3, -2e-3, 5e-4, 0.0, 0.0, 0.0, -1e-3, 3e-3, 2e-4, 1e-3, -2e-3, 0.0;
  FEM::BEAM::recoverReleasedDisplacements(releases, k, f0, u);
  const Eigen::Matrix<double, 12, 1> full = k * u - f0;
  const Eigen::Matrix<double, 12, 1> reduced = condensed * u - loads;
  for (Eigen::Index dof = 0; dof < 12; ++dof) {
    if (releases & (1U << dof)) CHECK(std::abs(full[dof]) <= 1e-9 * full.norm());
    else CHECK(std::abs(full[dof] - reduced[dof]) <= 1e-9 * full.norm()); // same end forces
  }
  // Mechanisms inside the element.
  for (const std::uint16_t bad : {std::uint16_t(R::axial | R::atNode2(R::axial)), std::uint16_t(R::torsion | R::atNode2(R::torsion)),
                                  std::uint16_t(R::momentZ | R::shearY | R::atNode2(R::momentZ))}) {
    auto copy = k;
    auto copyLoads = f0;
    CHECK(!FEM::BEAM::condenseReleases(bad, copy, copyLoads) && copy == k);
  }
}

TEST(hingeInAClampedBeamGivesTwoCantilevers) {
  // Clamped at x = 0 and 2L, hinge at mid span (element 0 released about z at node 1), load P
  // at the hinge: two equal cantilevers share P, so v = P L^3 / (6 E Iz) [+ P L / (2 G Asy)],
  // clamp moments P L / 2, no moment at the hinge, and the two sides turn in opposite senses.
  const double P = -12e3;
  for (const auto formulation : {Formulation::EulerBernoulli, Formulation::Timoshenko}) {
    auto mesh = beamLine(2, formulation);
    mesh.nodes[0].fixAll();
    mesh.nodes[2].fixAll();
    mesh.elements[0].endReleases = R::atNode2(R::momentZ);
    mesh.nodalLoads = {{1, {0.0, P, 0.0}, {}}};
    const auto solved = solve(mesh);
    const bool timoshenko = formulation == Formulation::Timoshenko;
    const double cantilever = std::pow(kL, 3) / (3.0 * kE * kSection.secondMomentZ) + (timoshenko ? kL / (kG * kSection.shearAreaY) : 0.0);
    CHECK(near(solved.mesh->nodes[1].getDisplacement()[1], 0.5 * P * cantilever, 1e-10));
    const auto& left = solved.mesh->elements[0].sectionForces;
    const auto& right = solved.mesh->elements[1].sectionForces;
    CHECK(near(left[11], 0.0, 0.0, 1e-6) && near(right[5], 0.0, 0.0, 1e-6)); // no moment at the hinge
    CHECK(near(std::abs(left[5]), 0.5 * std::abs(P) * kL, 1e-9) && near(std::abs(right[11]), 0.5 * std::abs(P) * kL, 1e-9));
    CHECK(near(right[1] - left[1], -P, 1e-9)); // V = V0 - q x: a load along +y lowers it

    // Node 1 turns with the rigid side (element 1); the hinged end of element 0 the other way.
    const auto properties = FEM::BEAM::elementSectionProperties(solved.mesh->elements, sections(), materials());
    const auto loads = FEM::BEAM::elementLocalLoads(solved.mesh->nodes, solved.mesh->elements, properties, solved.mesh->distributedLoads,
                                                    solved.mesh->gravity, materials());
    const auto ends = FEM::BEAM::elementEndDisplacements(*solved.mesh, 0, loads[0], materials(), sections());
    const double tipRotation = 0.5 * P * kL * kL / (2.0 * kE * kSection.secondMomentZ);
    CHECK(near(solved.mesh->nodes[1].getRotation()[2], -tipRotation, 1e-9)); // cantilever from x = 2L
    CHECK(near(ends[11], tipRotation, 1e-9));
    CHECK(near(ends[7], solved.mesh->nodes[1].getDisplacement()[1], 1e-12)); // translation stays connected
    CHECK(solved.energyCheckPassed);
  }
}

TEST(hingeOverASupportGivesSimpleSpans) {
  // Two spans on pin / roller supports, uniform q, hinge over the middle support: two simply
  // supported spans. M = q L^2 / 8 and v = 5 q L^4 / (384 E Iz) at mid span (diagrams), the end
  // slopes q L^3 / (24 E Iz) on either side of the hinge.
  const double q = 6e3;
  auto mesh = beamLine(2, Formulation::EulerBernoulli);
  mesh.nodes[0].setMovable({false, false, false});
  mesh.nodes[0].setRotatable({false, true, true});
  for (const std::uint32_t n : {1U, 2U}) mesh.nodes[n].setMovable({true, false, false});
  mesh.elements[0].endReleases = R::atNode2(R::momentZ);
  mesh.distributedLoads = {{0, {0.0, -q, 0.0}, FEM::BEAM::LoadFrame::Global}, {1, {0.0, -q, 0.0}, FEM::BEAM::LoadFrame::Global}};
  const auto solved = solve(mesh);
  const double slope = q * std::pow(kL, 3) / (24.0 * kE * kSection.secondMomentZ);
  CHECK(near(solved.mesh->nodes[1].getRotation()[2], -slope, 1e-9)); // left end of span 2 (rigid side), sagging: clockwise
  for (std::size_t e = 0; e < 2; ++e) {
    const auto& s = solved.mesh->elements[e].sectionForces;
    CHECK(near(s[5], 0.0, 0.0, 1e-6) && near(s[11], 0.0, 0.0, 1e-6));
    CHECK(near(std::abs(s[1]), q * kL / 2.0, 1e-9));
    const auto middle = FEM::BEAM::sampleAllElements(*solved.mesh, 3, materials(), sections())[e][1];
    CHECK(near(middle.forces[5], q * kL * kL / 8.0, 1e-9));
    CHECK(near(middle.localDisplacement[1], -5.0 * q * std::pow(kL, 4) / (384.0 * kE * kSection.secondMomentZ), 1e-9));
  }
  const auto properties = FEM::BEAM::elementSectionProperties(solved.mesh->elements, sections(), materials());
  const auto loads = FEM::BEAM::elementLocalLoads(solved.mesh->nodes, solved.mesh->elements, properties, solved.mesh->distributedLoads,
                                                  solved.mesh->gravity, materials());
  CHECK(near(FEM::BEAM::elementEndDisplacements(*solved.mesh, 0, loads[0], materials(), sections())[11], slope, 1e-9));
  CHECK(solved.energyCheckPassed);
}

TEST(pinnedGirderAndThreeHingedFrame) {
  // Portal frame, span 2L, columns of height h, uniform q on the girder.
  // (a) Clamped columns, girder pinned at both ends: a simply supported girder (M = q (2L)^2 / 8
  //     at mid span), columns carry q L each and no moment.
  // (b) Pinned bases and a crown hinge: the three-hinged frame, H = q (2L)^2 / (8 h), column top
  //     moment H h, crown moment zero.
  const double q = 5e3, h = 4.0, span = 2.0 * kL;
  for (const bool threeHinged : {false, true}) {
    MeshData mesh;
    mesh.gravity = {0.0, 0.0, 0.0};
    mesh.nodes = {Node{0, 0, 0, 0}, Node{1, 0, h, 0}, Node{2, kL, h, 0}, Node{3, span, h, 0}, Node{4, span, 0, 0}};
    mesh.elements = {beam(0, 1), beam(1, 2), beam(2, 3), beam(4, 3)};
    for (const std::uint32_t base : {0U, 4U}) {
      if (threeHinged) {
        mesh.nodes[base].setMovable({false, false, false});
        mesh.nodes[base].setRotatable({false, false, true}); // pinned in the frame plane
      } else {
        mesh.nodes[base].fixAll();
      }
    }
    if (threeHinged) mesh.elements[1].endReleases = R::atNode2(R::momentZ); // crown
    else mesh.elements[1].endReleases = mesh.elements[2].endReleases = R::hinge | R::atNode2(R::hinge);
    mesh.distributedLoads = {{1, {0, -q, 0}, FEM::BEAM::LoadFrame::Global}, {2, {0, -q, 0}, FEM::BEAM::LoadFrame::Global}};
    const auto solved = solve(mesh);
    CHECK(solved.energyCheckPassed);
    const auto& column = solved.mesh->elements[0].sectionForces;
    const auto& girder = solved.mesh->elements[1].sectionForces;
    CHECK(near(column[0], -q * kL, 1e-9)); // compression
    if (threeHinged) {
      const double H = q * span * span / (8.0 * h);
      CHECK(near(std::abs(column[1]), H, 1e-9));              // local y = global X for a vertical column
      CHECK(near(std::abs(column[11]), H * h, 1e-9));         // column top
      CHECK(near(column[5], 0.0, 0.0, 1e-6));                 // pinned base
      CHECK(near(girder[11], 0.0, 0.0, 1e-6));                // crown hinge
      CHECK(near(girder[0], -H, 1e-9));                       // the girder is pushed together
    } else {
      for (const std::size_t i : {1U, 2U, 4U, 5U, 7U, 8U, 10U, 11U}) CHECK(near(column[i], 0.0, 0.0, 1e-6));
      CHECK(near(girder[5], 0.0, 0.0, 1e-6) && near(girder[11], q * kL * kL / 2.0, 1e-9)); // mid span q (2L)^2 / 8
      // Pinned at both ends, the girders give nodes 1 and 3 no rotational stiffness of their own
      // about z, but the columns do: nothing is held.
    }
  }
}

TEST(freeHingeDirectionsAreHeldOrReported) {
  // Two beams meeting at node 1, both released about z there: node 1 has no stiffness about z.
  // Unloaded, that direction is held (the solve is the hinged result); a moment about z on it is
  // a mechanism.
  auto mesh = beamLine(2, Formulation::EulerBernoulli);
  mesh.nodes[0].fixAll();
  mesh.nodes[2].fixAll();
  mesh.elements[0].endReleases = R::atNode2(R::momentZ);
  mesh.elements[1].endReleases = R::momentZ;
  mesh.nodalLoads = {{1, {0.0, -1e4, 0.0}, {0.0, 0.0, 0.0}}};
  const auto solved = solve(mesh);
  CHECK(solved.mesh->nodes[1].getRotation()[2] == 0.0);
  CHECK(near(solved.mesh->nodes[1].getDisplacement()[1], -0.5e4 * std::pow(kL, 3) / (3.0 * kE * kSection.secondMomentZ), 1e-10));
  CHECK(solved.energyCheckPassed);

  mesh.nodalLoads = {{1, {0.0, -1e4, 0.0}, {0.0, 0.0, 500.0}}};
  CHECK(contains(errorOf(mesh), "node 1 can rotate freely"));
  mesh.nodalLoads = {{1, {0.0, -1e4, 0.0}, {500.0, 200.0, 0.0}}}; // torsion and My still carried
  CHECK(errorOf(mesh).empty());

  auto broken = beamLine(1, Formulation::EulerBernoulli);
  broken.nodes[0].fixAll();
  broken.elements[0].endReleases = R::axial | R::atNode2(R::axial);
  CHECK(contains(errorOf(broken), "mechanism"));
  broken.elements[0].endReleases = 0x1000;
  CHECK(contains(errorOf(broken), "unknown end release bits"));
}

TEST(endReleasesSurviveTheFileAdapter) {
  // Written only when some element has releases; read back bit for bit; invalid codes warn.
  auto mesh = beamLine(2, Formulation::EulerBernoulli);
  mesh.nodes[0].fixAll();
  mesh.nodes[2].fixAll();
  auto model = FEM::BEAM::ADAPTER::toMeshModel(mesh, materials(), sections());
  CHECK(!model.elementAttributes.contains(anaf::IO::Attribute::EndReleases));
  mesh.elements[0].endReleases = R::atNode2(R::hinge) | R::torsion;
  model = FEM::BEAM::ADAPTER::toMeshModel(mesh, materials(), sections());
  REQUIRE(model.elementAttributes.contains(anaf::IO::Attribute::EndReleases));
  CHECK(model.elementAttributes.at(anaf::IO::Attribute::EndReleases)[0] == 3072.0 + 8.0);
  auto imported = FEM::BEAM::ADAPTER::toMeshData(model, materials(), sections());
  REQUIRE(imported.has_value());
  CHECK(imported->mesh->elements[0].endReleases == mesh.elements[0].endReleases && imported->mesh->elements[1].endReleases == 0);
  model.elementAttributes[anaf::IO::Attribute::EndReleases][1] = 5000.0;
  imported = FEM::BEAM::ADAPTER::toMeshData(model, materials(), sections());
  REQUIRE(imported.has_value());
  CHECK(imported->mesh->elements[1].endReleases == 0);
  CHECK(std::ranges::any_of(imported->notes, [](const std::string& n) { return contains(n, "invalid EndReleases"); }));
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
    const auto states = FEM::BEAM::sampleElement(*solved.mesh, 0, 7, Eigen::Vector3d::Zero(), materials(), sections());
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
    const auto all = FEM::BEAM::sampleAllElements(*solved.mesh, 5, materials(), sections());
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
    CHECK(all[0][0].displacement == (std::array<double, 3>{0, 0, 0}));
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
  const auto states = FEM::BEAM::sampleAllElements(*solved.mesh, 4, materials(), sections())[0];
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
  const auto a = FEM::BEAM::sampleAllElements(*base.mesh, 9, materials(), sections());
  const auto b = FEM::BEAM::sampleAllElements(*turned.mesh, 9, materials(), sections());
  for (std::size_t e = 0; e < a.size(); ++e) {
    const auto& element = base.mesh->elements[e];
    CHECK(nearVec(a[e].front().displacement, base.mesh->nodes[element.node1].getDisplacement(), 1e-12));
    CHECK(nearVec(a[e].back().displacement, base.mesh->nodes[element.node2].getDisplacement(), 1e-9));
    for (std::size_t i = 0; i < a[e].size(); ++i) {
      const Eigen::Vector3d d = R * Eigen::Vector3d(a[e][i].displacement.data());
      CHECK(nearVec(b[e][i].displacement, {d[0], d[1], d[2]}, 1e-9));
      for (std::size_t k = 0; k < 6; ++k) CHECK(near(b[e][i].forces[k], a[e][i].forces[k], 1e-8, 1e-6));
    }
    const auto properties = FEM::BEAM::elementSectionProperties(base.mesh->elements, sections(), materials());
    const auto loads = FEM::BEAM::elementLocalLoads(base.mesh->nodes, base.mesh->elements, properties, base.mesh->distributedLoads,
                                                    base.mesh->gravity, materials());
    const double h = 1e-4;
    const auto left = FEM::BEAM::sectionAt(*base.mesh, e, 0.5 - h, loads[e], materials(), sections());
    const auto centre = FEM::BEAM::sectionAt(*base.mesh, e, 0.5, loads[e], materials(), sections());
    const auto right = FEM::BEAM::sectionAt(*base.mesh, e, 0.5 + h, loads[e], materials(), sections());
    const double dx = right.position - left.position;
    CHECK(near((right.forces[5] - left.forces[5]) / dx, -centre.forces[1], 1e-6, 1e-6));
    CHECK(near((right.forces[4] - left.forces[4]) / dx, centre.forces[2], 1e-6, 1e-6));
  }
}

TEST(diagramArgumentsAreChecked) {
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.nodalLoads = {{1, {0, -1e3, 0}, {}}};
  const auto zero = Eigen::Vector3d::Zero();
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(mesh, 0, 0.5, zero, materials(), sections()); })); // not solved
  const auto solved = solve(mesh);
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(*solved.mesh, 1, 0.5, zero, materials(), sections()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(*solved.mesh, 0, 1.5, zero, materials(), sections()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sectionAt(*solved.mesh, 0, std::nan(""), zero, materials(), sections()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sampleElement(*solved.mesh, 0, 1, zero, materials(), sections()); }));
  CHECK(throws([&] { (void)FEM::BEAM::sampleAllElements(*solved.mesh, 1, materials(), sections()); }));
}

// ---- cross-sections ---------------------------------------------------------------------------

TEST(sectionPropertiesMatchClosedForms) {
  using namespace FEM::BEAM;
  constexpr double v = 0.3;
  constexpr double pi = std::numbers::pi;
  // Rectangle 300 x 100: J = beta a c^3 with beta = 0.263 for a/c = 3 (Roark), 0.1406 for a square.
  auto p = computeProperties(RectangleSection{0.3, 0.1}, v);
  CHECK(near(p.area, 0.03, 1e-14) && near(p.secondMomentZ, 0.1 * 0.027 / 12.0, 1e-14) && near(p.secondMomentY, 0.3 * 0.001 / 12.0, 1e-14));
  CHECK(near(p.torsionConstant, 0.263 * 0.3 * 0.001, 2e-3));
  CHECK(near(computeProperties(RectangleSection{0.1, 0.1}, v).torsionConstant, 0.1406 * 1e-4, 3e-3));
  CHECK(near(p.shearAreaY, 10.0 * 1.3 / (12.0 + 3.3) * 0.03, 1e-14) && p.shearAreaY == p.shearAreaZ);
  // Circle and pipe: exact.
  p = computeProperties(CircleSection{0.2}, v);
  CHECK(near(p.area, pi * 0.01, 1e-14) && near(p.secondMomentY, pi * 1e-4 / 4.0, 1e-14) && near(p.torsionConstant, pi * 1e-4 / 2.0, 1e-14));
  CHECK(near(p.shearAreaY, 6.0 * 1.3 / (7.0 + 1.8) * p.area, 1e-14));
  p = computeProperties(PipeSection{0.2, 0.01}, v);
  CHECK(near(p.area, pi * (0.01 - 0.0081), 1e-13) && near(p.secondMomentZ, pi * (1e-4 - 0.09 * 0.09 * 0.09 * 0.09) / 4.0, 1e-13));
  CHECK(near(p.torsionConstant, 2.0 * p.secondMomentY, 1e-14));
  // Thin pipe tends to Cowper's thin-walled tube 2(1+v)/(4+3v).
  p = computeProperties(PipeSection{1.0, 1e-4}, v);
  CHECK(near(p.shearAreaY / p.area, 2.0 * 1.3 / (4.0 + 0.9), 1e-6));
  // Sharp-cornered box: A, I by subtraction, J by Bredt + the open part; square tube kappa = 20(1+v)/(48+39v).
  const double h = 0.2, b = 0.1, t = 0.008;
  p = computeProperties(BoxSection{h, b, t, 0.0, 0.0}, v);
  CHECK(near(p.area, h * b - (h - 2 * t) * (b - 2 * t), 1e-13));
  CHECK(near(p.secondMomentZ, (b * h * h * h - (b - 2 * t) * std::pow(h - 2 * t, 3)) / 12.0, 1e-13));
  CHECK(near(p.secondMomentY, (h * b * b * b - (h - 2 * t) * std::pow(b - 2 * t, 3)) / 12.0, 1e-13));
  const double perimeter = 2.0 * ((b - t) + (h - t));
  const double enclosed = (b - t) * (h - t);
  CHECK(near(p.torsionConstant, t * t * t * perimeter / 3.0 + 4.0 * t * enclosed * enclosed / perimeter, 1e-13));
  CHECK(p.shearAreaY > p.shearAreaZ); // the tall webs carry Vy
  p = computeProperties(BoxSection{0.1, 0.1, 0.005, 0.0, 0.0}, v);
  CHECK(near(p.shearAreaY / p.area, 20.0 * 1.3 / (48.0 + 39.0 * 0.3), 1e-14) && near(p.shearAreaY, p.shearAreaZ, 1e-14));
  // Sharp I: plates only; Cowper's I tends to the rectangle when the flanges vanish.
  p = computeProperties(ISection{0.3, 0.15, 0.007, 0.01, 0.0}, v);
  CHECK(near(p.area, 2 * 0.15 * 0.01 + 0.28 * 0.007, 1e-13));
  CHECK(near(p.secondMomentZ, (0.15 * 0.027 - 0.143 * std::pow(0.28, 3)) / 12.0, 1e-13));
  CHECK(near(p.secondMomentY, (2 * 0.01 * std::pow(0.15, 3) + 0.28 * std::pow(0.007, 3)) / 12.0, 1e-13));
  CHECK(near(p.shearAreaZ, 10.0 * 1.3 / (12.0 + 3.3) * 2 * 0.15 * 0.01, 1e-14));
  p = computeProperties(ISection{0.3, 0.010001, 0.01, 1e-7, 0.0}, v);
  CHECK(near(p.shearAreaY / p.area, 10.0 * 1.3 / (12.0 + 3.3), 1e-4));
  // General: unchanged.
  const auto general = computeProperties(GeneralSection{kSection}, v);
  CHECK(general.area == kSection.area && general.shearAreaZ == kSection.shearAreaZ);
}

TEST(sectionPropertiesMatchTheirOutline) {
  // A, Iy, Iz of every shape (fillets and corner radii included) against the integral over
  // its own outline: the formulas and the drawing describe the same section.
  using namespace FEM::BEAM;
  const std::vector<SectionShape> shapes{
    RectangleSection{0.3, 0.1}, CircleSection{0.2}, PipeSection{0.1143, 0.005},
    BoxSection{0.2, 0.1, 0.008, 0.012, 0.008}, BoxSection{0.1, 0.1, 0.0063, 0.00945, 0.0063}, BoxSection{0.15, 0.1, 0.006, 0.0, 0.0},
    ISection{0.3, 0.15, 0.0071, 0.0107, 0.015}, ISection{0.2, 0.2, 0.009, 0.015, 0.018}, ISection{0.6, 0.22, 0.012, 0.019, 0.0},
  };
  // 512 chords per quarter: a polygon of N = 2048 sides misses (2 pi / N)^2 / 6 = 1.6e-6 of a
  // circle's area and twice that of its second moment.
  for (const auto& shape : shapes) {
    REQUIRE(validateShape(shape).has_value());
    const auto p = computeProperties(shape, 0.3);
    const auto o = integrateOutline(shape);
    if (!near(o.area, p.area, 5e-6) || !near(o.Iy, p.secondMomentY, 5e-6) || !near(o.Iz, p.secondMomentZ, 5e-6)) {
      std::printf("      %s: A %.9g / %.9g, Iy %.9g / %.9g, Iz %.9g / %.9g\n", shapeKey(shape), o.area, p.area, o.Iy, p.secondMomentY, o.Iz, p.secondMomentZ);
    }
    CHECK(near(o.area, p.area, 5e-6) && near(o.Iy, p.secondMomentY, 5e-6) && near(o.Iz, p.secondMomentZ, 5e-6));
    CHECK(std::abs(o.centroidY) < 1e-12 && std::abs(o.centroidZ) < 1e-12);
  }
  CHECK(sectionOutline(GeneralSection{kSection}).empty());
  CHECK(sectionOutline(PipeSection{0.1, 0.01}).size() == 2 && sectionOutline(ISection{0.3, 0.15, 0.007, 0.01, 0.015}).size() == 1);
}

TEST(catalogMatchesPublishedTables) {
  // Reference values from freely available manufacturer section tables (e.g. the ArcelorMittal
  // sales programme), cm units. Our y / z are the tables' z / y:
  // the strong axis of an I-section is local z here.
  using namespace FEM::BEAM;
  const auto catalog = loadSectionLibrary(sourceAsset(kSectionCatalogAsset));
  if (!catalog) std::printf("      %s\n", catalog.error().c_str());
  REQUIRE(catalog.has_value() && catalog->size() > 50);
  const auto find = [&](const char* name) -> const BeamSection& {
    for (const auto& section : *catalog) {
      if (sameSectionName(section.getName(), name)) return section;
    }
    std::printf("      missing '%s'\n", name);
    throw anaf::TESTING::RequireFailure{};
  };
  struct Published { const char* name; double area, strong, weak, torsion; }; // cm^2, cm^4
  // The tables give four significant digits; 1e-3 still catches a 0.63 -> 0.6 slip in the J formula.
  for (const auto& row : {Published{"IPE 200", 28.48, 1943.0, 142.4, 6.98},
                          Published{"IPE 300", 53.81, 8356.0, 603.8, 20.12},
                          Published{"HEA 200", 53.83, 3692.0, 1336.0, 20.98},
                          Published{"HEB 200", 78.08, 5696.0, 2003.0, 59.28},
                          Published{"HEB 300", 149.1, 25170.0, 8563.0, 185.0}}) {
    const auto p = computeProperties(find(row.name).getShape(), 0.3);
    const bool ok = near(p.area * 1e4, row.area, 1e-3) && near(p.secondMomentZ * 1e8, row.strong, 1e-3)
      && near(p.secondMomentY * 1e8, row.weak, 1e-3) && near(p.torsionConstant * 1e8, row.torsion, 1e-3);
    if (!ok) std::printf("      %s: A %.4g, Iz %.5g, Iy %.5g, J %.4g\n", row.name, p.area * 1e4, p.secondMomentZ * 1e8, p.secondMomentY * 1e8, p.torsionConstant * 1e8);
    CHECK(ok);
  }
  const auto chs = computeProperties(find("CHS 114.3x5").getShape(), 0.3);
  CHECK(near(chs.area * 1e4, 17.2, 3e-3) && near(chs.secondMomentY * 1e8, 257.0, 3e-3));
  CHECK(near(computeProperties(find("SHS 100x100x6.3").getShape(), 0.3).area * 1e4, 23.2, 5e-3));
  // Box torsion with corner radii (hot finished: outer 1.5 t, inner 1.0 t), It cross-checked
  // against the freely available Dlubal online cross-section table for hot finished SHS.
  CHECK(near(computeProperties(find("SHS 100x100x6.3").getShape(), 0.3).torsionConstant * 1e8, 534.00, 1e-3));
  CHECK(near(computeProperties(BoxSection{0.1, 0.1, 0.0056, 0.0084, 0.0056}, 0.3).torsionConstant * 1e8, 484.00, 1e-3));
  // Every entry is a valid doubly symmetric shape with dimensions in metres.
  for (const auto& section : *catalog) {
    CHECK(section.getIsBuiltin() && validateSection(section).has_value());
    const auto p = computeProperties(section.getShape(), 0.3);
    CHECK(p.area > 1e-5 && p.area < 1.0); // 0.1 cm^2 .. 10 000 cm^2 (wind turbine tower tubes)
  }
}

TEST(invalidSectionsAreRefused) {
  using namespace FEM::BEAM;
  const auto refused = [](const SectionShape& shape) { return !validateShape(shape).has_value(); };
  CHECK(refused(RectangleSection{0.0, 0.1}) && refused(RectangleSection{0.1, std::nan("")}));
  CHECK(refused(CircleSection{-1.0}));
  CHECK(refused(PipeSection{0.1, 0.05}) && refused(PipeSection{0.1, 0.0}));
  CHECK(refused(BoxSection{0.1, 0.1, 0.05, 0.0, 0.0}) && refused(BoxSection{0.1, 0.1, 0.01, 0.06, 0.0}) && refused(BoxSection{0.1, 0.1, 0.01, 0.0, 0.045}));
  CHECK(refused(ISection{0.3, 0.15, 0.15, 0.01, 0.0}) && refused(ISection{0.3, 0.15, 0.007, 0.15, 0.0}));
  CHECK(refused(ISection{0.3, 0.15, 0.007, 0.01, 0.08}) && refused(ISection{0.3, 0.15, 0.007, 0.01, -0.001}));
  CHECK(refused(GeneralSection{{0.01, 0.0, 1e-5, 1e-5, 0.0, 0.0}}) && refused(GeneralSection{{0.01, 1e-5, 1e-5, 1e-5, -1.0, 0.0}}));
  CHECK(!refused(BoxSection{0.1, 0.1, 0.01, 0.05, 0.04}) && !refused(ISection{0.3, 0.15, 0.007, 0.01, 0.0}));
  CHECK(!validateSection(BeamSection{"", RectangleSection{0.1, 0.1}}).has_value());
  CHECK(!validateSection(BeamSection{"a\"b", RectangleSection{0.1, 0.1}}).has_value());
  CHECK(sameSectionName("ipe 300", "IPE 300") && !sameSectionName("IPE 300", "IPE 30"));
}

TEST(userSectionFileRoundTrip) {
  using namespace FEM::BEAM;
  const std::vector<BeamSection> list{
    BeamSection{"Built-in", RectangleSection{0.2, 0.1}, true, 0},
    BeamSection{"My general", GeneralSection{kSection}},
    BeamSection{"My rectangle", RectangleSection{0.25, 0.12}},
    BeamSection{"My circle", CircleSection{0.08}},
    BeamSection{"My pipe", PipeSection{0.0603, 0.004}},
    BeamSection{"My box", BoxSection{0.2, 0.1, 0.008, 0.012, 0.008}},
    BeamSection{"My I", ISection{0.3, 0.15, 0.0071, 0.0107, 0.015}},
  };
  const auto path = std::filesystem::temp_directory_path() / "anaf_beam_tests" / "userSections.json";
  REQUIRE(saveUserSectionFile(path, list).has_value());
  const auto back = loadUserSectionFile(path);
  REQUIRE(back.has_value() && back->size() == list.size() - 1); // built-ins are not saved
  for (std::size_t i = 0; i < back->size(); ++i) {
    const auto& a = list[i + 1];
    const auto& b = (*back)[i];
    CHECK(a.getName() == b.getName() && !b.getIsBuiltin() && std::string(shapeKey(a.getShape())) == shapeKey(b.getShape()));
    const auto pa = computeProperties(a.getShape(), 0.3);
    const auto pb = computeProperties(b.getShape(), 0.3);
    CHECK(pa.area == pb.area && pa.secondMomentZ == pb.secondMomentZ && pa.torsionConstant == pb.torsionConstant && pa.shearAreaY == pb.shearAreaY);
  }
  std::filesystem::remove_all(path.parent_path());
  const auto missing = loadUserSectionFile(path);
  CHECK(missing.has_value() && missing->empty());
}

TEST(solverUsesTheSectionShape) {
  // Timoshenko cantilever with the solid rectangle: Asy = kappa A with Cowper's kappa for v = 0.3.
  const double P = -20e3;
  auto mesh = cantilever(Formulation::Timoshenko);
  mesh.elements[0].sectionID = 1;
  mesh.nodalLoads = {{1, {0.0, P, 0.0}, {}}};
  const auto solved = solve(mesh);
  const double A = 0.03;
  const double Iz = 0.1 * 0.027 / 12.0;
  const double kappa = 10.0 * 1.3 / (12.0 + 11.0 * 0.3);
  CHECK(near(solved.mesh->nodes[1].getDisplacement()[1], P * std::pow(kL, 3) / (3.0 * kE * Iz) + P * kL / (kG * kappa * A), 1e-10));
  // An IPE 300 from the catalogue, Euler-Bernoulli: the strong axis carries the gravity-direction load.
  const auto catalog = FEM::BEAM::loadSectionLibrary(sourceAsset(FEM::BEAM::kSectionCatalogAsset));
  REQUIRE(catalog.has_value());
  std::uint32_t ipe = 0;
  while (ipe < catalog->size() && (*catalog)[ipe].getName() != "IPE 300") ++ipe;
  REQUIRE(ipe < catalog->size());
  auto steel = cantilever(Formulation::EulerBernoulli);
  steel.elements[0].sectionID = ipe;
  steel.nodalLoads = {{1, {0.0, P, 0.0}, {}}};
  const auto ipeSolved = solve(steel, *catalog);
  const double strong = FEM::BEAM::computeProperties((*catalog)[ipe].getShape(), 0.3).secondMomentZ;
  CHECK(near(strong * 1e8, 8356.0, 3e-3));
  CHECK(near(ipeSolved.mesh->nodes[1].getDisplacement()[1], P * std::pow(kL, 3) / (3.0 * kE * strong), 1e-10));
}

TEST(sectionFacesAreTriangulated) {
  // The triangles of every shape (concave I, holes in box and pipe) cover exactly the outline
  // polygon: all counter-clockwise, and their areas add up to the polygon's area.
  using namespace FEM::BEAM;
  const std::vector<SectionShape> shapes{
    RectangleSection{0.3, 0.1}, CircleSection{0.2}, PipeSection{0.1143, 0.005}, BoxSection{0.2, 0.1, 0.008, 0.012, 0.008},
    BoxSection{0.15, 0.1, 0.006, 0.0, 0.0}, BoxSection{0.1, 0.1, 0.0063, 0.00945, 0.0063}, ISection{0.3, 0.15, 0.0071, 0.0107, 0.015},
    ISection{0.6, 0.22, 0.012, 0.019, 0.0},
  };
  for (const auto& shape : shapes) {
    for (const int segments : {1, 4, 8}) {
      const auto mesh = triangulateSection(shape, segments);
      double area = 0.0;
      bool counterClockwise = true;
      for (const auto& t : mesh.triangles) {
        const auto& a = mesh.points[t[0]];
        const auto& b = mesh.points[t[1]];
        const auto& c = mesh.points[t[2]];
        const double twice = (b[1] - a[1]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[1] - a[1]);
        counterClockwise = counterClockwise && twice > 0.0;
        area += twice / 2.0;
      }
      double outline = 0.0;
      for (const auto& loop : sectionOutline(shape, segments)) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
          const auto& a = loop[i];
          const auto& b = loop[(i + 1) % loop.size()];
          outline += (a[1] * b[0] - b[1] * a[0]) / 2.0;
        }
      }
      if (!near(area, outline, 1e-10)) std::printf("      %s / %d: triangles %.9g, outline %.9g\n", shapeKey(shape), segments, area, outline);
      CHECK(counterClockwise && near(area, outline, 1e-10));
    }
  }
  CHECK(triangulateSection(GeneralSection{kSection}).triangles.empty());
}

// ---- stresses ---------------------------------------------------------------------------------

TEST(normalStressFollowsTheStrainField) {
  // sigma = E eps with eps(y, z) = u' - y v'' - z w'' from the displacement field along the
  // element (central differences, exact for the cubic fields): checks the sign convention of
  // N, My, Mz in sigma_x independently of the stress formula.
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.elements[0].sectionID = 1; // rectangle 300 x 100
  mesh.nodalLoads = {{1, {4e3, -9e3, 3e3}, {}}};
  const auto solved = solve(mesh);
  const auto zero = Eigen::Vector3d::Zero();
  const double xi = 0.4, h = 0.01;
  const auto left = FEM::BEAM::sectionAt(*solved.mesh, 0, xi - h, zero, materials(), sections());
  const auto centre = FEM::BEAM::sectionAt(*solved.mesh, 0, xi, zero, materials(), sections());
  const auto right = FEM::BEAM::sectionAt(*solved.mesh, 0, xi + h, zero, materials(), sections());
  const double dx = h * kL;
  const double du = (right.localDisplacement[0] - left.localDisplacement[0]) / (2.0 * dx);
  const double curvatureV = (right.localDisplacement[1] - 2.0 * centre.localDisplacement[1] + left.localDisplacement[1]) / (dx * dx);
  const double curvatureW = (right.localDisplacement[2] - 2.0 * centre.localDisplacement[2] + left.localDisplacement[2]) / (dx * dx);
  const auto stress = FEM::BEAM::sectionStress(sections()[1].getShape(), centre.forces);
  REQUIRE(stress.has_value());
  for (const auto& point : {stress->maxNormalPoint, stress->minNormalPoint}) {
    const double strain = du - point[0] * curvatureV - point[1] * curvatureW;
    const double expected = point == stress->maxNormalPoint ? stress->maxNormal : stress->minNormal;
    CHECK(near(kE * strain, expected, 1e-6));
  }
  // Downward tip load (Py < 0): the bottom fibre (y < 0) is compressed, the top one stretched.
  CHECK(stress->minNormalPoint[0] < 0.0 && stress->maxNormalPoint[0] > 0.0);
}

TEST(normalStressExtremesMatchTheOutline) {
  // The support function gives the exact extremes: no outline vertex exceeds them, and the
  // returned points carry them.
  using namespace FEM::BEAM;
  const std::vector<SectionShape> shapes{
    RectangleSection{0.3, 0.1}, CircleSection{0.2}, PipeSection{0.1143, 0.005}, BoxSection{0.2, 0.1, 0.008, 0.012, 0.008},
    ISection{0.3, 0.15, 0.0071, 0.0107, 0.015},
  };
  const std::vector<std::array<double, 6>> loads{{1e5, 0, 0, 0, 2e4, -3e4}, {-2e4, 0, 0, 0, -5e3, 0}, {0, 0, 0, 0, 0, 4e4}, {3e4, 0, 0, 0, 0, 0}};
  for (const auto& shape : shapes) {
    const auto p = computeProperties(shape, 0.3);
    for (const auto& f : loads) {
      const auto stress = sectionStress(shape, f);
      REQUIRE(stress.has_value());
      const auto sigma = [&](const std::array<double, 2>& q) { return f[0] / p.area - f[5] * q[0] / p.secondMomentZ + f[4] * q[1] / p.secondMomentY; };
      double outlineMax = -1e300, outlineMin = 1e300;
      for (const auto& loop : sectionOutline(shape, 256)) {
        for (const auto& q : loop) {
          outlineMax = std::max(outlineMax, sigma(q));
          outlineMin = std::min(outlineMin, sigma(q));
        }
      }
      const double scale = std::max(std::abs(stress->maxNormal), std::abs(stress->minNormal));
      CHECK(outlineMax <= stress->maxNormal + 1e-9 * scale && outlineMax >= stress->maxNormal - 1e-4 * scale);
      CHECK(outlineMin >= stress->minNormal - 1e-9 * scale && outlineMin <= stress->minNormal + 1e-4 * scale);
      CHECK(near(sigma(stress->maxNormalPoint), stress->maxNormal, 1e-12, 1e-9 * scale));
      CHECK(near(sigma(stress->minNormalPoint), stress->minNormal, 1e-12, 1e-9 * scale));
    }
  }
  CHECK(!sectionStress(GeneralSection{kSection}, {1, 0, 0, 0, 0, 0}).has_value());
}

TEST(shearAndTorsionStressesMatchClosedForms) {
  using namespace FEM::BEAM;
  constexpr double pi = std::numbers::pi;
  const double V = 1e4, T = 2e3;
  const auto shear = [&](const SectionShape& shape, const double vy, const double vz) { return sectionStress(shape, {0, vy, vz, 0, 0, 0})->shearFromForce; };
  const auto torsion = [&](const SectionShape& shape) { return sectionStress(shape, {0, 0, 0, T, 0, 0})->shearFromTorsion; };
  // Rectangle: 1.5 V / A; Roark's T (3a + 1.8c) / (a^2 c^2).
  CHECK(near(shear(RectangleSection{0.3, 0.1}, V, 0.0), 1.5 * V / 0.03, 1e-12) && near(shear(RectangleSection{0.3, 0.1}, 0.0, -V), 1.5 * V / 0.03, 1e-12));
  CHECK(near(torsion(RectangleSection{0.3, 0.1}), T * (0.9 + 0.18) / (0.09 * 0.01), 1e-12));
  // Circle: 4 V / 3A, T r / J; pipe: thin wall 2 V / A, T ro / J.
  CHECK(near(shear(CircleSection{0.2}, V, 0.0), 4.0 * V / (3.0 * pi * 0.01), 1e-12));
  CHECK(near(torsion(CircleSection{0.2}), 2.0 * T / (pi * 0.001), 1e-12));
  CHECK(near(shear(PipeSection{1.0, 1e-4}, V, 0.0), 2.0 * V / computeProperties(PipeSection{1.0, 1e-4}, 0.3).area, 1e-3));
  CHECK(near(torsion(PipeSection{0.2, 0.01}), T * 0.1 / computeProperties(PipeSection{0.2, 0.01}, 0.3).torsionConstant, 1e-12));
  // Sharp box: Q = (w h^2 - (w - 2t)(h - 2t)^2) / 8 over two webs; Bredt T / (2 (h - t)(w - t) t).
  const double h = 0.2, w = 0.1, t = 0.008;
  const BoxSection box{h, w, t, 0.0, 0.0};
  const auto boxProps = computeProperties(box, 0.3);
  CHECK(near(shear(box, V, 0.0), V * (w * h * h - (w - 2 * t) * (h - 2 * t) * (h - 2 * t)) / 8.0 / (boxProps.secondMomentZ * 2 * t), 1e-12));
  CHECK(near(torsion(box), T / (2.0 * (h - t) * (w - t) * t), 1e-12));
  // Sharp I: web Q = b tf (h - tf) / 2 + tw (h/2 - tf)^2 / 2; flanges 1.5 Vz / (2 b tf); T t_max / J.
  const ISection beam{0.3, 0.15, 0.007, 0.01, 0.0};
  const auto beamProps = computeProperties(beam, 0.3);
  const double q = 0.15 * 0.01 * 0.29 / 2.0 + 0.007 * 0.14 * 0.14 / 2.0;
  CHECK(near(shear(beam, V, 0.0), V * q / (beamProps.secondMomentZ * 0.007), 1e-12));
  CHECK(near(shear(beam, 0.0, V), 1.5 * V / (2.0 * 0.15 * 0.01), 1e-12));
  CHECK(near(torsion(beam), T * 0.01 / beamProps.torsionConstant, 1e-12));
  // With fillets and corner radii: Q against the clipped outline integral (tau I t / V = Q).
  const ISection rolled{0.3, 0.15, 0.0071, 0.0107, 0.015};
  CHECK(near(shear(rolled, V, 0.0) * computeProperties(rolled, 0.3).secondMomentZ * 0.0071 / V, halfSectionFirstMoment(rolled), 2e-6));
  const BoxSection hollow{0.2, 0.1, 0.008, 0.012, 0.008};
  const auto hollowProps = computeProperties(hollow, 0.3);
  CHECK(near(shear(hollow, V, 0.0) * hollowProps.secondMomentZ * 2 * 0.008 / V, halfSectionFirstMoment(hollow), 2e-6));
  CHECK(near(shear(hollow, 0.0, V) * hollowProps.secondMomentY * 2 * 0.008 / V, halfSectionFirstMoment(hollow, true), 2e-6));
  // Rounded corners and fillets change Q only a little.
  CHECK(near(shear(BoxSection{h, w, t, 1.5 * t, t}, V, 0.0), shear(box, V, 0.0), 0.05));
  CHECK(near(shear(ISection{0.3, 0.15, 0.007, 0.01, 0.015}, V, 0.0), shear(beam, V, 0.0), 0.05));
  // von Mises: |sigma| alone, sqrt(3) tau alone.
  const auto pure = sectionStress(RectangleSection{0.3, 0.1}, {3e4, 0, 0, 0, 0, 0});
  CHECK(near(pure->vonMises, 1e6, 1e-12));
  const auto twist = sectionStress(CircleSection{0.2}, {0, 0, 0, T, 0, 0});
  CHECK(near(twist->vonMises, std::sqrt(3.0) * twist->shearFromTorsion, 1e-12));
}

TEST(elementStressAlongTheElement) {
  using namespace FEM::BEAM;
  // Cantilever, catalogue IPE 300, tip load down: sigma = P L (h/2) / Iz at the clamp.
  const auto catalog = loadSectionLibrary(sourceAsset(kSectionCatalogAsset));
  REQUIRE(catalog.has_value());
  std::uint32_t ipe = 0;
  while (ipe < catalog->size() && (*catalog)[ipe].getName() != "IPE 300") ++ipe;
  REQUIRE(ipe < catalog->size());
  const double P = 30e3;
  auto mesh = cantilever(Formulation::EulerBernoulli);
  mesh.elements[0].sectionID = ipe;
  mesh.nodalLoads = {{1, {0.0, -P, 0.0}, {}}};
  auto solved = solve(mesh, *catalog);
  const auto ipeProps = computeProperties((*catalog)[ipe].getShape(), 0.3);
  const auto& stress = solved.mesh->elements[0].stress;
  const double bending = P * kL * 0.15 / ipeProps.secondMomentZ;
  CHECK(stress.available && near(stress.maxNormal, bending, 1e-9) && near(stress.minNormal, -bending, 1e-9));
  CHECK(stress.vonMisesPosition == 0.0 && stress.maxVonMises >= bending && !stress.isStressExceeded); // 236 MPa < 250 MPa
  // Twice the load exceeds the 250 MPa yield strength of the test steel.
  mesh.nodalLoads[0].force[1] = -2.0 * P;
  CHECK(solve(mesh, *catalog).mesh->elements[0].stress.isStressExceeded);

  // Simply supported rectangle under uniform load: the maximum is at mid span, found as the
  // stationary point of Mz even without samples: sigma = (q L^2 / 8)(h/2) / Iz.
  const double q = 5e3;
  MeshData simple;
  simple.gravity = {0.0, 0.0, 0.0};
  simple.nodes = {Node{0, 0, 0, 0}, Node{1, kL, 0, 0}};
  simple.nodes[0].setMovable({false, false, false});
  simple.nodes[0].setRotatable({false, true, true}); // torsion held at one end
  simple.nodes[1].setMovable({true, false, false});
  simple.elements = {beam(0, 1)};
  simple.elements[0].sectionID = 1;
  simple.distributedLoads = {{0, {0.0, -q, 0.0}, LoadFrame::Global}};
  solved = solve(simple);
  const double Iz = 0.1 * 0.027 / 12.0;
  const double midSpan = q * kL * kL / 8.0 * 0.15 / Iz;
  const auto& span = solved.mesh->elements[0];
  CHECK(near(span.stress.maxNormal, midSpan, 1e-9) && near(span.stress.minNormal, -midSpan, 1e-9));
  const auto direct = elementStress(span, Eigen::Vector3d(0.0, -q, 0.0), kL, sections()[1].getShape(), 250e6, 1);
  CHECK(near(direct.maxNormal, midSpan, 1e-9));
  // Shear peaks at the supports: 1.5 (qL/2) / A.
  CHECK(near(span.stress.maxShear, 1.5 * q * kL / 2.0 / 0.03, 1e-9));
  // A general section has no stresses.
  CHECK(!solve(cantilever(Formulation::EulerBernoulli)).mesh->elements[0].stress.available);
}

// ---- file adapter ------------------------------------------------------------------------------

namespace {
  // Every shape kind with a name, as a user would have them.
  const std::vector<FEM::BEAM::BeamSection>& fileSections() {
    static const std::vector<FEM::BEAM::BeamSection> list{
      FEM::BEAM::BeamSection{"Test general", FEM::BEAM::GeneralSection{kSection}},
      FEM::BEAM::BeamSection{"Rectangle 300x100", FEM::BEAM::RectangleSection{0.3, 0.1}},
      FEM::BEAM::BeamSection{"My box", FEM::BEAM::BoxSection{0.2, 0.1, 0.008, 0.012, 0.008}},
      FEM::BEAM::BeamSection{"My I", FEM::BEAM::ISection{0.3, 0.15, 0.0071, 0.0107, 0.015}},
      FEM::BEAM::BeamSection{"My pipe", FEM::BEAM::PipeSection{0.1143, 0.005}},
    };
    return list;
  }

  // frameModel() with one section per element and a rotation support along a global axis
  // (an inclined one cannot be stored, see inclinedRotationSupportIsReported), solved.
  FEM::BEAM::StaticResult solvedFileFrame() {
    auto mesh = frameModel(Eigen::Matrix3d::Identity());
    mesh.nodes[4].setAllowedRotationAxes({{0.0, 0.0, 1.0}});
    mesh.elements.push_back(beam(1, 3, Formulation::Timoshenko, {0, 1, 0}, 1)); // a brace, for the fifth shape
    mesh.elements.back().endReleases = FEM::BEAM::RELEASE::hinge | FEM::BEAM::RELEASE::atNode2(FEM::BEAM::RELEASE::hinge); // pinned
    for (std::uint32_t e = 0; e < mesh.elements.size(); ++e) mesh.elements[e].sectionID = e; // general, rectangle, box, I, pipe
    mesh.nodalLoads.push_back({4, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.5e3}});
    return solve(mesh, fileSections());
  }

  // Same span (projector) of two orthonormal bases.
  bool sameSpan(const std::vector<std::array<double, 3>>& a, const std::vector<std::array<double, 3>>& b) {
    if (a.size() != b.size()) return false;
    Eigen::Matrix3d pa = Eigen::Matrix3d::Zero(), pb = Eigen::Matrix3d::Zero();
    for (const auto& v : a) pa += Eigen::Vector3d(v[0], v[1], v[2]) * Eigen::Vector3d(v[0], v[1], v[2]).transpose();
    for (const auto& v : b) pb += Eigen::Vector3d(v[0], v[1], v[2]) * Eigen::Vector3d(v[0], v[1], v[2]).transpose();
    return (pa - pb).norm() <= 1e-12;
  }

  // Sum of the uniform loads per element and frame (the file stores the sums).
  std::map<std::pair<std::uint32_t, int>, std::array<double, 3>> loadSums(const MeshData& mesh) {
    std::map<std::pair<std::uint32_t, int>, std::array<double, 3>> sums;
    for (const auto& load : mesh.distributedLoads) {
      auto& sum = sums[{load.element, static_cast<int>(load.frame)}];
      for (std::size_t k = 0; k < 3; ++k) sum[k] += load.value[k];
    }
    return sums;
  }

  void compareBeamModels(const MeshData& a, const MeshData& b, const char* what) {
    bool ok = a.nodes.size() == b.nodes.size() && a.elements.size() == b.elements.size() && a.hasResults == b.hasResults
      && a.gravity == b.gravity && a.nodalLoads.size() == b.nodalLoads.size() && loadSums(a) == loadSums(b);
    for (std::size_t i = 0; ok && i < a.nodes.size(); ++i) {
      const auto& x = a.nodes[i];
      const auto& y = b.nodes[i];
      ok = x.getLocation() == y.getLocation() && sameSpan(x.getAllowedMotionDirections(), y.getAllowedMotionDirections())
        && sameSpan(x.getAllowedRotationAxes(), y.getAllowedRotationAxes()) && x.getDisplacement() == y.getDisplacement()
        && x.getRotation() == y.getRotation();
    }
    for (std::size_t e = 0; ok && e < a.elements.size(); ++e) {
      const auto& x = a.elements[e];
      const auto& y = b.elements[e];
      ok = x.node1 == y.node1 && x.node2 == y.node2 && x.materialID == y.materialID && x.sectionID == y.sectionID
        && x.formulation == y.formulation && x.orientation == y.orientation && x.endReleases == y.endReleases && x.sectionForces == y.sectionForces
        && x.stress.available == y.stress.available && near(x.stress.maxVonMises, y.stress.maxVonMises, 1e-12, 1e-6);
    }
    for (std::size_t l = 0; ok && l < a.nodalLoads.size(); ++l) {
      ok = a.nodalLoads[l].node == b.nodalLoads[l].node && a.nodalLoads[l].force == b.nodalLoads[l].force
        && a.nodalLoads[l].moment == b.nodalLoads[l].moment;
    }
    if (!ok) std::printf("      %s: beam model differs after the round trip\n", what);
    CHECK(ok);
  }

  std::filesystem::path beamWorkDir() {
    const auto dir = std::filesystem::temp_directory_path() / "anaf_beam_tests";
    std::filesystem::create_directories(dir);
    return dir;
  }
} // namespace end

TEST(beamModelSurvivesEveryWritableFormat) {
  // Sections of every shape, mixed formulations, inclined translational supports, nodal force
  // and moment, global and local uniform loads, self weight and results: bit-exact through
  // every format (shortest round-trip doubles), and the imported model solves the same.
  namespace IO = anaf::IO;
  const auto solved = solvedFileFrame();
  const auto model = FEM::BEAM::ADAPTER::toMeshModel(*solved.mesh, materials(), fileSections());
  CHECK(model.validate().empty() && model.warnings.empty());
  CHECK(FEM::BEAM::ADAPTER::isBeamModel(model));

  struct Variant { const char* file; IO::WriteOptions options; };
  const Variant variants[] = {
    {"b41b.msh", {.encoding = IO::Encoding::Binary}}, {"b41a.msh", {}}, {"b22a.msh", {.mshVersion = IO::MshVersion::V2_2}},
    {"bz.vtu", {.encoding = IO::Encoding::Binary, .compress = true}}, {"ba.vtu", {}},
    {"b51b.vtk", {.encoding = IO::Encoding::Binary}}, {"b42a.vtk", {.vtkVersion = IO::VtkLegacyVersion::V4_2}},
  };
  for (const auto& v : variants) {
    const auto path = beamWorkDir() / v.file;
    REQUIRE(IO::writeMesh(path, model, v.options).has_value());
    const auto read = IO::readMesh(path);
    if (!read) std::printf("      %s: %s\n", v.file, read.error().message.c_str());
    REQUIRE(read.has_value());
    const auto imported = FEM::BEAM::ADAPTER::toMeshData(*read, materials(), fileSections());
    if (!imported) std::printf("      %s: %s\n", v.file, imported.error().c_str());
    REQUIRE(imported.has_value());
    CHECK(imported->newSections.empty()); // every section found by name and shape
    compareBeamModels(*solved.mesh, *imported->mesh, v.file);
  }

  // The imported model, solved again, gives the same results. The inclined support basis is
  // rebuilt on import and may differ in the last bit (same span, see sameSpan), which MSVC
  // carries to ~1e-12 relative in the displacements.
  const auto read = IO::readMesh(beamWorkDir() / "b41b.msh");
  REQUIRE(read.has_value());
  auto imported = FEM::BEAM::ADAPTER::toMeshData(*read, materials(), fileSections());
  REQUIRE(imported.has_value());
  const auto again = solve(*imported->mesh, fileSections());
  for (std::size_t n = 0; n < again.mesh->nodes.size(); ++n) {
    CHECK(nearVec(again.mesh->nodes[n].getDisplacement(), solved.mesh->nodes[n].getDisplacement(), 1e-10));
  }
}

TEST(beamModelSurvivesStepWithSidecar) {
  // CAD export: geometry as edges, everything else in the .anafFields sidecar; meshing may
  // renumber, so compare counts, sections and the solve.
  namespace IO = anaf::IO;
  const auto solved = solvedFileFrame();
  const auto model = FEM::BEAM::ADAPTER::toMeshModel(*solved.mesh, materials(), fileSections());
  const auto path = beamWorkDir() / "frame.step";
  REQUIRE(IO::writeMesh(path, model, {}).has_value());
  const auto read = IO::readMesh(path);
  REQUIRE(read.has_value());
  const auto imported = FEM::BEAM::ADAPTER::toMeshData(*read, materials(), fileSections());
  if (!imported) std::printf("      %s\n", imported.error().c_str());
  REQUIRE(imported.has_value());
  CHECK(imported->newSections.empty());
  CHECK(imported->mesh->nodes.size() == solved.mesh->nodes.size() && imported->mesh->elements.size() == solved.mesh->elements.size());
  CHECK(imported->mesh->distributedLoads.size() == solved.mesh->distributedLoads.size() && imported->mesh->hasResults);
  double before = 0.0, after = 0.0;
  for (const auto& node : solved.mesh->nodes) before = std::max(before, std::hypot(node.getDisplacement()[0], node.getDisplacement()[1], node.getDisplacement()[2]));
  const auto again = solve(*imported->mesh, fileSections());
  for (const auto& node : again.mesh->nodes) after = std::max(after, std::hypot(node.getDisplacement()[0], node.getDisplacement()[1], node.getDisplacement()[2]));
  CHECK(near(after, before, 1e-9));
}

TEST(beamImportAddsUnknownSections) {
  // The importing list lacks "My box" and has another "My I": both come back as new sections
  // (the second under a new name); known ones are reused.
  const auto solved = solvedFileFrame();
  const auto model = FEM::BEAM::ADAPTER::toMeshModel(*solved.mesh, materials(), fileSections());
  const std::vector<FEM::BEAM::BeamSection> other{
    FEM::BEAM::BeamSection{"Rectangle 300x100", FEM::BEAM::RectangleSection{0.3, 0.1}},
    FEM::BEAM::BeamSection{"my i", FEM::BEAM::ISection{0.3, 0.15, 0.0071, 0.0107, 0.0}}, // no root radius: another section
  };
  const auto imported = FEM::BEAM::ADAPTER::toMeshData(model, materials(), other);
  REQUIRE(imported.has_value());
  const auto& added = imported->newSections;
  REQUIRE(added.size() == 4); // general, box, I, pipe
  const auto& elements = imported->mesh->elements;
  CHECK(elements[1].sectionID == 0); // the rectangle is reused
  const auto nameOf = [&](const std::uint32_t id) { return id < other.size() ? other[id].getName() : added[id - other.size()].getName(); };
  CHECK(nameOf(elements[0].sectionID) == "Test general" && nameOf(elements[2].sectionID) == "My box");
  CHECK(nameOf(elements[3].sectionID) == "My I (imported)" && nameOf(elements[4].sectionID) == "My pipe");
  for (const auto& section : added) CHECK(FEM::BEAM::validateSection(section).has_value());
  // The shapes came through intact: the solve with the extended list matches.
  std::vector<FEM::BEAM::BeamSection> extended = other;
  extended.insert(extended.end(), added.begin(), added.end());
  const auto again = solve(*imported->mesh, extended);
  for (std::size_t n = 0; n < again.mesh->nodes.size(); ++n) {
    CHECK(nearVec(again.mesh->nodes[n].getDisplacement(), solved.mesh->nodes[n].getDisplacement(), 1e-9));
  }
}

TEST(inclinedRotationSupportIsReported) {
  // Rotation free about (0, 1, 1) only: no global axis is free, so the file holds all three,
  // with a warning.
  const auto mesh = frameModel(Eigen::Matrix3d::Identity());
  const auto model = FEM::BEAM::ADAPTER::toMeshModel(mesh, materials(), sections());
  REQUIRE(model.warnings.size() == 1);
  CHECK(contains(model.warnings[0], "node 4"));
  const auto it = std::ranges::find_if(model.constraints, [](const anaf::IO::NodeConstraint& c) { return c.node == 4; });
  REQUIRE(it != model.constraints.end());
  CHECK(it->fixedRotation == (std::array<bool, 3>{true, true, true}) && it->allowedMotion.size() == 2);
}

TEST(beamFilesAreTellApartFromTrussFiles) {
  // A truss export has no beam formulation; a beam file without section data is refused.
  FEM::TRUSS::MeshData truss;
  truss.trussNodes = {FEM::TRUSS::Node(0, 0, 0, 0), FEM::TRUSS::Node(1, 1, 0, 0)};
  truss.trussElements = {{0, 1, 0.0f, false, 0, 1e-4, false}};
  CHECK(!FEM::BEAM::ADAPTER::isBeamModel(FEM::TRUSS::ADAPTER::toMeshModel(truss, materials())));

  anaf::IO::MeshModel bare;
  bare.nodes = {anaf::IO::Node{1, {0, 0, 0}}, anaf::IO::Node{2, {1, 0, 0}}};
  auto& line = bare.blockFor(anaf::IO::ElementType::Line2);
  line.tags = {1};
  line.connectivity = {0, 1};
  line.entityTags = {0};
  bare.elementAttributes[anaf::IO::Attribute::ElementFormulation] = {1.0};
  CHECK(FEM::BEAM::ADAPTER::isBeamModel(bare));
  const auto refused = FEM::BEAM::ADAPTER::toMeshData(bare, materials(), sections());
  CHECK(!refused && contains(refused.error(), "no usable section"));
  // With A, Iy, Iz, J it becomes a general section.
  bare.elementAttributes[anaf::IO::Attribute::CrossSectionArea] = {0.01};
  bare.elementAttributes[anaf::IO::Attribute::SecondMomentY] = {2e-5};
  bare.elementAttributes[anaf::IO::Attribute::SecondMomentZ] = {8e-5};
  bare.elementAttributes[anaf::IO::Attribute::TorsionConstant] = {1e-5};
  const auto accepted = FEM::BEAM::ADAPTER::toMeshData(bare, materials(), sections());
  REQUIRE(accepted.has_value() && accepted->newSections.size() == 1);
  CHECK(std::holds_alternative<FEM::BEAM::GeneralSection>(accepted->newSections[0].getShape()));
  CHECK(accepted->newSections[0].getName() == "Imported section 1");
}

// ---- built-in library ----------------------------------------------------------------------

namespace {
  struct BuiltInLists {
    std::vector<anaf::MATERIAL::Material> materials;
    std::vector<FEM::BEAM::BeamSection> sections;
  };
  const BuiltInLists& builtInLists() {
    static const BuiltInLists lists = [] {
      BuiltInLists l;
      auto materials = anaf::MATERIAL::loadMaterialLibrary(sourceAsset("bridge/materialProperties.json"));
      auto sections = FEM::BEAM::loadSectionLibrary(sourceAsset(FEM::BEAM::kSectionCatalogAsset));
      if (materials) l.materials = std::move(*materials);
      if (sections) l.sections = std::move(*sections);
      return l;
    }();
    return lists;
  }
  std::filesystem::path beamLibraryDir() { return sourceAsset(std::filesystem::path(FEM::BEAM::LIBRARY::kLibrarySubdir)); }

  std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
} // namespace end

TEST(builtInBeamLibraryMatchesTheGenerator) {
  // The committed files must be what anaf_beam_library_tool writes (regenerate after changing
  // beamLibrary.cpp or the catalogue; never edit them by hand). Compared by content with a
  // tolerance: compilers and the solver back end (CHOLMOD / LDLT) may change the last bits.
  namespace IO = anaf::IO;
  const auto& lists = builtInLists();
  REQUIRE(!lists.materials.empty() && !lists.sections.empty());
  const auto generated = beamWorkDir() / "library";
  const auto entries = FEM::BEAM::LIBRARY::writeLibrary(generated, lists.materials, lists.sections);
  if (!entries) std::printf("      %s\n", entries.error().c_str());
  REQUIRE(entries.has_value());
  CHECK(entries->size() >= 30 && entries->size() <= 50);
  const auto indexName = std::filesystem::path(FEM::BEAM::LIBRARY::kIndexFile);
  CHECK_MSG(readText(beamLibraryDir() / indexName) == readText(generated / indexName), "index.json is stale");

  const auto close = [](const double a, const double b, const double scale) { return std::abs(a - b) <= 1e-9 * (1.0 + std::abs(a)) + 1e-7 * scale; };
  for (const auto& entry : *entries) {
    for (const bool solved : {false, true}) {
      const auto committedPath = solved ? FEM::BEAM::LIBRARY::solvedFile(beamLibraryDir(), entry) : FEM::BEAM::LIBRARY::modelFile(beamLibraryDir(), entry);
      const auto freshPath = solved ? FEM::BEAM::LIBRARY::solvedFile(generated, entry) : FEM::BEAM::LIBRARY::modelFile(generated, entry);
      const auto committed = IO::readMesh(committedPath);
      const auto fresh = IO::readMesh(freshPath);
      CHECK_MSG(committed.has_value(), entry.id + (solved ? "_solved" : "") + " missing");
      if (!committed || !fresh) continue;
      bool same = committed->nodes.size() == fresh->nodes.size() && committed->blocks.size() == fresh->blocks.size()
        && committed->constraints.size() == fresh->constraints.size() && committed->loads.size() == fresh->loads.size()
        && committed->sets.size() == fresh->sets.size() && committed->elementAttributes.size() == fresh->elementAttributes.size()
        && committed->fields.size() == fresh->fields.size();
      for (std::size_t i = 0; same && i < fresh->nodes.size(); ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) same = close(committed->nodes[i].position[axis], fresh->nodes[i].position[axis], 0.0);
      }
      for (std::size_t b = 0; same && b < fresh->blocks.size(); ++b) same = committed->blocks[b].connectivity == fresh->blocks[b].connectivity;
      for (const auto& [name, values] : fresh->elementAttributes) {
        const auto it = committed->elementAttributes.find(name);
        same = same && it != committed->elementAttributes.end() && it->second.size() == values.size();
        for (std::size_t e = 0; same && e < values.size(); ++e) same = close(it->second[e], values[e], 0.0);
      }
      for (std::size_t s = 0; same && s < fresh->sets.size(); ++s) same = committed->sets[s].name == fresh->sets[s].name && committed->sets[s].members == fresh->sets[s].members;
      for (std::size_t f = 0; same && f < fresh->fields.size(); ++f) {
        const auto& a = committed->fields[f].steps.back();
        const auto& b = fresh->fields[f].steps.back();
        double scale = 0.0;
        for (const double v : b) scale = std::max(scale, std::abs(v));
        same = committed->fields[f].name == fresh->fields[f].name && a.size() == b.size();
        for (std::size_t k = 0; same && k < b.size(); ++k) same = close(a[k], b[k], scale);
      }
      CHECK_MSG(same, entry.id + (solved ? "_solved" : "") + " is stale: regenerate with anaf_beam_library_tool");
    }
  }
}

TEST(builtInBeamsAreStableAndReasonable) {
  // Every model imports with catalogue sections only (no new user sections, no warnings),
  // solves with a passing energy check, stays elastic (von Mises below yield) and is no
  // mechanism: small loads on every DOF must give small displacements (an unloaded mechanism
  // would not show in the design load case, ARCHITECTURE.md known issue 6). Its _solved file
  // carries the same results.
  namespace IO = anaf::IO;
  const auto& lists = builtInLists();
  const auto index = FEM::BEAM::LIBRARY::loadIndex(beamLibraryDir() / std::filesystem::path(FEM::BEAM::LIBRARY::kIndexFile));
  REQUIRE(index.has_value());
  for (const auto& entry : *index) {
    const auto read = IO::readMesh(FEM::BEAM::LIBRARY::modelFile(beamLibraryDir(), entry));
    REQUIRE(read.has_value());
    const auto imported = FEM::BEAM::ADAPTER::toMeshData(*read, lists.materials, lists.sections);
    if (!imported) std::printf("      %s: %s\n", entry.id.c_str(), imported.error().c_str());
    REQUIRE(imported.has_value());
    CHECK_MSG(imported->newSections.empty(), entry.id + " needs sections outside the catalogue");
    CHECK_MSG(std::ranges::none_of(imported->notes, [](const std::string& n) { return n.starts_with("warning"); }), entry.id + " import warnings");

    const auto solved = FEM::BEAM::solveStatic(*imported->mesh, lists.materials, lists.sections);
    if (!solved) std::printf("      %s: %s\n", entry.id.c_str(), solved.error().c_str());
    REQUIRE(solved.has_value());
    CHECK_MSG(solved->energyCheckPassed, entry.id + " energy check");

    std::array<double, 3> low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    double maxDisp = 0.0, maxStress = 0.0, utilisation = 0.0;
    for (const auto& node : solved->mesh->nodes) {
      const auto& d = node.getDisplacement();
      maxDisp = std::max(maxDisp, std::hypot(d[0], d[1], d[2]));
      for (std::size_t axis = 0; axis < 3; ++axis) {
        low[axis] = std::min(low[axis], node.getLocation()[axis]);
        high[axis] = std::max(high[axis], node.getLocation()[axis]);
      }
    }
    for (const auto& element : solved->mesh->elements) {
      maxStress = std::max(maxStress, element.stress.maxVonMises);
      utilisation = std::max(utilisation, element.stress.maxVonMises / lists.materials[element.materialID].getYieldTensile());
    }
    const double extent = std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2]});

    // Mechanism probe: 1 N and 1 N m on every node in every direction.
    auto probe = *imported->mesh;
    probe.nodalLoads.clear();
    probe.distributedLoads.clear();
    probe.gravity = {0.0, 0.0, 0.0};
    for (std::uint32_t n = 0; n < probe.nodes.size(); ++n) probe.nodalLoads.push_back({n, {1.0, 0.7, 0.4}, {0.3, 0.6, 0.9}});
    const auto probed = FEM::BEAM::solveStatic(probe, lists.materials, lists.sections);
    double probeDisp = 0.0;
    if (probed) {
      for (const auto& node : probed->mesh->nodes) probeDisp = std::max(probeDisp, std::hypot(node.getDisplacement()[0], node.getDisplacement()[1], node.getDisplacement()[2]));
    }
    std::printf("      %-30s %4zu nodes %4zu elements  max disp %9.2f mm (L/%.0f)  von Mises %6.1f MPa (%3.0f %%)  probe %.1e m\n",
                entry.id.c_str(), solved->mesh->nodes.size(), solved->mesh->elements.size(), maxDisp * 1e3, extent / maxDisp,
                maxStress / 1e6, utilisation * 100.0, probeDisp);
    // A mechanism answers 1 N with displacements far beyond the model (1e7 m and more); a
    // flexible but sound structure stays well inside it.
    CHECK_MSG(probed.has_value() && probeDisp < 0.1 * extent, entry.id + " is a mechanism");
    CHECK_MSG(std::isfinite(maxDisp) && maxDisp > 0.0 && maxDisp < extent / 10.0, entry.id + " deflection");
    CHECK_MSG(utilisation < 1.0, entry.id + " exceeds the yield strength");

    // The solved file has the same results.
    const auto results = IO::readMesh(FEM::BEAM::LIBRARY::solvedFile(beamLibraryDir(), entry));
    REQUIRE(results.has_value());
    const auto withResults = FEM::BEAM::ADAPTER::toMeshData(*results, lists.materials, lists.sections);
    REQUIRE(withResults.has_value() && withResults->mesh->hasResults);
    // Relative to the largest displacement: the file was written by another build (compiler,
    // -ffast-math, CHOLMOD or LDLT), so nodes that barely move differ in their last digits.
    bool same = true;
    for (std::size_t n = 0; n < solved->mesh->nodes.size(); ++n) {
      const auto& a = withResults->mesh->nodes[n].getDisplacement();
      const auto& b = solved->mesh->nodes[n].getDisplacement();
      for (std::size_t axis = 0; axis < 3; ++axis) same = same && std::abs(a[axis] - b[axis]) <= 1e-6 * maxDisp;
    }
    CHECK_MSG(same, entry.id + "_solved does not match the solve");
  }
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
  const auto brokenSection = [](auto change, const Formulation formulation = Formulation::EulerBernoulli) {
    auto values = kSection;
    change(values);
    const std::vector<FEM::BEAM::BeamSection> list{FEM::BEAM::BeamSection{"Broken", FEM::BEAM::GeneralSection{values}}};
    return errorOf(cantilever(formulation), list);
  };
  using P = FEM::BEAM::SectionProperties;
  CHECK(contains(brokenSection([](P& v) { v.torsionConstant = 0.0; }), "section 'Broken': A, Iy, Iz and J must be positive"));
  CHECK(contains(brokenSection([](P& v) { v.secondMomentY = -1.0; }), "must be positive"));
  CHECK(contains(brokenSection([](P& v) { v.area = std::nan(""); }), "must be positive"));
  CHECK(errorOf(cantilever(Formulation::EulerBernoulli), std::vector<FEM::BEAM::BeamSection>{
    FEM::BEAM::BeamSection{"No shear", FEM::BEAM::GeneralSection{{0.01, 2e-5, 8e-5, 1e-5, 0.0, 0.0}}}}).empty()); // EB needs no shear area
  CHECK(contains(brokenSection([](P& v) { v.shearAreaZ = 0.0; }, Formulation::Timoshenko), "shear areas"));
  CHECK(contains(broken([](MeshData& m) { m.elements[0].sectionID = 7; }), "section that is not in the section list"));
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
  const auto cancelled = FEM::BEAM::solveStatic(mesh, materials(), sections(), source.get_token());
  CHECK(!cancelled && cancelled.error() == "cancelled");

  std::vector<float> reported;
  const auto solved = FEM::BEAM::solveStatic(mesh, materials(), sections(), {}, [&](const float f) { reported.push_back(f); });
  REQUIRE(solved.has_value() && !reported.empty());
  bool increasing = true;
  for (std::size_t i = 1; i < reported.size(); ++i) increasing = increasing && reported[i] >= reported[i - 1];
  CHECK(increasing && reported.back() == 1.0f);
  // The input is not changed by the solve.
  CHECK(mesh.nodes[1].getDisplacement() == (std::array<double, 3>{0, 0, 0}) && !mesh.hasResults);
}

int main(int argc, char** argv) {
  return anaf::TESTING::runAll(argc > 1 ? argv[1] : "");
}
