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

#include "modelCompare.hpp"
#include "testSupport.hpp"

#include <io/meshIo.hpp>
#include <io/detail/modelCodec.hpp>
#include <io/service/ioService.hpp>

#include <gmsh.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

using namespace anaf::IO;
using anaf::TESTING::CompareOptions;
using anaf::TESTING::compareModels;
namespace fs = std::filesystem;

namespace {

  fs::path workDir() {
    static const fs::path dir = [] {
      auto path = fs::temp_directory_path() / std::format("anaf_io_tests_{}", std::chrono::steady_clock::now().time_since_epoch().count());
      fs::create_directories(path);
      return path;
    }();
    return dir;
  }

  std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  std::size_t countOccurrences(const std::string& text, const std::string& token) {
    std::size_t count = 0;
    for (auto pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + token.size())) ++count;
    return count;
  }

  void reportDiffs(const std::vector<std::string>& diffs, const std::string& label) {
    for (const auto& d : diffs) std::printf("      [%s] %s\n", label.c_str(), d.c_str());
    CHECK_MSG(diffs.empty(), label);
  }

  // Awkward values that only survive a round trip with shortest-exact number formatting.
  double awkward(const std::size_t i, const std::size_t salt) {
    static const double pool[] = {0.1 + 0.2, 1.0 / 3.0, -123456.789012345, 6.02214076e23, 1e-17, -0.0, 2.5, 1e300, -7.0 / 9.0, 3.141592653589793};
    return pool[(i * 7 + salt * 3) % 10] * (1.0 + static_cast<double>(i) * 1e-9);
  }

  // Every element type, non-contiguous tags, sets, multi-step fields, BCs, loads and attributes.
  MeshModel makeSampleModel() {
    MeshModel model;
    model.title = "sample model";
    constexpr std::size_t nodeTotal = 40;
    for (std::size_t i = 0; i < nodeTotal; ++i) {
      model.nodes.push_back(Node{100 + 7 * i, {awkward(i, 1), awkward(i, 2), static_cast<double>(i) * 0.1}});
    }
    std::size_t global = 0;
    for (const auto& info : allElementTypes()) {
      auto& block = model.blockFor(info.type);
      for (int k = 0; k < 2; ++k, ++global) {
        block.tags.push_back(5000 + 13 * global);
        block.entityTags.push_back(static_cast<int>(global % 3) + 1);
        for (int n = 0; n < info.nodeCount; ++n) block.connectivity.push_back(static_cast<std::uint32_t>((k * 5 + n) % nodeTotal));
      }
    }
    const std::size_t elementTotal = model.elementCount();

    EntitySet bars{"Bars", SetKind::Element, 1, 11, {}};
    EntitySet solids{"Solids", SetKind::Element, 3, 12, {}};
    EntitySet mixed{"Mixed Set", SetKind::Element, 3, -1, {}};
    std::size_t offset = 0;
    for (const auto& block : model.blocks) {
      const auto& info = elementInfo(block.type);
      for (std::size_t e = 0; e < block.size(); ++e) {
        const auto index = static_cast<std::uint32_t>(offset + e);
        if (info.dimension == 1) bars.members.push_back(index);
        if (info.dimension == 3) solids.members.push_back(index);
        if (block.type == ElementType::Line2 || block.type == ElementType::Tet4) mixed.members.push_back(index);
      }
      offset += block.size();
    }
    model.sets = {bars, solids, mixed, EntitySet{"Supports", SetKind::Node, -1, -1, {0, 1, 2}}};

    auto field = [&](const char* name, const FieldLocation location, const int components, const std::vector<double>& times) {
      Field f{name, location, components, times, {}, StepKind::Time, {}};
      const std::size_t entities = location == FieldLocation::Node ? nodeTotal : elementTotal;
      for (std::size_t s = 0; s < times.size(); ++s) {
        std::vector<double> values(entities * static_cast<std::size_t>(components));
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = awkward(i, s + components);
        f.steps.push_back(std::move(values));
      }
      return f;
    };
    model.fields = {
      field("Displacement", FieldLocation::Node, 3, {0.5, 1.0}),
      field("Stress", FieldLocation::Element, 1, {0.5, 1.0}),
      field("Temperature", FieldLocation::Node, 1, {0.0}),
      field("Strain", FieldLocation::Element, 6, {0.0}),
      field("Tensor", FieldLocation::Node, 9, {0.0}),
      field("ModeShape", FieldLocation::Node, 3, {1.5, 4.25, 1.0 / 3.0}),
      field("CaseStress", FieldLocation::Element, 1, {1.0, 2.0}),
    };
    model.fields[5].stepKind = StepKind::Mode;
    model.fields[6].stepKind = StepKind::LoadCase;
    model.fields[6].stepLabels = {"Dead load", "Wind \"+X\" gust"};
    // Beam results: mode shapes carry a rotation field next to the displacement one.
    model.fields.push_back(field(FieldName::Rotation, FieldLocation::Node, 3, {1.5, 4.25, 1.0 / 3.0}));
    model.fields.back().stepKind = StepKind::Mode;
    model.fields.push_back(field(FieldName::BeamSectionForce, FieldLocation::Element, 12, {0.5, 1.0}));
    model.globalData = {
      GlobalArray{GlobalName::NaturalFrequency, 1, {1.5, 4.25, 1.0 / 3.0}},
      GlobalArray{"Modal Mass", 2, {awkward(1, 9), awkward(2, 9), -0.0, 6.02214076e23}},
    };

    const double s = 1.0 / std::sqrt(2.0);
    model.constraints = {
      NodeConstraint{0, {true, true, true}, {}, {}, {}, {true, true, true}, {}, {}},
      NodeConstraint{1, {false, true, false}, {}, {0.0, -2.5e-3, 0.0}, "Ramp", {false, false, true}, {0.0, 0.0, 0.05}, "Ramp"},
      NodeConstraint{2, {true, true, true}, {{s, s, 0.0}}, {}, {}, {}, {}, {}},
    };
    model.loads = {
      NodalLoad{5, {0.0, -1000.5, 0.0}, {}, {50.0, 0.0, 0.0}},
      NodalLoad{7, {1e-3, 2e5, -3.25}, {}},
      NodalLoad{7, {0.0, 0.0, 12.5}, "Pulse", {0.0, -25.0, 0.0}},
      NodalLoad{8, {}, "Ramp", {10.0, 20.0, 30.0}}
    };
    model.temperatureConstraints = {TemperatureConstraint{3, 293.15, {}}, TemperatureConstraint{4, 0.0, "Heat Cycle"}};
    model.heatLoads = {HeatLoad{6, 150.0, {}}, HeatLoad{8, -1.0 / 3.0, "Heat Cycle"}};
    model.amplitudes = {
      Amplitude{"Ramp", {0.0, 1.0}, {0.0, 1.0}},
      Amplitude{"Pulse", {0.0, 0.1, 0.1, 0.2}, {0.0, 1.0, -0.5, 0.0}},  // a jump at t = 0.1
      Amplitude{"Heat Cycle", {0.0, 3600.0, 7200.0}, {1.0, awkward(3, 5), 1.0}},
    };
    std::vector<double> velocity(nodeTotal * 3), temperature(nodeTotal);
    for (std::size_t i = 0; i < velocity.size(); ++i) velocity[i] = awkward(i, 6);
    for (std::size_t i = 0; i < temperature.size(); ++i) temperature[i] = 273.15 + static_cast<double>(i);
    model.initialConditions = {InitialCondition{InitialQuantity::Velocity, 3, velocity},
                               InitialCondition{InitialQuantity::Rotation, 3, velocity},
                               InitialCondition{InitialQuantity::Temperature, 1, temperature}};
    model.damping = Damping{0.05, 2e-4, {0.02, 0.03, 1.0 / 3.0}};

    std::vector<double> material(elementTotal), area(elementTotal), thickness(elementTotal);
    for (std::size_t e = 0; e < elementTotal; ++e) {
      material[e] = static_cast<double>(e % 2);
      area[e] = 1e-4 * (1.0 + static_cast<double>(e) / 3.0);
      thickness[e] = 0.01 * static_cast<double>(e);
    }
    model.elementAttributes[Attribute::MaterialId] = material;
    model.elementAttributes[Attribute::CrossSectionArea] = area;
    model.elementAttributes["Thickness"] = thickness;
    model.elementAttributes[Attribute::HeatGeneration] = std::vector<double>(elementTotal, 5e3);

    // Beam data on the line elements: Euler-Bernoulli / Timoshenko alternating, one line element left
    // with a zero orientation (solver default). Every other element stays a bar with no orientation.
    std::vector<double> formulation(elementTotal, 0.0), secondMomentY(elementTotal), secondMomentZ(elementTotal),
                        torsion(elementTotal), shearArea(elementTotal);
    model.beamOrientation.assign(elementTotal, {});
    std::size_t element = 0;
    std::size_t lines = 0;
    for (const auto& block : model.blocks) {
      const auto& info = elementInfo(block.type);
      for (std::size_t e = 0; e < block.size(); ++e, ++element) {
        secondMomentY[element] = awkward(element, 11) * 1e-6;
        secondMomentZ[element] = awkward(element, 12) * 1e-6;
        torsion[element] = 2.0 / 3.0 * 1e-7;
        shearArea[element] = 0.0;
        if (info.dimension != 1) continue;
        formulation[element] = lines % 2 == 0 ? 1.0 : 2.0;
        if (lines % 2 == 1) shearArea[element] = 5.0 / 6.0 * area[element];
        if (lines++ == 0) continue;
        const auto& p0 = model.nodes[block.connectivity[e * static_cast<std::size_t>(info.nodeCount)]].position;
        const auto& p1 = model.nodes[block.connectivity[e * static_cast<std::size_t>(info.nodeCount) + 1]].position;
        const std::array<double, 3> axis{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const std::array<double, 3> helper{0.3, -0.7, 1.0 / 3.0};
        // v = axis x helper: perpendicular to the axis by construction.
        model.beamOrientation[element] = {axis[1] * helper[2] - axis[2] * helper[1], axis[2] * helper[0] - axis[0] * helper[2],
                                         axis[0] * helper[1] - axis[1] * helper[0]};
      }
    }
    model.elementAttributes[Attribute::ElementFormulation] = formulation;
    model.elementAttributes[Attribute::SecondMomentY] = secondMomentY;
    model.elementAttributes[Attribute::SecondMomentZ] = secondMomentZ;
    model.elementAttributes[Attribute::TorsionConstant] = torsion;
    model.elementAttributes[Attribute::ShearAreaY] = shearArea;
    model.elementAttributes[Attribute::ShearAreaZ] = shearArea;
    return model;
  }

  MeshModel mustRead(const fs::path& path, const ReadOptions& options = {}) {
    auto result = readMesh(path, options);
    if (!result) {
      anaf::TESTING::reportFailure(__FILE__, __LINE__, "read failed: " + path.string() + ": " + result.error().message);
      throw anaf::TESTING::RequireFailure{};
    }
    return std::move(*result);
  }

  void mustWrite(const fs::path& path, const MeshModel& model, const WriteOptions& options) {
    const auto result = writeMesh(path, model, options);
    if (!result) {
      anaf::TESTING::reportFailure(__FILE__, __LINE__, "write failed: " + path.string() + ": " + result.error().message);
      throw anaf::TESTING::RequireFailure{};
    }
  }

  // Prepares the global Gmsh state for fixture generation (tests are single-threaded).
  void resetGmshForFixture() {
    if (!gmsh::isInitialized()) gmsh::initialize(0, nullptr, false, false);
    gmsh::clear();
    gmsh::option::restoreDefaults();
    gmsh::option::setNumber("General.Terminal", 0);
  }

} // namespace end

