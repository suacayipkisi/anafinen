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

// End-to-end test of the path the GUI takes for File > Export / Import:
// solve -> snapshot -> ADAPTER::toMeshModel -> file -> readMesh -> ADAPTER::toMeshData -> snapshot.

#include "testSupport.hpp"

#include <io/meshIo.hpp>
#include <material/materialLibrary.hpp>
#include <io/core/pathUtf8.hpp>
#include <directory/getExecutableDirectory.hpp>
#include <io/service/ioService.hpp>
#include <trussEngine/trussSolver.hpp>
#include <truss_1D/trussIO/trussMeshAdapter.hpp>
#include <truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp>
#include <truss_1D/trussTypes/trussLibrary.hpp>

#include <Eigen/Dense>
#include <Eigen/SparseCholesky>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>

using namespace anaf;
namespace fs = std::filesystem;

namespace {

  fs::path workDir() {
    static const fs::path dir = [] {
      auto path = fs::temp_directory_path() / std::format("anaf_truss_io_{}", std::chrono::steady_clock::now().time_since_epoch().count());
      fs::create_directories(path);
      return path;
    }();
    return dir;
  }

  // Axis supports by node id, put on the snapshot's nodes (the only place a support is stored).
  using Fixity = std::map<std::uint32_t, std::array<bool, 3>>;

  void applyFixity(BRIDGE::MeshData& mesh, const Fixity& fixity) {
    for (const auto& [id, fixed] : fixity) mesh.trussNodes[id].setMovable({!fixed[0], !fixed[1], !fixed[2]});
  }

  // Axis supports read back from the nodes.
  Fixity fixityOf(const BRIDGE::MeshData& mesh) {
    Fixity fixity;
    for (const auto& node : mesh.trussNodes) {
      const auto& movable = node.getMovable();
      if (node.isSupported()) fixity[node.getNodeID()] = {!movable[0], !movable[1], !movable[2]};
    }
    return fixity;
  }

  // Runs the one solve pipeline exactly like TRUSS_WORKER::startSolve() (both truss panels),
  // with fixity added to the supports already on the nodes.
  std::expected<std::shared_ptr<BRIDGE::MeshData>, std::string> solveImported(const BRIDGE::MeshData& source,
                                                                              const Fixity& fixity = {}) {
    auto& bridge = BRIDGE::buildBridge();
    const auto materials = bridge.allMaterials;
    BRIDGE::MeshData mesh = source;
    applyFixity(mesh, fixity);
    std::stop_source stop;
    FEM::TRUSS::Truss_Imported_or_Entered solver;
    if (auto ready = solver.setModel(bridge, stop.get_token(), mesh, materials); !ready) {
      return std::unexpected(ready.error());
    }
    solver.setForce(bridge, stop.get_token(), mesh.appliedForces);
    solver.setContainer(bridge, stop.get_token());
    solver.calculate(bridge, stop.get_token(), materials);
    return solver.buildResultMesh(mesh, materials);
  }

  // The generated 5 x 1 x 5 grid, solved like the Simple Quadrangle panel does it.
  std::shared_ptr<BRIDGE::MeshData> solvedSnapshot(Fixity& fixity) {
    auto& bridge = BRIDGE::buildBridge();
    if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
    fixity = {{0u, {true, true, true}}, {5u, {true, true, true}}, {60u, {true, true, true}}, {65u, {false, true, false}}};
    auto grid = FEM::TRUSS::buildSimpleTruss({5, 1, 5}, 1.0, 80.0e-4, 1);
    REQUIRE(grid.has_value());
    grid->appliedForces = {{20u, {0.0, -12000.0, 0.0}}, {27u, {500.0, 0.0, -250.0}}};
    applyFixity(*grid, fixity);
    auto solved = solveImported(*grid);
    REQUIRE(solved.has_value());
    return *solved;
  }

  void compareSnapshots(const BRIDGE::MeshData& expected, const Fixity& expectedFixity,
                        const FEM::TRUSS::ADAPTER::ImportedTruss& actual, const std::string& label) {
    const auto& mesh = *actual.mesh;
    CHECK_MSG(mesh.hasResults, label);
    REQUIRE(mesh.trussNodes.size() == expected.trussNodes.size());
    bool positions = true, displacements = true;
    for (std::size_t i = 0; i < expected.trussNodes.size(); ++i) {
      positions = positions && mesh.trussNodes[i].getLocation() == expected.trussNodes[i].getLocation();
      displacements = displacements && mesh.trussNodes[i].getDisplacement() == expected.trussNodes[i].getDisplacement();
    }
    CHECK_MSG(positions, label + " positions");
    CHECK_MSG(displacements, label + " displacements");
    REQUIRE(mesh.trussElements.size() == expected.trussElements.size());
    bool elements = true;
    for (std::size_t e = 0; e < expected.trussElements.size(); ++e) {
      const auto& a = expected.trussElements[e];
      const auto& b = mesh.trussElements[e];
      elements = elements && a.node1 == b.node1 && a.node2 == b.node2 && a.stress == b.stress && a.materialID == b.materialID
        && a.crossSectionArea == b.crossSectionArea && a.isStressExceeded == b.isStressExceeded;
    }
    CHECK_MSG(elements, label + " elements");
    Fixity fixedOnly;
    for (const auto& [node, fixed] : expectedFixity) if (fixed[0] || fixed[1] || fixed[2]) fixedOnly[node] = fixed;
    CHECK_MSG(fixityOf(mesh) == fixedOnly, label + " fixity");
    REQUIRE(mesh.appliedForces.size() == expected.appliedForces.size());
    for (std::size_t f = 0; f < expected.appliedForces.size(); ++f) {
      CHECK_MSG(mesh.appliedForces[f].getAppliedNode() == expected.appliedForces[f].getAppliedNode(), label);
      CHECK_MSG(mesh.appliedForces[f].getForce() == expected.appliedForces[f].getForce(), label);
    }
  }

} // namespace end

