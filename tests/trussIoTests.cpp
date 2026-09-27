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
#include <io/service/ioService.hpp>
#include <trussEngine/trussSolver.hpp>
#include <truss_1D/trussIO/trussMeshAdapter.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <thread>

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

  // Builds the snapshot exactly like TrussControlPanel's solver worker does.
  std::shared_ptr<BRIDGE::MeshData> solvedSnapshot(BRIDGE::FixedDOFMap& fixity) {
    auto& bridge = BRIDGE::buildBridge();
    if (bridge.allMaterials.empty()) bridge.setStaticInfo();
    fixity = {{0u, {true, true, true}}, {5u, {true, true, true}}, {60u, {true, true, true}}, {65u, {false, true, false}}};
    const std::vector<FEM::TRUSS::ForceApplied> forces{{20u, {0.0, -12000.0, 0.0}}, {27u, {500.0, 0.0, -250.0}}};

    std::stop_source stop;
    FEM::TRUSS::Truss_SQPT solver{bridge, stop.get_token(), 5, 1, 5, 1.0, 80.0, 1};
    solver.trussSetAndSetFix_SQPT(bridge, stop.get_token(), fixity);
    solver.trussSetForce_SQRT(bridge, stop.get_token(), forces);
    solver.setContainer(bridge, stop.get_token());
    auto materials = bridge.allMaterials;
    solver.calculate(bridge, stop.get_token(), materials);

    auto mesh = std::make_shared<BRIDGE::MeshData>();
    mesh->trussNodes.assign(solver.getNodes().begin(), solver.getNodes().end());
    for (const auto& element : solver.getElements()) {
      const auto& nodes = element.getEleNodes();
      const auto& material = materials[element.getEleProperties()];
      mesh->trussElements.push_back({nodes[0], nodes[1], static_cast<float>(element.getEleStress()),
                                     std::abs(element.getEleStress()) > material.getYieldTensile(), element.getEleProperties(),
                                     element.getEleCrossSection()});
    }
    mesh->appliedForces = forces;
    mesh->hasResults = true;
    return mesh;
  }

  void compareSnapshots(const BRIDGE::MeshData& expected, const BRIDGE::FixedDOFMap& expectedFixity,
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
    std::map<std::uint32_t, std::array<bool, 3>> fixedOnly;
    for (const auto& [node, fixed] : expectedFixity) if (fixed[0] || fixed[1] || fixed[2]) fixedOnly[node] = fixed;
    using FixityMap = std::map<std::uint32_t, std::array<bool, 3>>;
    const bool sameFixity = FixityMap(actual.fixity.begin(), actual.fixity.end()) == fixedOnly;
    CHECK_MSG(sameFixity, label + " fixity");
    REQUIRE(mesh.appliedForces.size() == expected.appliedForces.size());
    for (std::size_t f = 0; f < expected.appliedForces.size(); ++f) {
      CHECK_MSG(mesh.appliedForces[f].getApliedNode() == expected.appliedForces[f].getApliedNode(), label);
      CHECK_MSG(mesh.appliedForces[f].getForce() == expected.appliedForces[f].getForce(), label);
    }
  }

} // namespace end

TEST(solvedTrussSurvivesEveryWritableFormat) {
  BRIDGE::FixedDOFMap fixity;
  const auto snapshot = solvedSnapshot(fixity);
  REQUIRE(BRIDGE::buildBridge().m_isValid.load());
  const auto model = FEM::TRUSS::ADAPTER::toMeshModel(*snapshot, fixity);
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
  BRIDGE::FixedDOFMap fixity;
  const auto snapshot = solvedSnapshot(fixity);
  const auto model = FEM::TRUSS::ADAPTER::toMeshModel(*snapshot, fixity);
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
  CHECK(imported.fixity.size() == 4);
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
  BRIDGE::FixedDOFMap fixity;
  const auto snapshot = solvedSnapshot(fixity);
  const auto path = workDir() / "async.msh";
  REQUIRE(IO::writeMesh(path, FEM::TRUSS::ADAPTER::toMeshModel(*snapshot, fixity), {.encoding = IO::Encoding::Binary}).has_value());
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

int main(int argc, char** argv) {
  const int status = anaf::TESTING::runAll(argc > 1 ? argv[1] : "");
  if (status == 0) fs::remove_all(workDir());
  return status;
}