// ------------------------------------------------------------------------ round trips

TEST(sampleModelIsValid) {
  const auto model = makeSampleModel();
  const auto problems = model.validate();
  for (const auto& p : problems) std::printf("      %s\n", p.c_str());
  CHECK(problems.empty());
  CHECK(model.blocks.size() == static_cast<std::size_t>(ElementType::Count));
}

TEST(mshRoundTripAllVersions) {
  const auto model = makeSampleModel();
  for (const auto version : {MshVersion::V2_2, MshVersion::V4_1}) {
    for (const auto encoding : {Encoding::Ascii, Encoding::Binary}) {
      const std::string label = std::format("msh{}_{}", version == MshVersion::V2_2 ? "22" : "41", encoding == Encoding::Ascii ? "ascii" : "binary");
      const auto path = workDir() / (label + ".msh");
      WriteOptions options;
      options.mshVersion = version;
      options.encoding = encoding;
      mustWrite(path, model, options);
      const auto back = mustRead(path);
      reportDiffs(compareModels(model, back, CompareOptions{.singleStep = false, .entityTags = false}), label);
      // Mesh written once, views appended (the old exporter wrote the file twice).
      const std::string text = readText(path);
      CHECK_MSG(countOccurrences(text, "$Nodes\n") == 1, label);
    }
  }
}

TEST(vtkLegacyRoundTripAllVersions) {
  const auto model = makeSampleModel();
  for (const auto version : {VtkLegacyVersion::V4_2, VtkLegacyVersion::V5_1}) {
    for (const auto encoding : {Encoding::Ascii, Encoding::Binary}) {
      const std::string label = std::format("vtk{}_{}", version == VtkLegacyVersion::V4_2 ? "42" : "51", encoding == Encoding::Ascii ? "ascii" : "binary");
      const auto path = workDir() / (label + ".vtk");
      WriteOptions options;
      options.vtkVersion = version;
      options.encoding = encoding;
      mustWrite(path, model, options);
      const auto back = mustRead(path);
      reportDiffs(compareModels(model, back, CompareOptions{.singleStep = true, .stepLabels = false}), label);
    }
  }
}

TEST(vtuRoundTripAllEncodings) {
  const auto model = makeSampleModel();
  struct Variant { const char* label; Encoding encoding; bool compress; };
  for (const auto& variant : {Variant{"vtu_ascii", Encoding::Ascii, false}, Variant{"vtu_binary", Encoding::Binary, false},
                              Variant{"vtu_zlib", Encoding::Binary, true}}) {
    const auto path = workDir() / (std::string(variant.label) + ".vtu");
    WriteOptions options;
    options.encoding = variant.encoding;
    options.compress = variant.compress;
    mustWrite(path, model, options);
    const auto back = mustRead(path);
    reportDiffs(compareModels(model, back, CompareOptions{.singleStep = true, .stepLabels = false}), variant.label);
  }
}

TEST(vtkSingleStepWritesTimeValue) {
  const auto model = makeSampleModel();
  for (const auto* name : {"step0.vtu", "step0.vtk"}) {
    const auto path = workDir() / name;
    WriteOptions options;
    options.timeStep = 0;
    mustWrite(path, model, options);
    CHECK_MSG(readText(path).find("TimeValue") != std::string::npos, name);
    const auto back = mustRead(path);
    const auto* displacement = back.findField("Displacement", FieldLocation::Node);
    REQUIRE(displacement != nullptr);
    CHECK_MSG(displacement->times == std::vector<double>{0.5}, name);
    CHECK_MSG(displacement->steps.front() == model.fields[0].steps.front(), name);
    CHECK_MSG(back.findGlobal("TimeValue") == nullptr, name);   // consumed into the field times
    const auto* modes = back.findField("ModeShape", FieldLocation::Node);
    REQUIRE(modes != nullptr);
    CHECK_MSG(modes->stepKind == StepKind::Mode && modes->times == model.fields[5].times, name);
    CHECK_MSG(readText(path).find("ModeShape_Mode_003") != std::string::npos, name);
  }
}