TEST(invalidGridParametersAreRefusedBeforeBuilding) {
  // A zero area or length used to throw inside the OpenMP loops (std::terminate).
  for (const auto& [cubes, length, area] : {std::tuple{std::array<std::uint32_t, 3>{2, 1, 2}, 1.0, 0.0},
                                           {std::array<std::uint32_t, 3>{2, 1, 2}, 0.0, 8e-3},
                                           {std::array<std::uint32_t, 3>{2, 0, 2}, 1.0, 8e-3},
                                           {std::array<std::uint32_t, 3>{2, 1, 2}, std::nan(""), 8e-3}}) {
    CHECK(!FEM::TRUSS::buildSimpleTruss(cubes, length, area, 0).has_value());
  }
  const auto valid = FEM::TRUSS::buildSimpleTruss({2, 1, 2}, 1.0, 8e-3, 0);
  REQUIRE(valid.has_value());
  CHECK(valid->trussNodes.size() == 18);
  // x / y / z edges 12 + 9 + 12, xy / xz / yz face diagonals 12 + 16 + 12.
  CHECK(valid->trussElements.size() == 73);
}

TEST(solvedTrussSurvivesEveryWritableFormat) {
  Fixity fixity;
  const auto snapshot = solvedSnapshot(fixity);
  REQUIRE(BRIDGE::buildBridge().m_isValid.load());
  const auto model = FEM::TRUSS::ADAPTER::toMeshModel(*snapshot, BRIDGE::buildBridge().allMaterials);
  CHECK(model.validate().empty());

  struct Variant { const char* file; IO::WriteOptions options; };
  const Variant variants[] = {
    {"t41b.msh", {.encoding = IO::Encoding::Binary}},
    {"t41a.msh", {}},
    {"t22a.msh", {.mshVersion = IO::MshVersion::V2_2}},
    {"t22b.msh", {.encoding = IO::Encoding::Binary, .mshVersion = IO::MshVersion::V2_2}},
    {"tz.vtu", {.encoding = IO::Encoding::Binary, .compress = true}},
    {"ta.vtu", {}},
    {"t51b.vtk", {.encoding = IO::Encoding::Binary}},
    {"t42a.vtk", {.vtkVersion = IO::VtkLegacyVersion::V4_2}},
  };
  const auto& materials = BRIDGE::buildBridge().allMaterials;
  for (const auto& v : variants) {
    const auto path = workDir() / v.file;
    const auto written = IO::writeMesh(path, model, v.options);
    REQUIRE(written.has_value());
    const auto read = IO::readMesh(path);
    if (!read) std::printf("      %s\n", read.error().message.c_str());
    REQUIRE(read.has_value());
    compareSnapshots(*snapshot, fixity, FEM::TRUSS::ADAPTER::toMeshData(*read, materials), v.file);
  }
}

TEST(stepExportKeepsTrussData) {
  Fixity fixity;
  const auto snapshot = solvedSnapshot(fixity);
  const auto model = FEM::TRUSS::ADAPTER::toMeshModel(*snapshot, BRIDGE::buildBridge().allMaterials);
  const auto path = workDir() / "truss.step";
  REQUIRE(IO::writeMesh(path, model, {}).has_value());
  const auto read = IO::readMesh(path);
  REQUIRE(read.has_value());
  const auto imported = FEM::TRUSS::ADAPTER::toMeshData(*read, BRIDGE::buildBridge().allMaterials);
  // CAD meshing renumbers nodes and elements: compare by position.
  CHECK(imported.mesh->trussNodes.size() == snapshot->trussNodes.size());
  CHECK(imported.mesh->trussElements.size() == snapshot->trussElements.size());
  // Crossing X-braces share a midpoint, so compare (midpoint, stress) pairs as a multiset.
  auto stressAt = [](const BRIDGE::MeshData& mesh) {
    std::multiset<std::pair<std::array<long long, 3>, float>> byMidpoint;
    for (const auto& e : mesh.trussElements) {
      const auto& a = mesh.trussNodes[e.node1].getLocation();
      const auto& b = mesh.trussNodes[e.node2].getLocation();
      byMidpoint.emplace(std::array<long long, 3>{std::llround((a[0] + b[0]) * 5e8), std::llround((a[1] + b[1]) * 5e8), std::llround((a[2] + b[2]) * 5e8)}, e.stress);
    }
    return byMidpoint;
  };
  CHECK(stressAt(*snapshot) == stressAt(*imported.mesh));
  CHECK(fixityOf(*imported.mesh).size() == 4);
  CHECK(imported.mesh->appliedForces.size() == 2);
}

TEST(solidMeshIsShownAsWireframe) {
  IO::MeshModel model;
  for (int i = 0; i < 8; ++i) model.nodes.push_back(IO::Node{static_cast<std::uint64_t>(i + 1), {double(i & 1), double((i >> 1) & 1), double(i >> 2)}});
  auto& hexes = model.blockFor(IO::ElementType::Hex8);
  hexes.tags.push_back(1);
  hexes.entityTags.push_back(0);
  hexes.connectivity = {0, 1, 3, 2, 4, 5, 7, 6};
  const auto imported = FEM::TRUSS::ADAPTER::toMeshData(model, {});
  CHECK(imported.mesh->trussElements.size() == 12); // unique edges of one hexahedron
  CHECK(!imported.mesh->hasResults);
  CHECK(std::ranges::any_of(imported.notes, [](const std::string& n) { return n.find("wireframe") != std::string::npos; }));
}

