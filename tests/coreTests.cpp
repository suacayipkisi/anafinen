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

// Unit tests of the FEM core (anaf_core) alone: no bridge, no GUI, no files. Linking this
// executable against anaf_core only also proves the core stays self-contained (a CLI needs
// nothing more). Physics checks use closed-form results (Logan, 5th ed., ch. 3).

#include "testSupport.hpp"
#include <material/materialLibrary.hpp>
#include <trussEngine/trussSolver.hpp>
#include <trussEngine/trussSolver/solverPortfolio.hpp>
#include <trussProperties/element.hpp>
#include <trussProperties/meshData.hpp>
#include <trussProperties/node.hpp>
#include <truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp>

#include <Eigen/SparseCore>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <vector>

using FEM::TRUSS::MeshData;
using FEM::TRUSS::Node;
using Dir = std::array<double, 3>;

namespace {
  constexpr double kE = 200e9;   // Pa
  constexpr double kArea = 1e-4; // m^2
  constexpr double kGravity = 9.80665;

  // Index 0: massless steel (load-only tests), index 1: steel with its density (self weight).
  const std::vector<anaf::MATERIAL::Material>& materials() {
    static const std::vector<anaf::MATERIAL::Material> list{
      anaf::MATERIAL::Material{{.name = "Massless", .elasticityModulus = kE, .shearModulus = 77e9, .bulkModulus = 160e9,
                                .yieldTensileStrength = 250e6, .ultimateTensileStrength = 400e6, .density = 0.0,
                                .poissonsRatio = 0.3, .ductility = 0.2}},
      anaf::MATERIAL::Material{{.name = "Steel", .elasticityModulus = kE, .shearModulus = 77e9, .bulkModulus = 160e9,
                                .yieldTensileStrength = 250e6, .ultimateTensileStrength = 400e6, .density = 7850.0,
                                .poissonsRatio = 0.3, .ductility = 0.2}},
    };
    return list;
  }