TEST(pvdRoundTripKeepsTimeHistory) {
  auto model = makeSampleModel();
  // A field whose history starts later: times are the union {0.5, 1, 2}.
  Field late{"Late", FieldLocation::Node, 1, {1.0, 2.0}, {}, StepKind::Time, {}};
  late.steps = {std::vector<double>(model.nodes.size(), 1.25), std::vector<double>(model.nodes.size(), -2.5)};
  model.fields.push_back(late);

  const auto path = workDir() / "series.pvd";
  float lastProgress = -1.0f;
  bool monotonic = true;
  IoContext context;
  context.onProgress = [&](const float fraction, std::string_view) {
    monotonic = monotonic && fraction >= lastProgress;
    lastProgress = fraction;
  };
  const auto written = writeMesh(path, model, WriteOptions{.encoding = Encoding::Binary, .compress = true}, context);
  REQUIRE(written.has_value());
  CHECK(monotonic && lastProgress == 1.0f);
  CHECK(written->extraFiles.size() == 3);
  CHECK(fs::exists(workDir() / "series" / "series_0002.vtu"));
  const std::string collection = readText(path);
  CHECK(collection.find("file=\"series/series_0000.vtu\"") != std::string::npos);
  CHECK(collection.find("timestep=\"0.5\"") != std::string::npos);
  CHECK(detectFormat(path) == FileFormat::Pvd);

  const auto back = mustRead(path);
  reportDiffs(compareModels(model, back, CompareOptions{.stepLabels = false, .singleStepTimes = false}), "pvd");

  // A single .vtu of the series is an ordinary VTK file.
  const auto middle = mustRead(workDir() / "series" / "series_0001.vtu");
  const auto* lateStep = middle.findField("Late", FieldLocation::Node);
  REQUIRE(lateStep != nullptr);
  CHECK(lateStep->times == std::vector<double>{1.0});

  IoContext cancelled;
  cancelled.isCancelled = [] { return true; };
  const auto aborted = writeMesh(workDir() / "cancelled.pvd", model, {}, cancelled);
  CHECK(!aborted.has_value() && aborted.error().code == IoError::Code::Cancelled);
}

TEST(boundaryConditionsAreStoredInEveryFormat) {
  // The round trips above compare the decoded model; this checks the arrays are really in the files.
  const auto model = makeSampleModel();
  for (const auto* name : {"bc.msh", "bc.vtk", "bc.vtu"}) {
    const auto path = workDir() / name;
    mustWrite(path, model, {});
    const std::string text = readText(path);
    const bool legacy = fs::path(name).extension() == ".vtk"; // legacy VTK escapes spaces in names as %20
    for (const std::string token : {"PrescribedDisplacement:Ramp", "NodalForce:Pulse", "PrescribedTemperature", "NodalHeat",
                                    "Initial:Velocity", "RayleighDamping", "ModalDampingRatio", "HeatGeneration",
                                    legacy ? "Amplitude:Heat%20Cycle" : "Amplitude:Heat Cycle",
                                    legacy ? "NodalHeat:Heat%20Cycle" : "NodalHeat:Heat Cycle"}) {
      CHECK_MSG(text.find(token) != std::string::npos, std::string(name) + ": " + token);
    }
  }
}

TEST(amplitudeInterpolation) {
  const Amplitude pulse{"Pulse", {0.0, 0.1, 0.1, 0.2}, {0.0, 1.0, -0.5, 0.0}};
  CHECK(pulse.factorAt(-1.0) == 0.0);   // held before the first point
  CHECK(pulse.factorAt(0.05) == 0.5);
  CHECK(pulse.factorAt(0.1) == 1.0);    // a jump takes the value reached from the left
  CHECK(std::abs(pulse.factorAt(0.15) + 0.25) < 1e-12); // (0.15 - 0.1) / 0.1 is not exactly 0.5
  CHECK(pulse.factorAt(5.0) == 0.0);    // held after the last point
  CHECK(Amplitude{}.factorAt(1.0) == 1.0);
}

TEST(validateRejectsBrokenBoundaryConditions) {
  auto model = makeSampleModel();
  model.loads.push_back(NodalLoad{0, {1.0, 0.0, 0.0}, "Missing"});
  model.amplitudes.push_back(Amplitude{"Bad \"name\"", {1.0, 0.0}, {0.0, 1.0}});
  model.initialConditions.push_back(InitialCondition{"Short", 3, {1.0}});
  const auto problems = model.validate();
  auto mentions = [&](const std::string& text) {
    return std::ranges::any_of(problems, [&](const std::string& p) { return p.find(text) != std::string::npos; });
  };
  CHECK(mentions("unknown amplitude 'Missing'"));
  CHECK(mentions("without quotes"));
  CHECK(mentions("not sorted"));
  CHECK(mentions("initial condition 'Short'"));
  CHECK(!writeMesh(workDir() / "invalid.msh", model, {}).has_value());
}

TEST(crossFormatChainPreservesModel) {
  // msh -> vtu -> vtk -> msh: everything except multi-step history survives every hop.
  auto model = makeSampleModel();
  for (auto& field : model.fields) {
    if (field.stepKind != StepKind::Time) continue; // modes and load cases survive every hop
    field.steps.erase(field.steps.begin(), field.steps.end() - 1);
    field.times = {0.0};
  }
  model.fields[6].stepLabels.clear(); // VTK formats do not store labels
  mustWrite(workDir() / "chain0.msh", model, {});
  const auto a = mustRead(workDir() / "chain0.msh");
  mustWrite(workDir() / "chain1.vtu", a, WriteOptions{.encoding = Encoding::Binary, .compress = true});
  const auto b = mustRead(workDir() / "chain1.vtu");
  mustWrite(workDir() / "chain2.vtk", b, WriteOptions{.encoding = Encoding::Binary});
  const auto c = mustRead(workDir() / "chain2.vtk");
  mustWrite(workDir() / "chain3.msh", c, WriteOptions{.encoding = Encoding::Binary});
  const auto d = mustRead(workDir() / "chain3.msh");
  reportDiffs(compareModels(model, d, CompareOptions{.entityTags = false}), "chain");
}

// ------------------------------------------------------------------------ Gmsh-written fixtures

namespace {
  // Position-based description: Gmsh renumbers tags between MSH versions and writes ASCII
  // coordinates with 16 significant digits, so tag-based exact comparison does not apply.
  using PositionKey = std::array<long long, 3>;
  PositionKey positionKey(const std::array<double, 3>& p) {
    return {std::llround(p[0] * 1e9), std::llround(p[1] * 1e9), std::llround(p[2] * 1e9)};
  }
  struct GeometricSignature {
    std::multiset<PositionKey> nodes;
    std::map<ElementType, std::multiset<std::vector<PositionKey>>> elements;
    std::map<std::string, std::size_t> setSizes;
    bool operator==(const GeometricSignature&) const = default;
  };
  GeometricSignature geometricSignature(const MeshModel& model) {
    GeometricSignature signature;
    for (const auto& node : model.nodes) signature.nodes.insert(positionKey(node.position));
    for (const auto& block : model.blocks) {
      const auto n = static_cast<std::size_t>(elementInfo(block.type).nodeCount);
      for (std::size_t e = 0; e < block.size(); ++e) {
        std::vector<PositionKey> sequence;
        for (std::size_t k = 0; k < n; ++k) sequence.push_back(positionKey(model.nodes[block.connectivity[e * n + k]].position));
        signature.elements[block.type].insert(std::move(sequence));
      }
    }
    for (const auto& set : model.sets) signature.setSizes[set.name] = set.members.size();
    return signature;
  }
} // namespace end