TEST(asyncImportConvertsOffTheCallingThread) {
  Fixity fixity;
  const auto snapshot = solvedSnapshot(fixity);
  const auto path = workDir() / "async.msh";
  REQUIRE(IO::writeMesh(path, FEM::TRUSS::ADAPTER::toMeshModel(*snapshot, BRIDGE::buildBridge().allMaterials), {.encoding = IO::Encoding::Binary}).has_value());
  const auto materials = BRIDGE::buildBridge().allMaterials;
  const auto caller = std::this_thread::get_id();
  std::thread::id worker;
  IO::IoService service;
  auto task = service.runAsync<FEM::TRUSS::ADAPTER::ImportedTruss>("import", [&, path](const IO::IoContext& context)
    -> std::expected<FEM::TRUSS::ADAPTER::ImportedTruss, IO::IoError> {
    worker = std::this_thread::get_id();
    auto model = IO::readMesh(path, {}, context);
    if (!model) return std::unexpected(model.error());
    return FEM::TRUSS::ADAPTER::toMeshData(*model, materials);
  });
  REQUIRE(task->wait().has_value());
  CHECK(worker != caller);
  compareSnapshots(*snapshot, fixity, *task->wait(), "async");
}

TEST(importedTrussSolvesLikeTheGeneratedOne) {
  Fixity fixity;
  const auto generated = solvedSnapshot(fixity);

  // Through a file, as File > Import delivers it; the results are dropped before solving.
  const auto path = workDir() / "resolve.msh";
  REQUIRE(IO::writeMesh(path, FEM::TRUSS::ADAPTER::toMeshModel(*generated, BRIDGE::buildBridge().allMaterials), {}).has_value());
  const auto read = IO::readMesh(path);
  REQUIRE(read.has_value());
  auto imported = FEM::TRUSS::ADAPTER::toMeshData(*read, BRIDGE::buildBridge().allMaterials);
  for (auto& node : imported.mesh->trussNodes) node.setDisplacements({0.0, 0.0, 0.0});
  for (auto& element : imported.mesh->trussElements) element.stress = 0.0f;
  imported.mesh->hasResults = false;

  const auto solved = solveImported(*imported.mesh);
  if (!solved) std::printf("      %s\n", solved.error().c_str());
  REQUIRE(solved.has_value());
  CHECK(BRIDGE::buildBridge().m_isValid.load());
  const auto& result = **solved;
  CHECK(result.hasResults);
  REQUIRE(result.trussNodes.size() == generated->trussNodes.size());
  REQUIRE(result.trussElements.size() == generated->trussElements.size());

  double maxDisp = 0.0, dispError = 0.0;
  for (std::size_t i = 0; i < result.trussNodes.size(); ++i) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const double expected = generated->trussNodes[i].getDisplacement()[axis];
      maxDisp = std::max(maxDisp, std::abs(expected));
      dispError = std::max(dispError, std::abs(result.trussNodes[i].getDisplacement()[axis] - expected));
    }
  }
  CHECK(maxDisp > 0.0);
  CHECK(dispError <= 1e-9 * maxDisp);

  double maxStress = 0.0, stressError = 0.0;
  for (std::size_t e = 0; e < result.trussElements.size(); ++e) {
    const double expected = generated->trussElements[e].stress;
    maxStress = std::max(maxStress, std::abs(expected));
    stressError = std::max(stressError, std::abs(static_cast<double>(result.trussElements[e].stress) - expected));
  }
  CHECK(stressError <= 1e-5 * maxStress); // stresses are stored as float
}

TEST(selfBuiltTrussMatchesTheHandSolution) {
  // Two-bar truss (Logan-style): supports at (0,0,0) and (2,0,0), load P down at (1,1,0).
  // Each bar carries N = -P / (2 sin 45deg) from the load; z is fixed everywhere (planar).
  auto& bridge = BRIDGE::buildBridge();
  if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
  BRIDGE::MeshData mesh;
  mesh.trussNodes = {FEM::TRUSS::Node(0, 0.0, 0.0, 0.0), FEM::TRUSS::Node(1, 2.0, 0.0, 0.0), FEM::TRUSS::Node(2, 1.0, 1.0, 0.0),
                     FEM::TRUSS::Node(3, 5.0, 5.0, 5.0)}; // node 3: not used by any bar
  const double area = 1e-4;
  mesh.trussElements = {{0u, 2u, 0.0f, false, 0u, area, false}, {1u, 2u, 0.0f, false, 0u, area, false},
                        {0u, 1u, 0.0f, false, 0u, 0.0, true}}; // wireframe edge: drawn, not solved
  const double load = 1.0e5;
  mesh.appliedForces = {{2u, {0.0, -load, 0.0}}};
  const Fixity fixity{{0u, {true, true, true}}, {1u, {true, true, true}}, {2u, {false, false, true}}};

  const auto solved = solveImported(mesh, fixity);
  if (!solved) std::printf("      %s\n", solved.error().c_str());
  REQUIRE(solved.has_value());
  const auto& result = **solved;

  // Axial stress of each bar (self weight adds a small term; steel over 1.41 m is ~5e4 Pa).
  const double expectedStress = -load / (2.0 * std::sqrt(0.5)) / area;
  for (std::size_t e = 0; e < 2; ++e) {
    CHECK(std::abs(result.trussElements[e].stress - expectedStress) < 1e-3 * std::abs(expectedStress));
  }
  CHECK(result.trussElements[2].stress == 0.0f);
  // Vertical deflection of the apex: v = P L / (2 A E sin^2 45deg), downwards.
  const double modulus = bridge.allMaterials[0].getElasticityModulus();
  const double expectedV = -load * std::sqrt(2.0) / (2.0 * area * modulus * 0.5);
  CHECK(std::abs(result.trussNodes[2].getDisplacement()[1] - expectedV) < 1e-2 * std::abs(expectedV));
  CHECK(result.trussNodes[3].getDisplacement() == (std::array<double, 3>{0.0, 0.0, 0.0}));
}