  double dot(const Dir& a, const Dir& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

  bool near(const double actual, const double expected, const double relative) {
    return std::abs(actual - expected) <= relative * std::abs(expected);
  }

  FEM::TRUSS::RenderElement bar(const std::uint32_t a, const std::uint32_t b, const std::uint32_t material = 0) {
    return {a, b, 0.0f, false, material, kArea, false};
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

// ---- model building blocks -----------------------------------------------------------------

TEST(elementGeometryAndValidation) {
  const std::vector<Node> nodes{Node(0, 0.0, 0.0, 0.0), Node(1, 3.0, 4.0, 12.0), Node(2, 0.0, 0.0, 0.0)};
  const FEM::TRUSS::TrussElement_1D element(0, kArea, 0, 1, nodes);
  CHECK(element.getEleLength() == 13.0);
  CHECK(element.getEleCosines() == (Dir{3.0 / 13.0, 4.0 / 13.0, 12.0 / 13.0}));

  CHECK(throws([&] { FEM::TRUSS::TrussElement_1D(0, 0.0, 0, 1, nodes); }));   // area 0
  CHECK(throws([&] { FEM::TRUSS::TrussElement_1D(0, kArea, 1, 1, nodes); })); // same node twice
  CHECK(throws([&] { FEM::TRUSS::TrussElement_1D(0, kArea, 0, 9, nodes); })); // node outside the list
  CHECK(throws([&] { FEM::TRUSS::TrussElement_1D(0, kArea, 0, 2, nodes); })); // coincident nodes, length 0
}

TEST(nodeSupportBasis) {
  Node node(0, 0.0, 0.0, 0.0);
  CHECK(!node.isSupported() && node.getAllowedMotionDirections().size() == 3);
  node.setMovable({true, false, true});
  CHECK(node.isSupported() && !node.hasInclinedSupport());
  CHECK(node.getAllowedMotionDirections() == (std::vector<Dir>{{1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}));
  node.setMovable({false, false, false});
  CHECK(node.getAllowedMotionDirections().empty());

  // Arbitrary directions are orthonormalized; a rail along (1, 1, 0) fixes x and y as a summary.
  node.setAllowedMotionDirections({{2.0, 2.0, 0.0}});
  REQUIRE(node.getAllowedMotionDirections().size() == 1);
  const auto& rail = node.getAllowedMotionDirections()[0];
  CHECK(std::abs(dot(rail, rail) - 1.0) < 1e-15 && std::abs(rail[0] - rail[1]) < 1e-15);
  CHECK(node.hasInclinedSupport() && node.getMovable() == (std::array<bool, 3>{false, false, false}));
}

TEST(supportDirectionsAndTheirComplement) {
  // The model editor turns restrained directions into the allowed motion and back.
  const auto roller = FEM::TRUSS::orthonormalize({{1.0, 1.0, 0.0}}); // reaction normal to a 45 deg plane
  const auto plane = FEM::TRUSS::orthogonalComplement(roller);
  REQUIRE(plane.size() == 2);
  for (const auto& d : plane) {
    CHECK(std::abs(dot(d, roller[0])) < 1e-12);
    CHECK(std::abs(dot(d, d) - 1.0) < 1e-12);
  }
  CHECK(std::abs(dot(plane[0], plane[1])) < 1e-12);
  const auto back = FEM::TRUSS::orthogonalComplement(plane);
  REQUIRE(back.size() == 1);
  CHECK(std::abs(std::abs(dot(back[0], roller[0])) - 1.0) < 1e-12);
  CHECK(FEM::TRUSS::orthogonalComplement({}).size() == 3);
  CHECK(FEM::TRUSS::orthogonalComplement(FEM::TRUSS::orthonormalize({{1, 0, 0}, {0, 2, 0}, {0, 0, 3}})).empty());
  CHECK(throws([] { (void)FEM::TRUSS::orthonormalize({{1.0, 2.0, 3.0}, {2.0, 4.0, 6.0}}); })); // dependent
  CHECK(throws([] { (void)FEM::TRUSS::orthonormalize({{0.0, 0.0, 0.0}}); }));                   // zero

  // An axis-aligned restraint stays an ordinary support; a skewed one is inclined, with the
  // global axes outside the plane reported as fixed.
  Node node(0, 0.0, 0.0, 0.0);
  node.setAllowedMotionDirections(FEM::TRUSS::orthogonalComplement(FEM::TRUSS::orthonormalize({{0.0, 1.0, 0.0}})));
  CHECK(!node.hasInclinedSupport() && node.getMovable() == (std::array<bool, 3>{true, false, true}));
  node.setAllowedMotionDirections(plane);
  CHECK(node.hasInclinedSupport() && node.getMovable() == (std::array<bool, 3>{false, false, true}));
}

TEST(simpleTrussGridAndInvalidParameters) {
  // A zero area or length used to throw inside the OpenMP loops (std::terminate).
  for (const auto& [cubes, length, area] : {std::tuple{std::array<std::uint32_t, 3>{2, 1, 2}, 1.0, 0.0},
                                           {std::array<std::uint32_t, 3>{2, 1, 2}, 0.0, 8e-3},
                                           {std::array<std::uint32_t, 3>{2, 0, 2}, 1.0, 8e-3},
                                           {std::array<std::uint32_t, 3>{2, 1, 2}, std::nan(""), 8e-3}}) {
    CHECK(!FEM::TRUSS::buildSimpleTruss(cubes, length, area, 0).has_value());
  }
  const auto grid = FEM::TRUSS::buildSimpleTruss({2, 1, 2}, 1.5, 8e-3, 0);
  REQUIRE(grid.has_value());
  REQUIRE(grid->trussNodes.size() == 18);
  // x / y / z edges 12 + 9 + 12, xy / xz / yz face diagonals 12 + 16 + 12.
  CHECK(grid->trussElements.size() == 73);
  // id = i + j (nx + 1) + k (nx + 1)(ny + 1): node 17 is (2, 1, 2).
  CHECK(grid->trussNodes[17].getNodeID() == 17);
  CHECK(grid->trussNodes[17].getLocation() == (Dir{3.0, 1.5, 3.0}));
  CHECK(!grid->hasResults && grid->appliedForces.empty());
}

// ---- static solve against closed-form results ----------------------------------------------

TEST(axialBarMatchesPLoverAE) {
  // One bar along (1, 2, 2) / 3, length 3: pinned at node 0, node 1 slides along the bar.
  const Dir axis{1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};
  const double length = 3.0, load = 2.0e4;
  MeshData mesh;
  mesh.trussNodes = {Node(0, 0.0, 0.0, 0.0), Node(1, 1.0, 2.0, 2.0)};
  mesh.trussNodes[0].setMovable({false, false, false});
  mesh.trussNodes[1].setAllowedMotionDirections({axis});
  mesh.trussElements = {bar(0, 1)};
  mesh.appliedForces = {{1u, {load * axis[0], load * axis[1], load * axis[2]}}};

  const auto solved = FEM::TRUSS::solveStatic(mesh, materials());
  if (!solved) std::printf("      %s\n", solved.error().c_str());
  REQUIRE(solved.has_value());
  const double elongation = load * length / (kArea * kE);
  const auto& u = solved->mesh->trussNodes[1].getDisplacement();
  for (std::size_t i = 0; i < 3; ++i) CHECK(near(u[i], elongation * axis[i], 1e-12));
  CHECK(solved->mesh->trussNodes[0].getDisplacement() == (Dir{0.0, 0.0, 0.0}));
  CHECK(near(solved->mesh->trussElements[0].stress, load / kArea, 1e-6)); // float in the snapshot
  CHECK(solved->energyCheckPassed && solved->mesh->hasResults);
}

TEST(twoBarTrussMatchesTheHandSolution) {
  // Supports at (0,0,0) and (2,0,0), load P down at (1,1,0); z is fixed at the apex (planar).
  // Each bar carries N = -P / (2 sin 45deg); the apex moves v = -P L / (2 A E sin^2 45deg).
  MeshData mesh;
  mesh.trussNodes = {Node(0, 0.0, 0.0, 0.0), Node(1, 2.0, 0.0, 0.0), Node(2, 1.0, 1.0, 0.0),
                     Node(3, 5.0, 5.0, 5.0)}; // node 3: used by no bar, held fixed
  mesh.trussNodes[0].setMovable({false, false, false});
  mesh.trussNodes[1].setMovable({false, false, false});
  mesh.trussNodes[2].setMovable({true, true, false});
  mesh.trussElements = {bar(0, 2), bar(1, 2), {0u, 1u, 0.0f, false, 0u, 0.0, true}}; // wireframe edge: drawn, not solved
  const double load = 1.0e5;
  mesh.appliedForces = {{2u, {0.0, -load, 0.0}}};

  const auto solved = FEM::TRUSS::solveStatic(mesh, materials());
  if (!solved) std::printf("      %s\n", solved.error().c_str());
  REQUIRE(solved.has_value());
  const auto& result = *solved->mesh;
  const double stress = -load / (2.0 * std::sqrt(0.5)) / kArea;
  CHECK(near(result.trussElements[0].stress, stress, 1e-6));
  CHECK(near(result.trussElements[1].stress, stress, 1e-6));
  CHECK(result.trussElements[2].stress == 0.0f);
  const double v = -load * std::sqrt(2.0) / (2.0 * kArea * kE * 0.5);
  CHECK(near(result.trussNodes[2].getDisplacement()[1], v, 1e-12));
  CHECK(std::abs(result.trussNodes[2].getDisplacement()[0]) < 1e-12 * std::abs(v)); // symmetry
  CHECK(result.trussNodes[3].getDisplacement() == (Dir{0.0, 0.0, 0.0}));
  CHECK(solved->energyCheckPassed);
}

TEST(indeterminateThreeBarTruss) {
  // Three bars from the ceiling to one node D (Logan-style statically indeterminate truss):
  // a vertical bar of length L and two bars at +-theta from it. With equal E A,
  // F_vertical = P / (1 + 2 cos^3 theta), F_inclined = P cos^2 theta / (1 + 2 cos^3 theta),
  // and D drops v = F_vertical L / (E A).
  const double length = 2.0, theta = std::numbers::pi / 6.0, load = 5.0e4;
  const double offset = length * std::tan(theta);
  MeshData mesh;
  mesh.trussNodes = {Node(0, -offset, length, 0.0), Node(1, 0.0, length, 0.0), Node(2, offset, length, 0.0),
                     Node(3, 0.0, 0.0, 0.0)};
  for (std::size_t i = 0; i < 3; ++i) mesh.trussNodes[i].setMovable({false, false, false});
  mesh.trussNodes[3].setMovable({true, true, false});
  mesh.trussElements = {bar(0, 3), bar(1, 3), bar(2, 3)};
  mesh.appliedForces = {{3u, {0.0, -load, 0.0}}};

  const auto solved = FEM::TRUSS::solveStatic(mesh, materials());
  REQUIRE(solved.has_value());
  const double c = std::cos(theta);
  const double vertical = load / (1.0 + 2.0 * c * c * c);
  const double inclined = load * c * c / (1.0 + 2.0 * c * c * c);
  const auto& bars = solved->mesh->trussElements;
  CHECK(near(bars[1].stress, vertical / kArea, 1e-6));
  CHECK(near(bars[0].stress, inclined / kArea, 1e-6));
  CHECK(near(bars[2].stress, inclined / kArea, 1e-6));
  CHECK(near(solved->mesh->trussNodes[3].getDisplacement()[1], -vertical * length / (kE * kArea), 1e-12));
  CHECK(solved->energyCheckPassed);
}

TEST(hangingBarUnderItsOwnWeight) {
  // A vertical bar hangs from a pin; the bottom node slides vertically. Self weight W = rho A g L
  // is lumped half to each node, so the bottom drops W / 2 * L / (E A) = rho g L^2 / (2 E).
  // The reported stress is the envelope: FE axial stress rho g L / 2 plus the distributed part
  // rho g L / 2, i.e. rho g L (tension) at the pin. Both node orders of the bar, so each end's
  // share of the weight is checked.
  const double length = 10.0;
  const double rho = materials()[1].getDensity();
  for (const auto& element : {bar(0, 1, 1), bar(1, 0, 1)}) {
    MeshData mesh;
    mesh.trussNodes = {Node(0, 0.0, length, 0.0), Node(1, 0.0, 0.0, 0.0)};
    mesh.trussNodes[0].setMovable({false, false, false});
    mesh.trussNodes[1].setMovable({false, true, false});
    mesh.trussElements = {element};

    const auto solved = FEM::TRUSS::solveStatic(mesh, materials());
    REQUIRE(solved.has_value());
    CHECK(near(solved->mesh->trussNodes[1].getDisplacement()[1], -rho * kGravity * length * length / (2.0 * kE), 1e-12));
    CHECK(near(solved->mesh->trussElements[0].stress, rho * kGravity * length, 1e-6));
    CHECK(solved->energyCheckPassed);
  }
}

TEST(inclinedRailCarriesTheLoadAlongItself) {
  // Node 1 slides on a rail r = (1, -1, 0) / sqrt 2 and is tied back by a bar along x
  // (k = E A / L, L = 1). A load P along r moves it s r: the bar stretches s r_x, its force
  // k s r_x projected on r is k s / 2 = P, so s = 2 P / k.
  const Dir rail{std::sqrt(0.5), -std::sqrt(0.5), 0.0};
  MeshData mesh;
  mesh.trussNodes = {Node(0, 0.0, 0.0, 0.0), Node(1, 1.0, 0.0, 0.0)};
  mesh.trussNodes[0].setMovable({false, false, false});
  mesh.trussNodes[1].setAllowedMotionDirections({rail});
  mesh.trussElements = {bar(0, 1)};
  const double load = 1.0e4;
  mesh.appliedForces = {{1u, {load * rail[0], load * rail[1], 0.0}}};

  const auto solved = FEM::TRUSS::solveStatic(mesh, materials());
  REQUIRE(solved.has_value());
  const double s = 2.0 * load / (kE * kArea);
  const auto& u = solved->mesh->trussNodes[1].getDisplacement();
  CHECK(near(u[0], s * rail[0], 1e-12) && near(u[1], s * rail[1], 1e-12) && u[2] == 0.0);
  CHECK(near(solved->mesh->trussElements[0].stress, kE * s * rail[0], 1e-6));
  CHECK(solved->energyCheckPassed);
}

// ---- failures, cancellation, progress ------------------------------------------------------

TEST(unsolvableModelsAreReported) {
  MeshData mesh;
  mesh.trussNodes = {Node(0, 0.0, 0.0, 0.0), Node(1, 1.0, 0.0, 0.0)};

  CHECK(!FEM::TRUSS::solveStatic(MeshData{}, materials()).has_value()); // no nodes
  CHECK(!FEM::TRUSS::solveStatic(mesh, materials()).has_value());       // no bars
  mesh.trussElements = {{0u, 1u, 0.0f, false, 0u, 0.0, true}};
  CHECK(!FEM::TRUSS::solveStatic(mesh, materials()).has_value());       // wireframe only
  mesh.trussElements = {{0u, 1u, 0.0f, false, 0u, 0.0, false}};
  const auto noArea = FEM::TRUSS::solveStatic(mesh, materials());
  REQUIRE(!noArea.has_value());
  CHECK(noArea.error().find("area") != std::string::npos);
  mesh.trussElements = {bar(0, 1, 999)};
  CHECK(!FEM::TRUSS::solveStatic(mesh, materials()).has_value());       // unknown material
  mesh.trussElements = {bar(0, 7)};
  CHECK(!FEM::TRUSS::solveStatic(mesh, materials()).has_value());       // missing node
  mesh.trussNodes = {Node(0, 0.0, 0.0, 0.0), Node(5, 1.0, 0.0, 0.0)};
  mesh.trussElements = {bar(0, 1)};
  CHECK(!FEM::TRUSS::solveStatic(mesh, materials()).has_value());       // ids are not positions
}

TEST(mechanismIsAnErrorNotAResult) {
  // Node 1 is free in all directions; one bar holds it only along x, so y and z are a
  // mechanism. Earlier the zero displacements of the failed solve passed the energy check.
  MeshData mesh;
  mesh.trussNodes = {Node(0, 0.0, 0.0, 0.0), Node(1, 1.0, 0.0, 0.0)};
  mesh.trussNodes[0].setMovable({false, false, false});
  mesh.trussElements = {bar(0, 1)};
  mesh.appliedForces = {{1u, {0.0, -1.0e3, 0.0}}};
  const auto solved = FEM::TRUSS::solveStatic(mesh, materials());
  REQUIRE(!solved.has_value());
  CHECK(solved.error().find("stiffness solve failed") != std::string::npos);
}

TEST(cancelledSolveAndProgress) {
  auto grid = FEM::TRUSS::buildSimpleTruss({4, 1, 4}, 1.0, kArea, 1);
  REQUIRE(grid.has_value());
  for (const std::uint32_t corner : {0u, 4u, 45u, 49u}) grid->trussNodes[corner].setMovable({false, false, false});

  std::stop_source stop;
  stop.request_stop();
  const auto cancelled = FEM::TRUSS::solveStatic(*grid, materials(), stop.get_token());
  REQUIRE(!cancelled.has_value());
  CHECK(cancelled.error() == "cancelled");

  std::vector<float> reported;
  const auto solved = FEM::TRUSS::solveStatic(*grid, materials(), {}, [&](const float fraction) { reported.push_back(fraction); });
  REQUIRE(solved.has_value());
  REQUIRE(!reported.empty());
  bool increasing = true;
  for (std::size_t i = 1; i < reported.size(); ++i) increasing = increasing && reported[i] >= reported[i - 1];
  CHECK(increasing && reported.back() == 1.0f);
  CHECK(solved->energyCheckPassed);
}

// ---- solver portfolio ------------------------------------------------------------------------

TEST(blockCgMatchesTheDirectSolver) {
  // Block-CG is only picked above 400k DOFs, so the referee never runs it in the other tests.
  // System: a chain of 200 nodes, 3 DOFs each, every DOF coupled to the same DOF of the next
  // node plus a small coupling between the axes of one node. The diagonal 2.5 exceeds the sum
  // of the off-diagonal magnitudes of every row (at most 2.2), so the matrix is SPD (with 2.0
  // it is indefinite and CG rightly stops on a non-positive curvature).
  constexpr std::uint32_t nodes = 200;
  constexpr Eigen::Index dofs = 3 * nodes;
  std::vector<Eigen::Triplet<double>> upper;
  for (Eigen::Index i = 0; i < dofs; ++i) {
    upper.emplace_back(i, i, 2.5);
    if (i + 3 < dofs) upper.emplace_back(i, i + 3, -1.0);
    if (i % 3 != 2) upper.emplace_back(i, i + 1, 0.1);
  }
  Eigen::SparseMatrix<double> matrix(dofs, dofs);
  matrix.setFromTriplets(upper.begin(), upper.end());
  Eigen::VectorXd force(dofs);
  for (Eigen::Index i = 0; i < dofs; ++i) force[i] = std::sin(0.1 * static_cast<double>(i));
  std::vector<std::int32_t> remap(dofs);
  for (Eigen::Index i = 0; i < dofs; ++i) remap[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(i);

  Eigen::VectorXd direct, iterative;
  const auto ldlt = FEM::TRUSS::SOLVER::solveSimplicialLDLT(matrix, force, direct);
  const auto cg = FEM::TRUSS::SOLVER::solveBlockCG(matrix, force, nodes, remap, {}, iterative);
  if (!ldlt.converged || !cg.converged) std::printf("      LDLT: %s, Block-CG: %s\n", ldlt.message.c_str(), cg.message.c_str());
  REQUIRE(ldlt.converged && cg.converged);
  CHECK(cg.iterations > 0 && cg.relativeResidual < 1e-8);
  CHECK((iterative - direct).norm() <= 1e-6 * direct.norm());
}

// ---- materials -------------------------------------------------------------------------------

TEST(materialValidation) {
  const auto valid = materials()[1].getProperties();
  CHECK(anaf::MATERIAL::validateMaterial(anaf::MATERIAL::Material{valid}).has_value());
  const auto refused = [&](auto change) {
    auto properties = valid;
    change(properties);
    return !anaf::MATERIAL::validateMaterial(anaf::MATERIAL::Material{properties}).has_value();
  };
  using P = anaf::MATERIAL::MaterialProperties;
  CHECK(refused([](P& p) { p.name.clear(); }));
  CHECK(refused([](P& p) { p.name = "with \"quotes\""; }));
  CHECK(refused([](P& p) { p.name = std::string(121, 'x'); }));
  CHECK(refused([](P& p) { p.elasticityModulus = 0.0; }));
  CHECK(refused([](P& p) { p.density = -1.0; }));
  CHECK(refused([](P& p) { p.ultimateTensileStrength = p.yieldTensileStrength / 2.0; }));
  CHECK(refused([](P& p) { p.poissonsRatio = 0.5; }));
  CHECK(refused([](P& p) { p.ductility = std::nan(""); }));
  CHECK(anaf::MATERIAL::sameMaterialName("Steel S355", "steel s355"));
  CHECK(!anaf::MATERIAL::sameMaterialName("Steel", "Steel "));
}

int main(int argc, char** argv) {
  return anaf::TESTING::runAll(argc > 1 ? argv[1] : "");
}