TEST(readsEveryMshVersionWrittenByGmsh) {
  resetGmshForFixture();
  gmsh::model::add("box");
  gmsh::model::occ::addBox(0, 0, 0, 1, 2, 3);
  gmsh::model::occ::synchronize();
  gmsh::model::addPhysicalGroup(3, {1}, 7, "Body");
  gmsh::model::addPhysicalGroup(2, {1, 2}, 8, "Faces");
  gmsh::option::setNumber("Mesh.MeshSizeMax", 0.8);
  gmsh::option::setNumber("Mesh.ElementOrder", 2);
  gmsh::model::mesh::generate(3);
  // Model-based views (nodal and element data) are appended to every file.
  std::vector<std::size_t> nodeTags;
  std::vector<double> coords, parametric;
  gmsh::model::mesh::getNodes(nodeTags, coords, parametric, -1, -1, false, false);
  std::vector<double> temperature(nodeTags.size());
  for (std::size_t i = 0; i < nodeTags.size(); ++i) temperature[i] = coords[3 * i] + 10.0 * coords[3 * i + 2];
  const int temperatureView = gmsh::view::add("Temperature");
  gmsh::view::addHomogeneousModelData(temperatureView, 0, "box", "NodeData", nodeTags, temperature, 0.5, 1);
  std::vector<int> types;
  std::vector<std::vector<std::size_t>> elementTags, elementNodes;
  gmsh::model::mesh::getElements(types, elementTags, elementNodes, 3);
  const auto& volumeTags = elementTags.at(0);
  const int stressView = gmsh::view::add("Stress");
  gmsh::view::addHomogeneousModelData(stressView, 0, "box", "ElementData", volumeTags, std::vector<double>(volumeTags.size(), 2.5), 0.0, 1);
  gmsh::option::setNumber("Mesh.SaveAll", 1);
  gmsh::option::setNumber("PostProcessing.SaveMesh", 0);

  struct Variant { const char* name; double version; int binary; };
  const Variant variants[] = {{"g_v1.msh", 1.0, 0}, {"g_v22a.msh", 2.2, 0}, {"g_v22b.msh", 2.2, 1}, {"g_v40a.msh", 4.0, 0},
                              {"g_v41a.msh", 4.1, 0}, {"g_v41b.msh", 4.1, 1}};
  for (const auto& v : variants) {
    gmsh::option::setNumber("Mesh.MshFileVersion", v.version);
    gmsh::option::setNumber("Mesh.Binary", v.binary);
    gmsh::option::setNumber("PostProcessing.Binary", v.binary);
    const auto path = (workDir() / v.name).string();
    gmsh::write(path);
    gmsh::view::write(temperatureView, path, true);
    gmsh::view::write(stressView, path, true);
  }
  gmsh::clear();

  // MSH 2.2 with physical groups honoured (only grouped elements are written).
  resetGmshForFixture();
  gmsh::model::add("groups");
  gmsh::model::occ::addBox(0, 0, 0, 1, 2, 3);
  gmsh::model::occ::synchronize();
  gmsh::model::addPhysicalGroup(3, {1}, 7, "Body");
  gmsh::model::addPhysicalGroup(2, {1, 2}, 8, "Faces");
  gmsh::option::setNumber("Mesh.MeshSizeMax", 0.8);
  gmsh::option::setNumber("Mesh.ElementOrder", 2);
  gmsh::model::mesh::generate(3);
  gmsh::option::setNumber("Mesh.MshFileVersion", 2.2);
  gmsh::write((workDir() / "g_v22_groups.msh").string());
  gmsh::clear();

  const auto reference = mustRead(workDir() / "g_v41b.msh"); // binary: exact values
  CHECK(reference.elementCount() > 0);
  CHECK(reference.findSet("Body", SetKind::Element) != nullptr);
  CHECK(reference.findSet("Faces", SetKind::Element) != nullptr);
  const auto referenceSignature = geometricSignature(reference);
  for (const auto& v : variants) {
    const auto model = mustRead(workDir() / v.name);
    auto expected = referenceSignature;
    auto actual = geometricSignature(model);
    if (v.version < 3.0) {
      // MSH 1 has no physical names; Gmsh's MSH 2 writer with Mesh.SaveAll = 1 writes physical tag 0.
      expected.setSizes.clear();
      actual.setSizes.clear();
    }
    CHECK_MSG(expected == actual, v.name);
    const auto* t = model.findField("Temperature", FieldLocation::Node);
    const auto* stress = model.findField("Stress", FieldLocation::Element);
    CHECK_MSG(t && t->steps.size() == 1 && t->times[0] == 0.5, v.name);
    if (t) {
      bool valuesMatch = true;
      for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& p = model.nodes[i].position;
        valuesMatch = valuesMatch && std::abs(t->steps[0][i] - (p[0] + 10.0 * p[2])) < 1e-12;
      }
      CHECK_MSG(valuesMatch, std::string(v.name) + " Temperature values");
    }
    CHECK_MSG(stress && std::ranges::count(stress->steps[0], 2.5) == static_cast<long>(volumeTags.size()), v.name);
  }
  const auto grouped = mustRead(workDir() / "g_v22_groups.msh");
  CHECK(geometricSignature(grouped).setSizes == referenceSignature.setSizes);
}

TEST(gmshReadsFilesWrittenByAnafinen) {
  // Gmsh itself must accept what we write. Binary MSH 4.1 is excluded: the Gmsh 4.15 build under
  // test aborts on any binary 4.1 file, including the ones it writes itself.
  const auto model = makeSampleModel();
  struct Variant { const char* name; MshVersion version; Encoding encoding; };
  for (const auto& v : {Variant{"gm_22a.msh", MshVersion::V2_2, Encoding::Ascii}, Variant{"gm_22b.msh", MshVersion::V2_2, Encoding::Binary},
                        Variant{"gm_41a.msh", MshVersion::V4_1, Encoding::Ascii}}) {
    const auto path = workDir() / v.name;
    mustWrite(path, model, WriteOptions{.encoding = v.encoding, .mshVersion = v.version});
    resetGmshForFixture();
    gmsh::open(path.string());
    std::vector<std::size_t> nodeTags;
    std::vector<double> coords, parametric;
    gmsh::model::mesh::getNodes(nodeTags, coords, parametric, -1, -1, true, false);
    std::set<std::size_t> uniqueNodes(nodeTags.begin(), nodeTags.end());
    // Gmsh keeps unreferenced nodes only in MSH 4 (MSH 2 has no entity to attach them to).
    std::set<std::uint32_t> referenced;
    for (const auto& block : model.blocks) referenced.insert(block.connectivity.begin(), block.connectivity.end());
    const std::size_t expectedNodes = v.version == MshVersion::V4_1 ? model.nodes.size() : referenced.size();
    CHECK_MSG(uniqueNodes.size() == expectedNodes, v.name);
    std::vector<int> types;
    std::vector<std::vector<std::size_t>> elementTags, elementNodes;
    gmsh::model::mesh::getElements(types, elementTags, elementNodes);
    std::set<std::size_t> uniqueElements;
    for (const auto& tags : elementTags) uniqueElements.insert(tags.begin(), tags.end());
    CHECK_MSG(uniqueElements.size() == model.elementCount(), v.name);
    gmsh::vectorpair groups;
    gmsh::model::getPhysicalGroups(groups);
    std::set<std::string> names;
    for (const auto& [dim, tag] : groups) {
      std::string name;
      gmsh::model::getPhysicalName(dim, tag, name);
      names.insert(name);
    }
    CHECK_MSG((names == std::set<std::string>{"Bars", "Solids", "Mixed Set"}), v.name);
    std::vector<int> views;
    gmsh::view::getTags(views);
    CHECK_MSG(views.size() >= model.fields.size(), v.name);
    gmsh::clear();
  }
}