TEST(supportDirectionsAndTheirComplement) {
  // The model editor turns restrained directions into the allowed motion and back.
  using Dir = std::array<double, 3>;
  const auto dot = [](const Dir& a, const Dir& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
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

  bool threw = false;
  try {
    (void)FEM::TRUSS::orthonormalize({{1.0, 2.0, 3.0}, {2.0, 4.0, 6.0}});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  // An axis-aligned restraint stays an ordinary support; a skewed one is inclined, with the
  // global axes outside the plane reported as fixed (so the fixity map keeps the node).
  FEM::TRUSS::Node node(0, 0.0, 0.0, 0.0);
  node.setAllowedMotionDirections(FEM::TRUSS::orthogonalComplement(FEM::TRUSS::orthonormalize({{0.0, 1.0, 0.0}})));
  CHECK(!node.hasInclinedSupport() && node.getMovable() == (std::array<bool, 3>{true, false, true}));
  node.setAllowedMotionDirections(plane);
  CHECK(node.hasInclinedSupport() && node.getMovable() == (std::array<bool, 3>{false, false, true}));
}

TEST(inclinedSupportsMatchTheRotatedModel) {
  // Triangle truss in the xy plane: pin at node 0, roller along x at node 1, node 2 moves in
  // the plane. Turning the whole model about the gravity axis y leaves self weight unchanged
  // but makes the roller and the plane inclined supports; the displacements must turn with it.
  auto& bridge = BRIDGE::buildBridge();
  if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
  const double angle = 30.0 * std::numbers::pi / 180.0;
  const auto rotate = [c = std::cos(angle), s = std::sin(angle)](const std::array<double, 3>& v) {
    return std::array<double, 3>{c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]};
  };
  const std::vector<std::array<double, 3>> positions{{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {1.0, 1.0, 0.0}};
  const std::array<double, 3> load{3.0e4, -1.0e5, 0.0};
  const double area = 1e-4;

  BRIDGE::MeshData axisAligned;
  axisAligned.trussElements = {{0u, 1u, 0.0f, false, 0u, area, false}, {0u, 2u, 0.0f, false, 0u, area, false},
                               {1u, 2u, 0.0f, false, 0u, area, false}};
  BRIDGE::MeshData inclined = axisAligned;
  for (std::uint32_t i = 0; i < positions.size(); ++i) {
    const auto& p = positions[i];
    axisAligned.trussNodes.emplace_back(i, p[0], p[1], p[2]);
    const auto r = rotate(p);
    inclined.trussNodes.emplace_back(i, r[0], r[1], r[2]);
  }
  axisAligned.appliedForces = {{2u, load}};
  inclined.appliedForces = {{2u, rotate(load)}};
  const Fixity axisFixity{{0u, {true, true, true}}, {1u, {false, true, true}}, {2u, {false, false, true}}};
  for (const auto& [id, fixed] : axisFixity) axisAligned.trussNodes[id].setMovable({!fixed[0], !fixed[1], !fixed[2]});
  inclined.trussNodes[0].setMovable({false, false, false});
  inclined.trussNodes[1].setAllowedMotionDirections({rotate({1.0, 0.0, 0.0})});
  inclined.trussNodes[2].setAllowedMotionDirections({rotate({1.0, 0.0, 0.0}), {0.0, 1.0, 0.0}});
  CHECK(inclined.trussNodes[1].hasInclinedSupport());
  CHECK(!axisAligned.trussNodes[1].hasInclinedSupport());

  // The inclined model goes through a file, as File > Export / Import delivers it.
  const auto path = workDir() / "inclined.msh";
  REQUIRE(IO::writeMesh(path, FEM::TRUSS::ADAPTER::toMeshModel(inclined, bridge.allMaterials), {}).has_value());
  const auto read = IO::readMesh(path);
  REQUIRE(read.has_value());
  const auto imported = FEM::TRUSS::ADAPTER::toMeshData(*read, bridge.allMaterials);
  REQUIRE(imported.mesh->trussNodes.size() == 3);
  CHECK(imported.mesh->trussNodes[1].hasInclinedSupport());
  CHECK(imported.mesh->trussNodes[2].hasInclinedSupport());

  const auto reference = solveImported(axisAligned);
  REQUIRE(reference.has_value());
  const auto solved = solveImported(*imported.mesh);
  if (!solved) std::printf("      %s\n", solved.error().c_str());
  REQUIRE(solved.has_value());
  CHECK(bridge.m_isValid.load());

  double maxDisp = 0.0, dispError = 0.0;
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const auto expected = rotate((*reference)->trussNodes[i].getDisplacement());
    const auto& actual = (*solved)->trussNodes[i].getDisplacement();
    for (std::size_t axis = 0; axis < 3; ++axis) {
      maxDisp = std::max(maxDisp, std::abs(expected[axis]));
      dispError = std::max(dispError, std::abs(actual[axis] - expected[axis]));
    }
  }
  CHECK(maxDisp > 0.0);
  CHECK(dispError <= 1e-9 * maxDisp);
  // Node 1 moves only along its inclined rail.
  const auto& railMotion = (*solved)->trussNodes[1].getDisplacement();
  const auto rail = rotate({1.0, 0.0, 0.0});
  const double along = railMotion[0] * rail[0] + railMotion[1] * rail[1] + railMotion[2] * rail[2];
  CHECK(std::abs(along) > 0.0);
  for (std::size_t axis = 0; axis < 3; ++axis) CHECK(std::abs(railMotion[axis] - along * rail[axis]) <= 1e-12 * std::abs(along));

  for (std::size_t e = 0; e < 3; ++e) {
    const double expected = (*reference)->trussElements[e].stress;
    CHECK(expected != 0.0);
    CHECK(std::abs(static_cast<double>((*solved)->trussElements[e].stress) - expected) <= 1e-5 * std::abs(expected));
  }
}

TEST(importedTrussRejectsUnsolvableModels) {
  auto& bridge = BRIDGE::buildBridge();
  if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
  BRIDGE::MeshData mesh;
  mesh.trussNodes = {FEM::TRUSS::Node(0, 0.0, 0.0, 0.0), FEM::TRUSS::Node(1, 1.0, 0.0, 0.0)};

  CHECK(!solveImported(BRIDGE::MeshData{}).has_value());          // no nodes
  CHECK(!solveImported(mesh).has_value());                        // no bars
  mesh.trussElements = {{0u, 1u, 0.0f, false, 0u, 0.0, true}};
  CHECK(!solveImported(mesh).has_value());                        // wireframe only
  mesh.trussElements = {{0u, 1u, 0.0f, false, 0u, 0.0, false}};
  const auto noArea = solveImported(mesh);
  REQUIRE(!noArea.has_value());
  CHECK(noArea.error().find("area") != std::string::npos);
  mesh.trussElements = {{0u, 1u, 0.0f, false, 999u, 1e-4, false}};
  CHECK(!solveImported(mesh).has_value());                        // unknown material
  mesh.trussElements = {{0u, 7u, 0.0f, false, 0u, 1e-4, false}};
  CHECK(!solveImported(mesh).has_value());                        // missing node
  mesh.trussNodes = {FEM::TRUSS::Node(0, 0.0, 0.0, 0.0), FEM::TRUSS::Node(5, 1.0, 0.0, 0.0)};
  mesh.trussElements = {{0u, 1u, 0.0f, false, 0u, 1e-4, false}};
  CHECK(!solveImported(mesh).has_value());                        // ids are not positions
}

TEST(resetModelLeavesNothingBehind) {
  auto& bridge = BRIDGE::buildBridge();
  auto mesh = std::make_shared<BRIDGE::MeshData>();
  mesh->trussNodes = {FEM::TRUSS::Node(0, 0.0, 0.0, 0.0)};
  mesh->trussNodes[0].setMovable({false, false, false}); // the support goes with the snapshot
  {
    std::lock_guard lock(bridge.dataMutex);
    bridge.activeMesh = mesh;
    bridge.selectedNodeId = 0u;
    bridge.hasTrussPreview = true;
  }
  bridge.m_objectType = BRIDGE::ObjectType::truss_SQPT;
  const auto generation = bridge.modelGeneration.load();
  const auto version = bridge.dataVersion.load();

  bridge.resetModel(BRIDGE::ObjectType::truss_imported_or_entered);
  CHECK(bridge.m_objectType.load() == BRIDGE::ObjectType::truss_imported_or_entered);
  CHECK(bridge.activeMesh == nullptr);
  CHECK(bridge.selectedNodeId == std::numeric_limits<std::uint32_t>::max());
  CHECK(!bridge.hasTrussPreview);
  CHECK(!bridge.m_isRunning.load() && !bridge.m_isGeneratingPreview.load());
  CHECK(bridge.modelGeneration.load() != generation); // workers started before cannot publish
  CHECK(bridge.dataVersion.load() != version);        // the viewport redraws the empty scene
  bridge.m_objectType = BRIDGE::ObjectType::no_type;
}

namespace {
  fs::path libraryDir() {
    return fs::path(IO::pathFromUtf8(MAIN_DIR)) / "assets" / fs::path(FEM::TRUSS::LIBRARY::kLibrarySubdir);
  }

  std::string readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }

  // Smallest / largest eigenvalue of the reduced stiffness matrix T^T K T, where T maps one
  // DOF per allowed motion direction of each node (supports, inclined ones too) to the global
  // DOFs. A mechanism (unstable truss) has a zero eigenvalue, so the ratio drops to round-off
  // level. Large models use the smallest / largest LDLT pivot instead: for a symmetric positive
  // definite matrix every pivot lies in [lambda_min, lambda_max], and a mechanism still gives a
  // round-off sized pivot, while a dense eigen solve of a few thousand DOFs would take minutes.
  double stiffnessConditionRatio(const BRIDGE::MeshData& mesh, std::span<const MATERIAL::Material> materials) {
    const auto dofs = static_cast<Eigen::Index>(mesh.trussNodes.size() * 3);
    std::vector<Eigen::Triplet<double>> transform;
    Eigen::Index n = 0;
    for (const auto& node : mesh.trussNodes) {
      for (const auto& direction : node.getAllowedMotionDirections()) {
        for (Eigen::Index axis = 0; axis < 3; ++axis) {
          const double factor = direction[static_cast<std::size_t>(axis)];
          if (factor != 0.0) transform.emplace_back(3 * static_cast<Eigen::Index>(node.getNodeID()) + axis, n, factor);
        }
        ++n;
      }
    }
    std::vector<Eigen::Triplet<double>> triplets;
    for (const auto& element : mesh.trussElements) {
      if (element.isWireframe) continue;
      const auto& a = mesh.trussNodes[element.node1].getLocation();
      const auto& b = mesh.trussNodes[element.node2].getLocation();
      const Eigen::Vector3d d(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
      const double length = d.norm();
      const Eigen::Vector3d c = d / length;
      const Eigen::Matrix3d block = materials[element.materialID].getElasticityModulus() * element.crossSectionArea / length * (c * c.transpose());
      const std::array<Eigen::Index, 2> base{3 * static_cast<Eigen::Index>(element.node1), 3 * static_cast<Eigen::Index>(element.node2)};
      for (std::size_t p = 0; p < 2; ++p) {
        for (std::size_t q = 0; q < 2; ++q) {
          for (Eigen::Index r = 0; r < 3; ++r) {
            for (Eigen::Index s = 0; s < 3; ++s) triplets.emplace_back(base[p] + r, base[q] + s, (p == q ? 1.0 : -1.0) * block(r, s));
          }
        }
      }
    }
    Eigen::SparseMatrix<double> global(dofs, dofs), t(dofs, n);
    global.setFromTriplets(triplets.begin(), triplets.end());
    t.setFromTriplets(transform.begin(), transform.end());
    const Eigen::SparseMatrix<double> reduced = t.transpose() * global * t;
    if (n <= 1200) {
      const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(Eigen::MatrixXd(reduced), Eigen::EigenvaluesOnly);
      return eigen.eigenvalues().minCoeff() / eigen.eigenvalues().maxCoeff();
    }
    const Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt(reduced);
    if (ldlt.info() != Eigen::Success) return 0.0;
    const Eigen::VectorXd pivots = ldlt.vectorD();
    return pivots.minCoeff() / pivots.cwiseAbs().maxCoeff();
  }
} // namespace end

TEST(builtInTrussLibraryMatchesTheGenerator) {
  // The committed files must be exactly what the generator writes (regenerate with
  // anaf_truss_library_tool after changing trussLibrary.cpp; never edit them by hand).
  const auto generated = workDir() / "library";
  const auto entries = FEM::TRUSS::LIBRARY::writeLibrary(generated);
  if (!entries) std::printf("      %s\n", entries.error().c_str());
  REQUIRE(entries.has_value());
  CHECK(entries->size() >= 10 && entries->size() <= 40);

  const auto indexFile = libraryDir() / fs::path(FEM::TRUSS::LIBRARY::kIndexFile);
  CHECK_MSG(readBytes(indexFile) == readBytes(generated / fs::path(FEM::TRUSS::LIBRARY::kIndexFile)), "index.json is stale");
  // Models are compared by content, not bytes: -ffast-math and different libm builds (GCC,
  // Clang, MinGW) may change the last bits of sin / cos in the generated coordinates.
  const auto close = [](const double a, const double b) { return std::abs(a - b) <= 1e-9 * (1.0 + std::abs(a)); };
  for (const auto& entry : *entries) {
    const auto committedPath = FEM::TRUSS::LIBRARY::modelFile(libraryDir(), entry);
    CHECK_MSG(fs::exists(committedPath), entry.id + " missing");
    const auto committed = IO::readMesh(committedPath);
    const auto fresh = IO::readMesh(FEM::TRUSS::LIBRARY::modelFile(generated, entry));
    REQUIRE(committed.has_value() && fresh.has_value());
    bool same = committed->nodes.size() == fresh->nodes.size() && committed->blocks.size() == fresh->blocks.size()
      && committed->constraints.size() == fresh->constraints.size() && committed->loads.size() == fresh->loads.size()
      && committed->sets.size() == fresh->sets.size() && committed->elementAttributes.size() == fresh->elementAttributes.size();
    for (std::size_t i = 0; same && i < fresh->nodes.size(); ++i) {
      for (std::size_t axis = 0; axis < 3; ++axis) same = same && close(committed->nodes[i].position[axis], fresh->nodes[i].position[axis]);
    }
    for (std::size_t b = 0; same && b < fresh->blocks.size(); ++b) {
      same = committed->blocks[b].type == fresh->blocks[b].type && committed->blocks[b].connectivity == fresh->blocks[b].connectivity;
    }
    for (const auto& [name, values] : fresh->elementAttributes) {
      const auto it = committed->elementAttributes.find(name);
      same = same && it != committed->elementAttributes.end() && it->second.size() == values.size();
      for (std::size_t e = 0; same && e < values.size(); ++e) same = close(it->second[e], values[e]);
    }
    for (std::size_t c = 0; same && c < fresh->constraints.size(); ++c) {
      same = committed->constraints[c].node == fresh->constraints[c].node && committed->constraints[c].fixed == fresh->constraints[c].fixed;
    }
    for (std::size_t l = 0; same && l < fresh->loads.size(); ++l) {
      same = committed->loads[l].node == fresh->loads[l].node;
      for (std::size_t axis = 0; same && axis < 3; ++axis) same = close(committed->loads[l].force[axis], fresh->loads[l].force[axis]);
    }
    for (std::size_t s = 0; same && s < fresh->sets.size(); ++s) {
      same = committed->sets[s].name == fresh->sets[s].name && committed->sets[s].members == fresh->sets[s].members;
    }
    CHECK_MSG(same, entry.id + " is stale: regenerate with anaf_truss_library_tool");
  }
  const auto index = FEM::TRUSS::LIBRARY::loadIndex(indexFile);
  REQUIRE(index.has_value());
  CHECK(index->size() == entries->size());
  std::set<std::string> categories;
  for (const auto& entry : *index) categories.insert(entry.category);
  CHECK(categories == (std::set<std::string>{"Aircraft", "Bridge", "Roof", "Stadium", "Tower & Platform"}));
}

TEST(builtInTrussesAreStableAndSolve) {
  auto& bridge = BRIDGE::buildBridge();
  if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
  const auto index = FEM::TRUSS::LIBRARY::loadIndex(libraryDir() / fs::path(FEM::TRUSS::LIBRARY::kIndexFile));
  REQUIRE(index.has_value());

  for (const auto& entry : *index) {
    const auto read = IO::readMesh(FEM::TRUSS::LIBRARY::modelFile(libraryDir(), entry));
    if (!read) std::printf("      %s: %s\n", entry.id.c_str(), read.error().message.c_str());
    REQUIRE(read.has_value());
    const auto imported = FEM::TRUSS::ADAPTER::toMeshData(*read, bridge.allMaterials);
    // Materials resolve by name to the built-ins; every bar has a section and is solvable.
    CHECK_MSG(std::ranges::none_of(imported.notes, [](const std::string& n) { return n.starts_with("warning"); }), entry.id);
    CHECK_MSG(std::ranges::all_of(imported.mesh->trussElements, [](const BRIDGE::RenderElement& e) {
      return !e.isWireframe && e.crossSectionArea > 0.0;
    }), entry.id);
    CHECK_MSG(!fixityOf(*imported.mesh).empty() && !imported.mesh->appliedForces.empty(), entry.id);

    const double ratio = stiffnessConditionRatio(*imported.mesh, bridge.allMaterials);
    CHECK_MSG(ratio > 1e-12, std::format("{}: mechanism (lambda_min / lambda_max = {:.3g})", entry.id, ratio));

    const auto solved = solveImported(*imported.mesh);
    if (!solved) std::printf("      %s: %s\n", entry.id.c_str(), solved.error().c_str());
    REQUIRE(solved.has_value());
    CHECK_MSG(bridge.m_isValid.load(), entry.id + " energy check");

    std::array<double, 3> low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    double maxDisp = 0.0, maxStress = 0.0, yieldRatio = 0.0;
    for (const auto& node : (*solved)->trussNodes) {
      const auto& d = node.getDisplacement();
      maxDisp = std::max(maxDisp, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
      for (std::size_t axis = 0; axis < 3; ++axis) {
        low[axis] = std::min(low[axis], node.getLocation()[axis]);
        high[axis] = std::max(high[axis], node.getLocation()[axis]);
      }
    }
    for (const auto& element : (*solved)->trussElements) {
      maxStress = std::max(maxStress, std::abs(static_cast<double>(element.stress)));
      yieldRatio = std::max(yieldRatio, std::abs(static_cast<double>(element.stress)) / bridge.allMaterials[element.materialID].getYieldTensile());
    }
    const double extent = std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2]});
    std::printf("      %-32s %4zu nodes %4zu bars  max disp %8.2f mm (L/%.0f)  max |stress| %7.1f MPa (%.0f %% of yield)  cond %.1e  energy diff %.2e J\n",
                entry.id.c_str(), imported.mesh->trussNodes.size(), imported.mesh->trussElements.size(), maxDisp * 1e3,
                extent / maxDisp, maxStress / 1e6, yieldRatio * 100.0, ratio, bridge.m_energyDiff.load());
    // Ready-made examples are designed to be reasonable: elastic and stiff (deflection below L/250).
    CHECK_MSG(std::isfinite(maxDisp) && maxDisp > 0.0 && maxDisp < extent / 250.0, entry.id + " deflection");
    CHECK_MSG(yieldRatio < 1.0, entry.id + " yields");
  }
}