TEST(highOrderNodeOrderingMatchesGmshVtkWriter) {
  // Gmsh's own VTK writer is the ground truth for Gmsh <-> VTK node permutations: an element read
  // from Gmsh's .vtk must list the same node positions, in the same order, as the .msh version.
  struct Case { const char* name; bool recombine; bool incomplete; bool extrude; };
  const Case cases[] = {
    {"tet10", false, true, false}, {"hex20", true, true, false}, {"hex27", true, false, false}, {"prism15", false, true, true},
  };
  std::set<ElementType> covered;
  for (const auto& c : cases) {
    resetGmshForFixture();
    gmsh::model::add(c.name);
    if (c.extrude) {
      gmsh::model::occ::addRectangle(0, 0, 0, 1, 1);
      gmsh::model::occ::synchronize();
      gmsh::vectorpair out;
      // Layered extrusion of a triangle mesh with recombine = true produces prisms.
      gmsh::model::occ::extrude({{2, 1}}, 0, 0, 1, out, {2}, {}, true);
      gmsh::model::occ::synchronize();
    } else {
      gmsh::model::occ::addBox(0, 0, 0, 1, 1, 1);
      gmsh::model::occ::synchronize();
      if (c.recombine) {
        gmsh::vectorpair curves;
        gmsh::model::getEntities(curves, 1);
        for (const auto& [d, t] : curves) gmsh::model::mesh::setTransfiniteCurve(t, 3);
        gmsh::vectorpair surfaces;
        gmsh::model::getEntities(surfaces, 2);
        for (const auto& [d, t] : surfaces) {
          gmsh::model::mesh::setTransfiniteSurface(t);
          gmsh::model::mesh::setRecombine(2, t);
        }
        gmsh::model::mesh::setTransfiniteVolume(1);
      }
    }
    gmsh::option::setNumber("Mesh.MeshSizeMax", 0.6);
    gmsh::option::setNumber("Mesh.ElementOrder", 2);
    gmsh::option::setNumber("Mesh.SecondOrderIncomplete", c.incomplete ? 1 : 0);
    gmsh::model::mesh::generate(3);
    gmsh::option::setNumber("Mesh.SaveAll", 1);
    const auto msh = workDir() / (std::string(c.name) + ".msh");
    const auto vtk = workDir() / (std::string(c.name) + "_gmsh.vtk");
    gmsh::write(msh.string());
    gmsh::write(vtk.string());
    gmsh::clear();

    const auto fromMsh = mustRead(msh);
    const auto fromVtk = mustRead(vtk);
    using Sequence = std::vector<std::array<double, 3>>;
    auto signatures = [](const MeshModel& model) {
      std::map<ElementType, std::multiset<Sequence>> result;
      for (const auto& block : model.blocks) {
        const int n = elementInfo(block.type).nodeCount;
        for (std::size_t e = 0; e < block.size(); ++e) {
          Sequence sequence;
          for (int k = 0; k < n; ++k) sequence.push_back(model.nodes[block.connectivity[e * n + k]].position);
          result[block.type].insert(sequence);
        }
      }
      return result;
    };
    const auto a = signatures(fromMsh);
    const auto b = signatures(fromVtk);
    for (const auto& [type, set] : a) {
      if (type == ElementType::Point1) continue;
      const auto it = b.find(type);
      CHECK_MSG(it != b.end() && it->second == set, std::format("{} {}", c.name, elementInfo(type).name));
      if (it != b.end() && it->second == set) covered.insert(type);
    }
  }
  for (const auto type : {ElementType::Tet10, ElementType::Hex20, ElementType::Hex27, ElementType::Prism15, ElementType::Tri6,
                          ElementType::Quad8, ElementType::Quad9, ElementType::Line3}) {
    CHECK_MSG(covered.contains(type), std::string(elementInfo(type).name) + " not covered by a Gmsh fixture");
  }
}

// ------------------------------------------------------------------------ CAD

TEST(stepRoundTripWithSidecar) {
  // A truss: line elements only; STEP keeps geometry, the sidecar keeps everything else.
  MeshModel model;
  for (std::size_t i = 0; i < 6; ++i) model.nodes.push_back(Node{i + 1, {0.1 * static_cast<double>(i % 3), 1.0 / 3.0 * static_cast<double>(i / 3), 0.0}});
  auto& lines = model.blockFor(ElementType::Line2);
  const std::uint32_t pairs[][2] = {{0, 1}, {1, 2}, {3, 4}, {4, 5}, {0, 3}, {1, 4}, {2, 5}, {0, 4}};
  for (std::size_t e = 0; e < std::size(pairs); ++e) {
    lines.tags.push_back(e + 1);
    lines.entityTags.push_back(0);
    lines.connectivity.insert(lines.connectivity.end(), {pairs[e][0], pairs[e][1]});
  }
  const std::size_t elements = model.elementCount();
  model.elementAttributes[Attribute::MaterialId] = std::vector<double>(elements, 1.0);
  model.elementAttributes[Attribute::CrossSectionArea] = std::vector<double>(elements, 8e-3);
  // Beam data, distinct per element so a mismatched element shows up.
  model.elementAttributes[Attribute::ElementFormulation] = std::vector<double>(elements, 2.0);
  model.elementAttributes[Attribute::SecondMomentZ].resize(elements);
  model.beamOrientation.resize(elements);
  for (std::size_t e = 0; e < elements; ++e) {
    model.elementAttributes[Attribute::SecondMomentZ][e] = awkward(e, 7) * 1e-6;
    model.beamOrientation[e] = {0.0, 0.0, 1.0 + static_cast<double>(e) / 3.0}; // the truss lies in z = 0
  }
  model.constraints = {NodeConstraint{0, {true, true, true}, {}, {}, {}, {true, true, true}, {}, {}},
                       NodeConstraint{2, {false, true, false}, {}, {}, {}, {false, true, false}, {0.0, 0.01, 0.0}, "Warm Up"}};
  model.loads = {NodalLoad{4, {0.0, -1e4, 0.0}, {}, {0.0, 0.0, 2500.0}}};
  std::vector<double> stress(elements);
  for (std::size_t e = 0; e < elements; ++e) stress[e] = (e % 2 ? -1.0 : 1.0) * awkward(e, 4);
  model.fields = {Field{FieldName::Stress, FieldLocation::Element, 1, {0.0}, {stress}, StepKind::Time, {}},
                  Field{"CaseForce", FieldLocation::Node, 1, {1.0, 2.0}, {std::vector<double>(6, 1.0), std::vector<double>(6, 2.0)},
                        StepKind::LoadCase, {"Dead load", " Snow  (drift) "}}};
  model.globalData = {GlobalArray{GlobalName::NaturalFrequency, 1, {awkward(1, 2), 12.5}}};
  model.temperatureConstraints = {TemperatureConstraint{5, 350.0, "Warm Up"}};
  model.amplitudes = {Amplitude{"Warm Up", {0.0, 60.0}, {0.0, 1.0}}};
  model.damping = Damping{0.1, 0.0, {}};
  model.sets = {EntitySet{"Chords", SetKind::Element, 1, -1, {0, 1, 2, 3}}};

  const auto path = workDir() / "truss.step";
  const auto written = writeMesh(path, model, {});
  REQUIRE(written.has_value());
  CHECK(written->extraFiles.size() == 1);
  const std::string step = readText(path);
  CHECK(step.find("ISO-10303-21") != std::string::npos);
  // OCC always labels STEP lengths in millimetres; coordinates must be scaled accordingly (0.1 m -> 100 mm).
  CHECK(step.find("SI_UNIT(.MILLI.,.METRE.)") != std::string::npos);
  CHECK(step.find("CARTESIAN_POINT('',(100.,0.,0.))") != std::string::npos);

  const auto back = mustRead(path);
  CHECK(back.nodes.size() == model.nodes.size());
  REQUIRE(back.elementCount() == elements);
  // Tags differ after CAD meshing; match by position.
  auto nodeAt = [&](const std::array<double, 3>& p) {
    for (std::uint32_t i = 0; i < back.nodes.size(); ++i) {
      const auto& q = back.nodes[i].position;
      if (std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]) < 1e-9) return i;
    }
    return UINT32_MAX;
  };
  for (const auto& c : model.constraints) {
    const auto node = nodeAt(model.nodes[c.node].position);
    const bool found = std::ranges::any_of(back.constraints, [&](const NodeConstraint& b) {
      return b.node == node && b.fixed == c.fixed && b.fixedRotation == c.fixedRotation
          && b.prescribedRotation == c.prescribedRotation && b.amplitudeRotation == c.amplitudeRotation;
    });
    CHECK_MSG(found, std::format("constraint on node {}", c.node));
  }
  REQUIRE(back.loads.size() == 1);
  CHECK(back.loads[0].node == nodeAt(model.nodes[4].position));
  CHECK(back.loads[0].force == model.loads[0].force);
  CHECK(back.loads[0].moment == model.loads[0].moment);
  const auto* backStress = back.findField(FieldName::Stress, FieldLocation::Element);
  REQUIRE(backStress != nullptr);
  std::multiset<double> expectedStress(stress.begin(), stress.end()), actualStress(backStress->steps[0].begin(), backStress->steps[0].end());
  CHECK(expectedStress == actualStress);
  CHECK(back.elementAttributes.at(Attribute::CrossSectionArea) == std::vector<double>(elements, 8e-3));
  // Beam data follows each element, and node 0 stays node 0 (the local x axis must not flip).
  REQUIRE(back.beamOrientation.size() == elements);
  REQUIRE(back.elementAttributes.contains(Attribute::SecondMomentZ));
  CHECK(back.elementAttributes.at(Attribute::ElementFormulation) == std::vector<double>(elements, 2.0));
  const auto& backLines = back.blocks.front();
  for (std::size_t e = 0; e < elements; ++e) {
    const auto first = nodeAt(model.nodes[pairs[e][0]].position);
    const auto second = nodeAt(model.nodes[pairs[e][1]].position);
    std::size_t match = elements;
    bool sameDirection = false;
    for (std::size_t b = 0; b < backLines.size(); ++b) {
      const auto n0 = backLines.connectivity[b * 2];
      const auto n1 = backLines.connectivity[b * 2 + 1];
      if ((n0 == first && n1 == second) || (n0 == second && n1 == first)) {
        match = b;
        sameDirection = n0 == first;
      }
    }
    REQUIRE(match < elements);
    CHECK_MSG(sameDirection, std::format("element {} reversed", e));
    CHECK(back.beamOrientation[match] == model.beamOrientation[e]);
    CHECK(back.elementAttributes.at(Attribute::SecondMomentZ)[match] == model.elementAttributes.at(Attribute::SecondMomentZ)[e]);
  }
  const auto* chords = back.findSet("Chords", SetKind::Element);
  CHECK(chords && chords->members.size() == 4);
  const auto* cases = back.findField("CaseForce", FieldLocation::Node);
  REQUIRE(cases != nullptr);
  CHECK(cases->stepKind == StepKind::LoadCase);
  CHECK(cases->times == model.fields[1].times);
  CHECK(cases->stepLabels == model.fields[1].stepLabels);
  const auto* frequencies = back.findGlobal(GlobalName::NaturalFrequency);
  CHECK(frequencies && frequencies->values == model.globalData[0].values);
  REQUIRE(back.temperatureConstraints.size() == 1);
  CHECK(back.temperatureConstraints[0].node == nodeAt(model.nodes[5].position));
  CHECK(back.temperatureConstraints[0].temperature == 350.0 && back.temperatureConstraints[0].amplitude == "Warm Up");
  const auto* warmUp = back.findAmplitude("Warm Up");
  CHECK(warmUp && warmUp->factors == model.amplitudes[0].factors);
  CHECK(back.damping && back.damping->rayleighAlpha == 0.1 && back.damping->modalRatios.empty());
}

TEST(cadSolidImportAllKernels) {
  resetGmshForFixture();
  gmsh::model::add("cad");
  gmsh::model::occ::addBox(0, 0, 0, 1, 1, 1);
  gmsh::model::occ::synchronize();
  for (const char* name : {"box.step", "box.iges", "box.brep"}) gmsh::write((workDir() / name).string());
  gmsh::clear();

  for (const char* name : {"box.step", "box.brep"}) {
    ReadOptions options;
    options.cadMeshDimension = 3;
    options.cadMeshSize = 0.4;
    const auto solid = mustRead(workDir() / name, options);
    CHECK_MSG(solid.blocks.size() == 1 && solid.blocks[0].type == ElementType::Tet4 && solid.blocks[0].size() > 0, name);

    options.cadKeepLowerDimensions = true;
    options.cadElementOrder = 2;
    const auto withBoundary = mustRead(workDir() / name, options);
    CHECK_MSG(withBoundary.maxDimension() == 3, name);
    const bool hasTet10 = std::ranges::any_of(withBoundary.blocks, [](const ElementBlock& b) { return b.type == ElementType::Tet10; });
    const bool hasTri6 = std::ranges::any_of(withBoundary.blocks, [](const ElementBlock& b) { return b.type == ElementType::Tri6; });
    CHECK_MSG(hasTet10 && hasTri6, name);

  }
  // OCC's IGES writer exports the faces of the solid, not the solid itself: mesh surfaces.
  {
    ReadOptions options;
    options.cadMeshDimension = 2;
    options.cadMeshSize = 0.4;
    const auto shell = mustRead(workDir() / "box.iges", options);
    CHECK(shell.blocks.size() == 1 && shell.blocks[0].type == ElementType::Tri3 && shell.blocks[0].size() > 0);
  }
  for (const char* name : {"box.step", "box.iges", "box.brep"}) {
    const auto edges = mustRead(workDir() / name); // default: one bar per CAD edge
    CHECK_MSG(edges.blocks.size() == 1 && edges.blocks[0].type == ElementType::Line2 && edges.blocks[0].size() == 12, name);
    CHECK_MSG(edges.nodes.size() == 8, name);
  }
}

// ------------------------------------------------------------------------ backward compatibility