namespace {
  MATERIAL::Material renamedCopy(const MATERIAL::Material& m, std::string name) {
    return {false, std::move(name), m.getElasticityModulus(), m.getShearModulus(), m.getBulkModulus(),
            m.getYieldTensile(), m.getUltTensile(), m.getYoungModulus(), m.getDensity(), m.getPoisson(),
            m.getDuctility(), 0u};
  }
} // namespace end

TEST(materialLibraryFileIsLoadedWithStableIds) {
  auto& bridge = BRIDGE::buildBridge();
  if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
  REQUIRE(bridge.allMaterials.size() >= 2);
  for (std::uint32_t i = 0; i < bridge.allMaterials.size(); ++i) {
    CHECK(bridge.allMaterials[i].getIsBuiltin());
    CHECK(bridge.allMaterials[i].getMaterialID() == i);
  }
  // Saved models store these indices: steel 0, aluminum 1 as in anafinen <= 0.1.2.
  CHECK(bridge.allMaterials[0].getElasticityModulus() == 205.0e9);
  CHECK(bridge.allMaterials[1].getYieldTensile() == 276.0e6);

  const auto gap = workDir() / "materials_gap.json";
  std::ofstream(gap) << R"({"materials": [{"id": 1, "name": "X", "elasticityModulus": 1e9, "shearModulus": 1e9,
    "bulkModulus": 1e9, "yieldTensileStrength": 1e6, "ultimateTensileStrength": 2e6, "density": 1000,
    "poissonsRatio": 0.3, "ductility": 0.1}]})";
  CHECK(!MATERIAL::loadMaterialLibrary(gap).has_value());
  CHECK(!MATERIAL::loadMaterialLibrary(workDir() / "missing.json").has_value());
}