TEST(readsFilesWrittenByAnafinen012) {
  // Legacy VTK layout of the previous exporter (FixityX/Y/Z + NodeConstraints FIELD).
  const auto vtk = workDir() / "legacy012.vtk";
  std::ofstream(vtk) << "# vtk DataFile Version 2.0\nCreated by anafinen\nASCII\nDATASET UNSTRUCTURED_GRID\n"
                        "POINTS 3 double\n0 0 0\n1 0 0\n1 1 0\n\nCELLS 2 6\n2 0 1\n2 1 2\n\nCELL_TYPES 2\n3\n3\n"
                        "\nPOINT_DATA 3\nVECTORS Displacement double\n0 0 0\n0.001 -0.002 0\n0 0 0\n"
                        "SCALARS FixityX int 1\nLOOKUP_TABLE default\n1\n0\n0\n"
                        "SCALARS FixityY int 1\nLOOKUP_TABLE default\n1\n0\n1\n"
                        "SCALARS FixityZ int 1\nLOOKUP_TABLE default\n1\n0\n0\n"
                        "FIELD NodeConstraints 2\nAllowedMotionRank 1 3 int\n0\n3\n2\n"
                        "AllowedMotionBasis 9 3 double\n0 0 0 0 0 0 0 0 0\n1 0 0 0 1 0 0 0 1\n1 0 0 0 0 1 0 0 0\n"
                        "\nCELL_DATA 2\nSCALARS Stress double 1\nLOOKUP_TABLE default\n1500000\n-2500000\n"
                        "SCALARS MaterialID double 1\nLOOKUP_TABLE default\n1\n0\n"
                        "SCALARS CrossSectionArea double 1\nLOOKUP_TABLE default\n0.008\n0.008\n";
  const auto model = mustRead(vtk);
  CHECK(model.nodes.size() == 3 && model.elementCount() == 2);
  CHECK(model.constraints.size() == 2);
  for (const auto& c : model.constraints) {
    if (c.node == 0) CHECK((c.fixed == std::array<bool, 3>{true, true, true}));
    if (c.node == 2) CHECK((c.fixed == std::array<bool, 3>{false, true, false}));
  }
  CHECK(model.elementAttributes.at(Attribute::MaterialId) == (std::vector<double>{1.0, 0.0}));
  const auto* stress = model.findField(FieldName::Stress, FieldLocation::Element);
  CHECK(stress && stress->steps[0] == (std::vector<double>{1500000.0, -2500000.0}));

  // Version 1 STEP sidecar ("NODES n / x y z dx dy dz", "ELEMENTS m / mx my mz mat area stress").
  resetGmshForFixture();
  // Same unit as the anafinen STEP writer. Without it Gmsh 4.13 labels the file in millimetres and the
  // reader (OCCTargetUnit M) scales the line to 0.002, so the coordinate-mapped sidecar never matches.
  gmsh::option::setString("Geometry.OCCTargetUnit", "M");
  gmsh::model::add("v1");
  const int p0 = gmsh::model::occ::addPoint(0, 0, 0);
  const int p1 = gmsh::model::occ::addPoint(2, 0, 0);
  gmsh::model::occ::addLine(p0, p1);
  gmsh::model::occ::synchronize();
  const auto step = workDir() / "legacy012.step";
  gmsh::write(step.string());
  gmsh::clear();
  std::ofstream(step.string() + ".anafFields") << "# anafinen auxiliary FEA data (Coordinate-Mapped)\nNODES 2\n0 0 0 0 0 0\n2 0 0 0.5 0 0\n"
                                                  "ELEMENTS 1\n1 0 0 1 0.004 12345.5\n";
  const auto fromStep = mustRead(step);
  REQUIRE(fromStep.elementCount() == 1);
  CHECK(fromStep.elementAttributes.at(Attribute::CrossSectionArea)[0] == 0.004);
  const auto* disp = fromStep.findField(FieldName::Displacement, FieldLocation::Node);
  REQUIRE(disp != nullptr);
  double total = 0.0;
  for (const double v : disp->steps[0]) total += v;
  CHECK(total == 0.5);
}

// ------------------------------------------------------------------------ errors

TEST(reportsErrorsInsteadOfThrowing) {
  auto missing = readMesh(workDir() / "does_not_exist.msh");
  CHECK(!missing && missing.error().code == IoError::Code::FileNotFound);

  const auto unknown = workDir() / "notes.txt";
  std::ofstream(unknown) << "hello";
  auto unsupported = readMesh(unknown);
  CHECK(!unsupported && unsupported.error().code == IoError::Code::UnsupportedFormat);

  const auto garbage = workDir() / "garbage.vtk";
  std::ofstream(garbage) << "# vtk DataFile Version 3.0\nbroken\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 3 double\n0 0 0\n1 1\n";
  auto parse = readMesh(garbage);
  CHECK(!parse && parse.error().code == IoError::Code::ParseError);

  const auto model = makeSampleModel();
  mustWrite(workDir() / "truncate.vtk", model, WriteOptions{.encoding = Encoding::Binary});
  std::string bytes = readText(workDir() / "truncate.vtk");
  bytes.resize(bytes.size() / 2);
  std::ofstream(workDir() / "truncated.vtk", std::ios::binary) << bytes;
  auto truncated = readMesh(workDir() / "truncated.vtk");
  CHECK(!truncated && truncated.error().code == IoError::Code::ParseError);

  auto badMsh = readMesh(workDir() / "garbage.msh", ReadOptions{.format = FileFormat::Msh});
  CHECK(!badMsh);

  MeshModel broken = model;
  broken.blocks[0].connectivity.push_back(0); // connectivity no longer matches element count
  auto invalid = writeMesh(workDir() / "invalid.vtu", broken, {});
  CHECK(!invalid && invalid.error().code == IoError::Code::InvalidModel);

  auto noFormat = writeMesh(workDir() / "model.unknown", model, {});
  CHECK(!noFormat && noFormat.error().code == IoError::Code::UnsupportedFormat);
}

// ------------------------------------------------------------------------ async service

TEST(ioServiceRunsOffTheCallingThread) {
  const auto model = std::make_shared<const MeshModel>(makeSampleModel());
  IoService service;
  auto exportTask = service.exportAsync(workDir() / "async.vtu", model, WriteOptions{.encoding = Encoding::Binary});
  auto importTask = service.importAsync(workDir() / "async.vtu");
  // Polling never blocks; the result arrives within a reasonable time.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!importTask->ready() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(exportTask->ready() && importTask->ready());
  REQUIRE(exportTask->tryResult()->has_value());
  REQUIRE(importTask->tryResult()->has_value());
  CHECK(importTask->progress() == 1.0f);
  reportDiffs(compareModels(*model, importTask->wait().value(), CompareOptions{.singleStep = true, .stepLabels = false}), "async");
}

TEST(ioServiceCancellation) {
  const auto model = std::make_shared<const MeshModel>(makeSampleModel());
  mustWrite(workDir() / "cancel_source.vtu", *model, WriteOptions{.encoding = Encoding::Binary, .compress = true});
  std::shared_ptr<IoTask<WriteReport>> queued;
  std::vector<std::shared_ptr<IoTask<MeshModel>>> pending;
  {
    IoService service;
    queued = service.exportAsync(workDir() / "cancelled.vtu", model, {});
    queued->cancel(); // cancelled before it starts (or at its first checkpoint)
    for (int i = 0; i < 20; ++i) pending.push_back(service.importAsync(workDir() / "cancel_source.vtu"));
  } // destructor: remaining jobs finish as cancelled, none is left without a result
  REQUIRE(queued->ready());
  CHECK(!queued->wait().has_value() && queued->wait().error().code == IoError::Code::Cancelled);
  for (const auto& task : pending) {
    REQUIRE(task->ready());
    const auto& result = task->wait();
    CHECK(result.has_value() || result.error().code == IoError::Code::Cancelled);
  }
}