TEST(userMaterialsAreAddedAndRemovedWithoutBreakingIndices) {
  auto& bridge = BRIDGE::buildBridge();
  if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
  const auto builtinCount = static_cast<std::uint32_t>(bridge.allMaterials.size());
  const auto steel = bridge.allMaterials[0];

  const auto a = bridge.addUserMaterial(renamedCopy(steel, "Test A"));
  const auto b = bridge.addUserMaterial(renamedCopy(steel, "Test B"));
  REQUIRE(a.has_value() && b.has_value());
  CHECK(*a != *b && *a >= builtinCount);
  CHECK(!bridge.addUserMaterial(renamedCopy(steel, "test a")).has_value()); // duplicate name
  CHECK(!bridge.addUserMaterial(renamedCopy(steel, "")).has_value());
  CHECK(!bridge.removeUserMaterial(steel.getMaterialID()).has_value()); // built-in

  auto mesh = std::make_shared<BRIDGE::MeshData>();
  mesh->trussElements.push_back({0u, 1u, 0.0f, false, builtinCount + 1, 1.0, false}); // uses B
  bridge.activeMesh = mesh;
  CHECK(!bridge.removeUserMaterial(*b).has_value()); // in use
  REQUIRE(bridge.removeUserMaterial(*a).has_value());
  CHECK(bridge.activeMesh->trussElements[0].materialID == builtinCount); // shifted onto B
  CHECK(bridge.allMaterials[builtinCount].getMaterialID() == *b);

  bridge.activeMesh = nullptr;
  REQUIRE(bridge.removeUserMaterial(*b).has_value());
  CHECK(bridge.allMaterials.size() == builtinCount);
}

TEST(userMaterialsArePersistedOutsideTheAssets) {
  const auto steel = [] {
    auto& bridge = BRIDGE::buildBridge();
    if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
    return bridge.allMaterials[0];
  }();
  const auto path = workDir() / "config" / "userMaterials.json";

  auto saved = renamedCopy(steel, "Persisted");
  REQUIRE(MATERIAL::saveUserMaterialFile(path, std::vector{steel, saved}).has_value()); // built-in is skipped
  const auto loaded = MATERIAL::loadUserMaterialFile(path);
  REQUIRE(loaded.has_value() && loaded->size() == 1);
  CHECK(!(*loaded)[0].getIsBuiltin());
  CHECK((*loaded)[0].getMaterialType() == "Persisted");
  CHECK((*loaded)[0].getElasticityModulus() == steel.getElasticityModulus());
  CHECK((*loaded)[0].getPoisson() == steel.getPoisson());
  CHECK((*loaded)[0].getDuctility() == steel.getDuctility());
  CHECK(!fs::exists(fs::path(path) += ".tmp"));

  // A missing file is an empty list; a broken one is an error.
  CHECK(MATERIAL::loadUserMaterialFile(workDir() / "none.json").value().empty());
  std::ofstream(workDir() / "broken.json") << "{ not json";
  CHECK(!MATERIAL::loadUserMaterialFile(workDir() / "broken.json").has_value());
}