TEST(trussModelEncodesWithoutRotationalFields) {
  MeshModel model;
  model.nodes = {Node{1, {0.0, 0.0, 0.0}}, Node{2, {1.0, 0.0, 0.0}}};
  auto& block = model.blockFor(ElementType::Line2);
  block.tags = {1};
  block.connectivity = {0, 1};
  model.constraints = {NodeConstraint{0, {true, true, true}, {}, {}, {}}};
  model.loads = {NodalLoad{1, {0.0, -100.0, 0.0}, {}}};

  // Exactly the arrays a truss model got before rotational DOFs existed.
  std::vector<std::string> names;
  for (const auto& field : anaf::IO::detail::encodeModelData(model, {})) names.push_back(field.name);
  CHECK((names == std::vector<std::string>{"Fixity", "NodalForce", "NodeTag", "ElementTag"}));
}

TEST(forceAndMomentOnSameNodeMergeOnDecode) {
  MeshModel model;
  model.nodes = {Node{1, {0.0, 0.0, 0.0}}, Node{2, {1.0, 0.0, 0.0}}};
  auto& block = model.blockFor(ElementType::Line2);
  block.tags = {1};
  block.connectivity = {0, 1};
  model.loads = {NodalLoad{1, {10.0, 20.0, 30.0}, "StepPulse", {100.0, 200.0, 300.0}}};
  model.amplitudes = {Amplitude{"StepPulse", {0.0, 1.0}, {1.0, 1.0}}};

  const auto encoded = anaf::IO::detail::encodeModelData(model, {});
  MeshModel decoded = model;
  decoded.loads.clear();
  decoded.fields = encoded;
  anaf::IO::detail::decodeModelData(decoded, {});

  REQUIRE(decoded.loads.size() == 1);
  CHECK(decoded.loads[0].node == 1);
  CHECK(decoded.loads[0].force == (std::array<double, 3>{10.0, 20.0, 30.0}));
  CHECK(decoded.loads[0].moment == (std::array<double, 3>{100.0, 200.0, 300.0}));
  CHECK(decoded.loads[0].amplitude == "StepPulse");
}

TEST(rotationOnlyConstraintAndPinSupport) {
  MeshModel model;
  model.nodes = {Node{1, {0.0, 0.0, 0.0}}, Node{2, {1.0, 0.0, 0.0}}};
  auto& block = model.blockFor(ElementType::Line2);
  block.tags = {1};
  block.connectivity = {0, 1};
  model.constraints = {
    NodeConstraint{0, {false, false, false}, {}, {}, {}, {true, true, true}, {}, {}},
    NodeConstraint{1, {true, true, false}, {}, {}, {}, {false, false, false}, {}, {}},
  };

  const auto encoded = anaf::IO::detail::encodeModelData(model, {});
  MeshModel decoded = model;
  decoded.constraints.clear();
  decoded.fields = encoded;
  anaf::IO::detail::decodeModelData(decoded, {});

  REQUIRE(decoded.constraints.size() == 2);
  auto c0 = std::ranges::find_if(decoded.constraints, [](const NodeConstraint& c) { return c.node == 0; });
  auto c1 = std::ranges::find_if(decoded.constraints, [](const NodeConstraint& c) { return c.node == 1; });
  REQUIRE(c0 != decoded.constraints.end());
  REQUIRE(c1 != decoded.constraints.end());
  CHECK(c0->fixed == (std::array<bool, 3>{false, false, false}));
  CHECK(c0->fixedRotation == (std::array<bool, 3>{true, true, true}));
  CHECK(c0->allowedMotion.empty());
  CHECK(c1->fixed == (std::array<bool, 3>{true, true, false}));
  CHECK(c1->fixedRotation == (std::array<bool, 3>{false, false, false}));
  CHECK(c1->allowedMotion.empty());
}

TEST(validateRejectsPrescribedRotationOnFreeDof) {
  MeshModel model;
  model.nodes = {Node{1, {0.0, 0.0, 0.0}}};
  model.constraints = {
    NodeConstraint{0, {}, {}, {}, {}, {false, true, false}, {0.05, 0.0, 0.0}, {}},
  };
  const auto problems = model.validate();
  CHECK(!problems.empty());
  CHECK(std::ranges::any_of(problems, [](const std::string& p) {
    return p.find("prescribed rotation about X on a free rotational DOF") != std::string::npos;
  }));
}

TEST(sectionAttributesUseThePrefixOlderReadersUnderstand) {
  MeshModel model;
  model.nodes = {Node{1, {0.0, 0.0, 0.0}}, Node{2, {1.0, 0.0, 0.0}}};
  auto& block = model.blockFor(ElementType::Line2);
  block.tags = {1};
  block.connectivity = {0, 1};
  model.elementAttributes[Attribute::CrossSectionArea] = {1e-3};
  model.elementAttributes[Attribute::SecondMomentZ] = {2e-6};
  model.elementAttributes[Attribute::ElementFormulation] = {1.0};
  model.beamOrientation = {{0.0, 1.0, 0.0}};

  std::vector<std::string> names;
  for (const auto& field : anaf::IO::detail::encodeModelData(model, {true, true, false})) names.push_back(field.name);
  // v0.1.3 decodes "Attribute:<name>" into elementAttributes; unprefixed new names would stay plain fields.
  CHECK((names == std::vector<std::string>{"CrossSectionArea", "Attribute:ElementFormulation", "Attribute:SecondMomentZ", "BeamOrientation"}));
}

TEST(validateRejectsBrokenBeamData) {
  MeshModel model;
  model.nodes = {Node{1, {0.0, 0.0, 0.0}}, Node{2, {1.0, 0.0, 0.0}}, Node{3, {0.0, 1.0, 0.0}}};
  auto& line = model.blockFor(ElementType::Line2);
  line.tags = {1, 2};
  line.connectivity = {0, 1, 0, 2};
  auto& triangle = model.blockFor(ElementType::Tri3);
  triangle.tags = {3};
  triangle.connectivity = {0, 1, 2};

  model.beamOrientation = {{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
  model.elementAttributes[Attribute::ElementFormulation] = {1.0, 3.0, 2.0};
  const auto problems = model.validate();
  auto mentions = [&](const std::string& text) {
    return std::ranges::any_of(problems, [&](const std::string& p) { return p.find(text) != std::string::npos; });
  };
  CHECK(mentions("element 1: beam orientation is parallel to the element axis"));
  CHECK(mentions("beam orientation on a non-line element"));
  CHECK(mentions("element 1: unknown element formulation 3"));
  CHECK(mentions("beam formulation on a non-line element"));
  CHECK(!mentions("element 0:"));
  CHECK(problems.size() == 4);

  model.beamOrientation.pop_back();
  CHECK(std::ranges::any_of(model.validate(), [](const std::string& p) { return p.find("beam orientation: 2 vectors for 3 elements") != std::string::npos; }));
}

TEST(formatDetection) {
  CHECK(detectFormat("a.MSH") == FileFormat::Msh);
  CHECK(detectFormat("a.vtu") == FileFormat::Vtu);
  CHECK(detectFormat("a.stp") == FileFormat::Step);
  CHECK(detectFormat("a.igs") == FileFormat::Iges);
  // Content sniffing for unknown extensions.
  const auto renamed = workDir() / "mesh.dat";
  mustWrite(renamed, makeSampleModel(), WriteOptions{.format = FileFormat::Msh});
  CHECK(detectFormat(renamed) == FileFormat::Msh);
}

int main(int argc, char** argv) {
  const std::string filter = argc > 1 ? argv[1] : "";
  const int status = anaf::TESTING::runAll(filter);
  if (status == 0) fs::remove_all(workDir());
  else std::printf("artifacts kept in %s\n", workDir().string().c_str());
  if (gmsh::isInitialized()) gmsh::finalize();
  return status;
}