TEST(materialsAreMatchedByNameWhenTheListChanges) {
  Fixity fixity;
  auto snapshot = solvedSnapshot(fixity);
  const auto& builtins = BRIDGE::buildBridge().allMaterials;
  REQUIRE(builtins.size() >= 2);

  // Export time: built-ins + one user material used by every other bar.
  std::vector<MATERIAL::Material> atExport(builtins.begin(), builtins.end());
  atExport.push_back(renamedCopy(builtins[0], "Copper C110 (annealed)"));
  const auto userIndex = static_cast<std::uint32_t>(atExport.size() - 1);
  auto mesh = std::make_shared<BRIDGE::MeshData>(*snapshot);
  for (std::size_t e = 0; e < mesh->trussElements.size(); e += 2) mesh->trussElements[e].materialID = userIndex;

  // Import time: a new built-in was inserted before aluminum, the user material name differs in case.
  const std::vector<MATERIAL::Material> atImport{
    builtins[0], renamedCopy(builtins[0], "New Built-in"), builtins[1], renamedCopy(builtins[0], "copper c110 (ANNEALED)")};
  const std::map<std::uint32_t, std::uint32_t> expectedIndex{{0u, 0u}, {1u, 2u}, {userIndex, 3u}};

  const auto model = FEM::TRUSS::ADAPTER::toMeshModel(*mesh, atExport);
  for (const char* file : {"names.msh", "names22.msh", "names.vtk", "names.vtu", "names.step"}) {
    const auto path = workDir() / file;
    IO::WriteOptions options;
    if (std::string_view(file) == "names22.msh") options.mshVersion = IO::MshVersion::V2_2;
    REQUIRE(IO::writeMesh(path, model, options).has_value());
    const auto read = IO::readMesh(path);
    REQUIRE(read.has_value());

    // MSH 4.1 stores elements per entity (one per material set), so compare by end nodes.
    const auto imported = FEM::TRUSS::ADAPTER::toMeshData(*read, atImport);
    REQUIRE(imported.mesh->trussElements.size() == mesh->trussElements.size());
    std::map<std::pair<std::uint32_t, std::uint32_t>, const BRIDGE::RenderElement*> byNodes;
    for (const auto& element : imported.mesh->trussElements) byNodes[{element.node1, element.node2}] = &element;
    bool allMatched = true;
    for (const auto& element : mesh->trussElements) {
      const auto it = byNodes.find({element.node1, element.node2});
      allMatched = allMatched && it != byNodes.end()
                   && it->second->materialID == expectedIndex.at(element.materialID)
                   && it->second->stress == element.stress;
    }
    CHECK_MSG(allMatched, file);

    // Unknown name: material 0 and a warning.
    const auto missing = FEM::TRUSS::ADAPTER::toMeshData(*read, std::span(builtins));
    CHECK_MSG(std::ranges::all_of(missing.mesh->trussElements, [](const BRIDGE::RenderElement& element) { return element.materialID <= 1u; }), file);
    CHECK_MSG(std::ranges::any_of(missing.notes, [](const std::string& n) { return n.find("Copper C110 (annealed)") != std::string::npos; }), file);
  }
}

TEST(materialFilesWorkUnderNonAsciiFolders) {
  // Typical Windows profile folder of a Turkish user; UTF-8 in the source, wide on Windows.
  const auto dir = workDir() / IO::pathFromUtf8("Kullanıcı Şükrü İğ");
  const auto path = dir / "userMaterials.json";
  const auto& builtins = [] -> const std::vector<MATERIAL::Material>& {
    auto& bridge = BRIDGE::buildBridge();
    if (bridge.allMaterials.empty()) REQUIRE(bridge.setStaticInfo());
    return bridge.allMaterials;
  }();

  const auto named = renamedCopy(builtins[0], "Çelik S235 (ığüşöç)");
  REQUIRE(MATERIAL::saveUserMaterialFile(path, std::vector{named}).has_value());
  CHECK(fs::exists(path));
  const auto loaded = MATERIAL::loadUserMaterialFile(path);
  REQUIRE(loaded.has_value() && loaded->size() == 1);
  CHECK((*loaded)[0].getMaterialType() == "Çelik S235 (ığüşöç)");

  // Error messages carry the folder name as UTF-8 instead of throwing.
  const auto missing = MATERIAL::loadMaterialLibrary(dir / "none.json");
  REQUIRE(!missing.has_value());
  CHECK(missing.error().find("Şükrü") != std::string::npos);
  CHECK(IO::pathFromUtf8(IO::pathToUtf8(path)) == path);
  CHECK(!DIRECTORY::getUserConfigDirectory().empty());
}

int main(int argc, char** argv) {
  const int status = anaf::TESTING::runAll(argc > 1 ? argv[1] : "");
  if (status == 0) fs::remove_all(workDir());
  return status;
}
