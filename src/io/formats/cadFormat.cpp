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

// CAD formats through the Gmsh OpenCASCADE kernel.
//
// Read:  STEP (AP203 / AP214 / AP242), IGES and BREP. The geometry is converted to metres and
//        meshed to `cadMeshDimension`. Bars (1D): coincident end nodes are merged, crossing
//        members stay unconnected. Surfaces / volumes: the geometry is fragmented first.
// Write: STEP. CAD files carry geometry only, so every node becomes a CAD vertex and every
//        line element a straight CAD edge. Everything else (BCs, loads, attributes, sets,
//        fields) goes to the sidecar `<file>.anafFields`, matched back by position on read.
//
// Sidecar version 3 adds to version 2:
//   after each "FIELD" header: "KIND <Time|Frequency|Mode|LoadCase> <0|1>", then when the flag
//                              is 1 one "LABEL <text>" line per step
//   before "END":              "GLOBAL <components> <tuples> <name>" followed by the values

#include "formats.hpp"
#include "../detail/gmshSession.hpp"
#include "../detail/modelCodec.hpp"
#include "../detail/textIo.hpp"

#include <gmsh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace anaf::IO::formats {

  namespace {

    using detail::Cursor;
    using detail::ParseFailure;
    using Point = std::array<double, 3>;

    constexpr std::string_view kSidecarSuffix = ".anafFields";
    constexpr std::string_view kSidecarMagic = "ANAFINEN_SIDECAR";
    constexpr int kSidecarVersion = 3;

    void checkCancel(const IoContext& context) {
      if (context.cancelled()) throw detail::CancelledFailure();
    }

    std::filesystem::path sidecarPath(const std::filesystem::path& cadPath) {
      return std::filesystem::path(cadPath.string() + std::string(kSidecarSuffix));
    }

    // Uniform grid hash for "same position within tolerance" lookups (O(1) per query).
    class PointIndex {
    public:
      PointIndex(const std::vector<Point>& points, const double tolerance) : m_points(points), m_tolerance(tolerance), m_cell(tolerance * 2.0) {
        m_grid.reserve(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) m_grid[key(cellOf(points[i]))].push_back(static_cast<std::uint32_t>(i));
      }

      std::optional<std::uint32_t> nearest(const Point& p) const {
        const auto c = cellOf(p);
        std::optional<std::uint32_t> best;
        double bestDistance = m_tolerance;
        for (int dx = -1; dx <= 1; ++dx) {
          for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
              const auto it = m_grid.find(key({c[0] + dx, c[1] + dy, c[2] + dz}));
              if (it == m_grid.end()) continue;
              for (const auto index : it->second) {
                const auto& q = m_points[index];
                const double d = std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
                if (d <= bestDistance) {
                  bestDistance = d;
                  best = index;
                }
              }
            }
          }
        }
        return best;
      }

    private:
      std::array<std::int64_t, 3> cellOf(const Point& p) const {
        return {static_cast<std::int64_t>(std::floor(p[0] / m_cell)), static_cast<std::int64_t>(std::floor(p[1] / m_cell)),
                static_cast<std::int64_t>(std::floor(p[2] / m_cell))};
      }
      static std::uint64_t key(const std::array<std::int64_t, 3>& c) {
        // Mix the three cell coordinates into one hash key; collisions only cost extra distance checks.
        std::uint64_t h = static_cast<std::uint64_t>(c[0]) * 0x9E3779B97F4A7C15ULL;
        h ^= static_cast<std::uint64_t>(c[1]) * 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
        h ^= static_cast<std::uint64_t>(c[2]) * 0x165667B19E3779F9ULL + (h << 6) + (h >> 2);
        return h;
      }

      const std::vector<Point>& m_points;
      double m_tolerance;
      double m_cell;
      std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_grid;
    };

    std::vector<Point> nodePositions(const MeshModel& model) {
      std::vector<Point> points(model.nodes.size());
      for (std::size_t i = 0; i < model.nodes.size(); ++i) points[i] = model.nodes[i].position;
      return points;
    }

    std::vector<Point> elementCentroids(const MeshModel& model) {
      std::vector<Point> centroids;
      centroids.reserve(model.elementCount());
      for (const auto& block : model.blocks) {
        const int count = elementInfo(block.type).nodeCount;
        for (std::size_t e = 0; e < block.size(); ++e) {
          Point c{};
          for (int k = 0; k < count; ++k) {
            const auto& p = model.nodes[block.connectivity[e * count + k]].position;
            for (int a = 0; a < 3; ++a) c[a] += p[a] / count;
          }
          centroids.push_back(c);
        }
      }
      return centroids;
    }

    double matchTolerance(const std::vector<Point>& points) {
      Point lo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
      Point hi{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
      for (const auto& p : points) {
        for (int a = 0; a < 3; ++a) {
          lo[a] = std::min(lo[a], p[a]);
          hi[a] = std::max(hi[a], p[a]);
        }
      }
      const double diagonal = points.empty() ? 1.0 : std::hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
      // CAD kernels round coordinates; 1e-6 of the model size is far below any element size.
      return std::max(diagonal * 1e-6, 1e-12);
    }

    // ------------------------------------------------------------ sidecar write

    void writeSidecar(const std::filesystem::path& path, const MeshModel& model, const std::vector<std::size_t>& exportedElements) {
      std::string out = std::format("{} {}\nUNIT {}\n", kSidecarMagic, kSidecarVersion, model.lengthUnit);

      out += std::format("NODES {}\n", model.nodes.size());
      for (const auto& node : model.nodes) {
        detail::appendNumber(out, node.position[0]);
        out.push_back(' ');
        detail::appendNumber(out, node.position[1]);
        out.push_back(' ');
        detail::appendNumber(out, node.position[2]);
        out.push_back('\n');
      }
      // Elements are identified by their end nodes (indices into the NODES list above), which
      // stays unique where centroids do not (crossing X-braces share their midpoint).
      out += std::format("ELEMENTS {}\n", exportedElements.size());
      for (const auto e : exportedElements) {
        const auto location = model.locateElement(e);
        const auto& block = model.blocks[location.block];
        const auto n = static_cast<std::size_t>(elementInfo(block.type).nodeCount);
        out += std::format("2 {} {}\n", block.connectivity[location.local * n], block.connectivity[location.local * n + 1]);
      }

      // All non-geometric data, expressed as fields over the listed nodes / elements.
      const auto encoded = detail::encodeModelData(model, detail::CodecOptions{true, true, false});
      std::vector<const Field*> fields;
      for (const auto& f : model.fields) fields.push_back(&f);
      for (const auto& f : encoded) fields.push_back(&f);
      for (const Field* field : fields) {
        const bool onNodes = field->location == FieldLocation::Node;
        out += std::format("FIELD {} {} {} {}\n", onNodes ? "N" : "E", field->components, field->steps.size(), field->name);
        out += std::format("KIND {} {}\n", stepKindName(field->stepKind), field->stepLabels.empty() ? 0 : 1);
        for (auto label : field->stepLabels) {
          std::ranges::replace(label, '\n', ' ');
          out += "LABEL " + label + "\n";
        }
        for (std::size_t s = 0; s < field->steps.size(); ++s) {
          out += "TIME ";
          detail::appendNumber(out, s < field->times.size() ? field->times[s] : 0.0);
          out.push_back('\n');
          const auto& values = field->steps[s];
          const auto rows = onNodes ? model.nodes.size() : exportedElements.size();
          for (std::size_t r = 0; r < rows; ++r) {
            const std::size_t entity = onNodes ? r : exportedElements[r];
            for (int c = 0; c < field->components; ++c) {
              detail::appendNumber(out, values[entity * static_cast<std::size_t>(field->components) + c]);
              out.push_back(c + 1 == field->components ? '\n' : ' ');
            }
          }
        }
      }
      for (const auto& global : model.globalData) {
        out += std::format("GLOBAL {} {} {}\n", global.components, global.tuples(), global.name);
        for (std::size_t i = 0; i < global.values.size(); ++i) {
          detail::appendNumber(out, global.values[i]);
          out.push_back((i + 1) % static_cast<std::size_t>(global.components) == 0 ? '\n' : ' ');
        }
      }
      out += "END\n";
      detail::writeWholeFile(path, out);
    }

    // ------------------------------------------------------------ sidecar read

    // Versions 2 and 3 (see the file header for the differences).
    void readSidecarV2(Cursor& cursor, MeshModel& model, const int version) {
      if (cursor.token() != "UNIT") throw ParseFailure("sidecar: UNIT expected");
      cursor.line();
      if (cursor.token() != "NODES") throw ParseFailure("sidecar: NODES expected");
      const auto nodeCount = cursor.number<std::size_t>();
      std::vector<Point> sideNodes(nodeCount);
      for (auto& p : sideNodes) p = {cursor.number<double>(), cursor.number<double>(), cursor.number<double>()};
      if (cursor.token() != "ELEMENTS") throw ParseFailure("sidecar: ELEMENTS expected");
      const auto elementCount = cursor.number<std::size_t>();
      std::vector<std::vector<std::size_t>> sideElements(elementCount);
      for (auto& nodes : sideElements) {
        nodes.resize(cursor.number<std::size_t>());
        for (auto& n : nodes) n = cursor.number<std::size_t>();
      }

      // Nodes are matched by position; elements by the (matched) nodes they connect.
      const auto modelNodes = nodePositions(model);
      const double tolerance = matchTolerance(modelNodes);
      const PointIndex nodeIndex(modelNodes, tolerance);
      std::vector<std::optional<std::uint32_t>> nodeMap(nodeCount), elementMap(elementCount);
      std::size_t unmatchedNodes = 0, unmatchedElements = 0;
      for (std::size_t i = 0; i < nodeCount; ++i) if (!(nodeMap[i] = nodeIndex.nearest(sideNodes[i]))) ++unmatchedNodes;

      std::map<std::vector<std::uint32_t>, std::uint32_t> elementByEnds;
      {
        std::uint32_t global = 0;
        for (const auto& block : model.blocks) {
          const auto n = static_cast<std::size_t>(elementInfo(block.type).nodeCount);
          for (std::size_t e = 0; e < block.size(); ++e, ++global) {
            if (n < 2) continue;
            std::vector<std::uint32_t> ends{block.connectivity[e * n], block.connectivity[e * n + 1]};
            std::ranges::sort(ends);
            elementByEnds.emplace(std::move(ends), global);
          }
        }
      }
      for (std::size_t i = 0; i < elementCount; ++i) {
        std::vector<std::uint32_t> ends;
        for (const auto n : sideElements[i]) {
          if (n < nodeCount && nodeMap[n]) ends.push_back(*nodeMap[n]);
        }
        std::ranges::sort(ends);
        const auto it = elementByEnds.find(ends);
        if (ends.size() == sideElements[i].size() && it != elementByEnds.end()) elementMap[i] = it->second;
        else ++unmatchedElements;
      }
      if (unmatchedNodes + unmatchedElements > 0) {
        model.warnings.push_back(std::format("sidecar: {} nodes and {} elements did not match the CAD mesh", unmatchedNodes, unmatchedElements));
      }

      while (true) {
        const auto keyword = cursor.token();
        if (keyword.empty() || keyword == "END") break;
        if (keyword == "GLOBAL" && version >= 3) {
          GlobalArray global;
          global.components = cursor.number<int>();
          const auto tuples = cursor.number<std::size_t>();
          if (global.components < 1) throw ParseFailure("sidecar: invalid GLOBAL component count");
          cursor.skipSpace();
          global.name = std::string(cursor.line());
          global.values.resize(tuples * static_cast<std::size_t>(global.components));
          for (auto& v : global.values) v = cursor.number<double>();
          model.globalData.push_back(std::move(global));
          continue;
        }
        if (keyword != "FIELD") throw ParseFailure("sidecar: FIELD expected, got '" + std::string(keyword) + "'");
        Field field;
        const bool onNodes = cursor.token() == "N";
        field.location = onNodes ? FieldLocation::Node : FieldLocation::Element;
        field.components = cursor.number<int>();
        const auto steps = cursor.number<std::size_t>();
        cursor.skipSpace();
        field.name = std::string(cursor.line());
        if (version >= 3) {
          if (cursor.token() != "KIND") throw ParseFailure("sidecar: KIND expected");
          const auto kind = stepKindFromName(cursor.token());
          if (!kind) throw ParseFailure("sidecar: unknown step kind");
          field.stepKind = *kind;
          if (cursor.number<int>() != 0) {
            field.stepLabels.resize(steps);
            for (auto& label : field.stepLabels) {
              if (cursor.token() != "LABEL") throw ParseFailure("sidecar: LABEL expected");
              // One separating space after the keyword; the rest of the line is the label.
              auto rest = cursor.line();
              if (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
              label = std::string(rest);
            }
          }
        }
        const std::size_t rows = onNodes ? nodeCount : elementCount;
        const std::size_t entities = onNodes ? model.nodes.size() : model.elementCount();
        const auto& map = onNodes ? nodeMap : elementMap;
        for (std::size_t s = 0; s < steps; ++s) {
          if (cursor.token() != "TIME") throw ParseFailure("sidecar: TIME expected");
          field.times.push_back(cursor.number<double>());
          std::vector<double> values(entities * static_cast<std::size_t>(field.components), 0.0);
          for (std::size_t r = 0; r < rows; ++r) {
            for (int c = 0; c < field.components; ++c) {
              const double v = cursor.number<double>();
              if (map[r]) values[*map[r] * static_cast<std::size_t>(field.components) + c] = v;
            }
          }
          field.steps.push_back(std::move(values));
        }
        model.fields.push_back(std::move(field));
      }
    }

    // Version 1 (anafinen <= 0.1.2): "NODES n / x y z dx dy dz" and
    // "ELEMENTS m / mx my mz materialID area stress", matched by position.
    void readSidecarV1(Cursor& cursor, MeshModel& model) {
      const auto modelNodes = nodePositions(model);
      const auto modelCentroids = elementCentroids(model);
      const double tolerance = matchTolerance(modelNodes);
      const PointIndex nodeIndex(modelNodes, tolerance);
      const PointIndex elementIndex(modelCentroids, tolerance);
      std::vector<double> displacement(model.nodes.size() * 3, 0.0);
      std::vector<double> material(model.elementCount(), 0.0), area(model.elementCount(), 0.0), stress(model.elementCount(), 0.0);
      bool anyNodes = false, anyElements = false;
      while (true) {
        const auto keyword = cursor.token();
        if (keyword.empty()) break;
        if (keyword.starts_with("#")) {
          cursor.line();
          continue;
        }
        const auto count = cursor.number<std::size_t>();
        for (std::size_t i = 0; i < count; ++i) {
          const Point p{cursor.number<double>(), cursor.number<double>(), cursor.number<double>()};
          const double a = cursor.number<double>(), b = cursor.number<double>(), c = cursor.number<double>();
          if (keyword == "NODES") {
            if (const auto n = nodeIndex.nearest(p)) {
              displacement[*n * 3] = a;
              displacement[*n * 3 + 1] = b;
              displacement[*n * 3 + 2] = c;
              anyNodes = true;
            }
          } else if (keyword == "ELEMENTS") {
            if (const auto e = elementIndex.nearest(p)) {
              material[*e] = a;
              area[*e] = b;
              stress[*e] = c;
              anyElements = true;
            }
          }
        }
      }
      auto add = [&](const char* name, const FieldLocation location, const int components, std::vector<double> values) {
        model.fields.push_back(Field{name, location, components, {0.0}, {std::move(values)}, StepKind::Time, {}});
      };
      if (anyNodes) add(FieldName::Displacement, FieldLocation::Node, 3, std::move(displacement));
      if (anyElements) {
        add(Attribute::MaterialId, FieldLocation::Element, 1, std::move(material));
        add(Attribute::CrossSectionArea, FieldLocation::Element, 1, std::move(area));
        add(FieldName::Stress, FieldLocation::Element, 1, std::move(stress));
      }
    }

    void readSidecar(const std::filesystem::path& path, MeshModel& model) {
      const std::string content = detail::readWholeFile(path);
      Cursor cursor(content);
      const auto first = cursor.peekToken();
      if (first == kSidecarMagic) {
        cursor.token();
        const int version = cursor.number<int>();
        if (version > kSidecarVersion) throw ParseFailure(std::format("sidecar version {} is newer than supported ({})", version, kSidecarVersion));
        readSidecarV2(cursor, model, version);
      } else {
        readSidecarV1(cursor, model);
      }
      detail::decodeModelData(model, detail::CodecOptions{true, true, false});
    }

  } // namespace end

  // Nodes, elements and physical groups of the current Gmsh model. `onlyDimension` >= 0 keeps
  // elements of that dimension only (CAD import); -1 keeps everything.
  void extractGmshMesh(MeshModel& model, const IoContext& context, const int onlyDimension) {
    context.progress(0.3f, "reading nodes");
    {
      std::vector<std::size_t> nodeTags;
      std::vector<double> coords;
      std::vector<double> parametric;
      gmsh::model::mesh::getNodes(nodeTags, coords, parametric, -1, -1, false, false);
      model.nodes.resize(nodeTags.size());
      for (std::size_t i = 0; i < nodeTags.size(); ++i) {
        model.nodes[i] = Node{nodeTags[i], {coords[3 * i], coords[3 * i + 1], coords[3 * i + 2]}};
      }
    }
    const auto nodeIndex = model.nodeIndexByTag();
    checkCancel(context);

    context.progress(0.45f, "reading elements");
    struct GroupKey {
      int dim;
      int tag;
      bool operator<(const GroupKey& other) const { return std::pair{dim, tag} < std::pair{other.dim, other.tag}; }
    };
    std::map<GroupKey, std::vector<ElementLocation>> groupElements;
    std::map<std::string, std::size_t> skippedTypes;

    gmsh::vectorpair entities;
    gmsh::model::getEntities(entities);
    for (const auto& [dim, entityTag] : entities) {
      if (onlyDimension >= 0 && dim != onlyDimension) continue;
      std::vector<int> types;
      std::vector<std::vector<std::size_t>> elementTags;
      std::vector<std::vector<std::size_t>> elementNodes;
      gmsh::model::mesh::getElements(types, elementTags, elementNodes, dim, entityTag);

      std::vector<int> physicalTags;
      gmsh::model::getPhysicalGroupsForEntity(dim, entityTag, physicalTags);

      for (std::size_t t = 0; t < types.size(); ++t) {
        const auto type = elementTypeFromGmsh(types[t]);
        if (!type) {
          std::string name;
          int typeDim = 0, order = 0, typeNodes = 0, primary = 0;
          std::vector<double> local;
          gmsh::model::mesh::getElementProperties(types[t], name, typeDim, order, typeNodes, local, primary);
          skippedTypes[name] += elementTags[t].size();
          continue;
        }
        auto& block = model.blockFor(*type);
        const std::size_t blockIndex = static_cast<std::size_t>(&block - model.blocks.data());
        const std::size_t first = block.size();
        block.tags.insert(block.tags.end(), elementTags[t].begin(), elementTags[t].end());
        block.entityTags.insert(block.entityTags.end(), elementTags[t].size(), entityTag);
        block.connectivity.reserve(block.connectivity.size() + elementNodes[t].size());
        for (const auto tag : elementNodes[t]) {
          const auto it = nodeIndex.find(tag);
          if (it == nodeIndex.end()) throw detail::ParseFailure(std::format("element references unknown node {}", tag));
          block.connectivity.push_back(it->second);
        }
        for (const int physical : physicalTags) {
          auto& members = groupElements[GroupKey{dim, physical}];
          for (std::size_t e = 0; e < elementTags[t].size(); ++e) members.push_back({blockIndex, first + e});
        }
      }
      checkCancel(context);
    }
    for (const auto& [name, count] : skippedTypes) {
      model.warnings.push_back(std::format("{} elements of unsupported type '{}' were skipped", count, name));
    }

    // Physical groups -> sets (global element indices are final only now).
    std::vector<std::size_t> offsets(model.blocks.size(), 0);
    for (std::size_t b = 1; b < model.blocks.size(); ++b) offsets[b] = offsets[b - 1] + model.blocks[b - 1].size();
    for (const auto& [key, members] : groupElements) {
      EntitySet set;
      gmsh::model::getPhysicalName(key.dim, key.tag, set.name);
      if (set.name.empty()) set.name = std::format("Physical{}D_{}", key.dim, key.tag);
      set.tag = key.tag;
      set.dimension = key.dim;
      if (key.dim == 0) {
        set.kind = SetKind::Node;
        std::set<std::uint32_t> nodes;
        for (const auto& location : members) {
          const auto& block = model.blocks[location.block];
          const int count = elementInfo(block.type).nodeCount;
          for (int k = 0; k < count; ++k) nodes.insert(block.connectivity[location.local * count + k]);
        }
        set.members.assign(nodes.begin(), nodes.end());
      } else {
        set.kind = SetKind::Element;
        set.members.reserve(members.size());
        for (const auto& location : members) set.members.push_back(static_cast<std::uint32_t>(offsets[location.block] + location.local));
        std::ranges::sort(set.members);
      }
      // Groups sharing a name (one per dimension, as written for mixed-dimension sets) form one set.
      const auto existing = std::ranges::find_if(model.sets, [&](const EntitySet& other) {
        return other.kind == set.kind && other.name == set.name;
      });
      if (existing != model.sets.end()) {
        existing->members.insert(existing->members.end(), set.members.begin(), set.members.end());
        std::ranges::sort(existing->members);
        existing->dimension = std::max(existing->dimension, set.dimension);
        existing->tag = -1;
      } else {
        model.sets.push_back(std::move(set));
      }
    }


  }

  MeshModel readCad(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context) {
    const int dimension = std::clamp(options.cadMeshDimension, 1, 3);
    MeshModel model;
    {
      detail::GmshSession session;
      // Convert whatever unit the CAD file declares to metres.
      gmsh::option::setString("Geometry.OCCTargetUnit", "M");
      context.progress(0.05f, "importing CAD");
      gmsh::vectorpair imported;
      gmsh::model::occ::importShapes(path.string(), imported, false);
      // Surfaces / volumes: fragment the geometry so touching bodies get conformal interfaces.
      // Bars: no fragmenting, it would split crossing members (X-bracing) at their intersection
      // and connect them; coincident end nodes are merged after meshing instead.
      if (dimension >= 2) gmsh::model::occ::removeAllDuplicates();
      gmsh::model::occ::synchronize();
      checkCancel(context);

      if (options.cadMeshSize > 0.0) {
        gmsh::option::setNumber("Mesh.MeshSizeMin", options.cadMeshSize);
        gmsh::option::setNumber("Mesh.MeshSizeMax", options.cadMeshSize);
      } else if (dimension == 1) {
        // One element per curve: every CAD edge is one bar / beam.
        gmsh::option::setNumber("Mesh.MeshSizeMin", 1e22);
        gmsh::option::setNumber("Mesh.MeshSizeMax", 1e22);
      }
      gmsh::option::setNumber("Mesh.ElementOrder", std::clamp(options.cadElementOrder, 1, 2));
      context.progress(0.3f, "meshing");
      gmsh::model::mesh::generate(dimension);
      if (dimension == 1) gmsh::model::mesh::removeDuplicateNodes();
      checkCancel(context);
      extractGmshMesh(model, context, options.cadKeepLowerDimensions ? -1 : dimension);
    }
    if (dimension == 1) {
      // CAD files often repeat an edge for every face / body that uses it; after the node merge
      // those become coincident bars, which would count the member's stiffness twice.
      std::size_t removed = 0;
      for (auto& block : model.blocks) {
        const auto n = static_cast<std::size_t>(elementInfo(block.type).nodeCount);
        std::set<std::vector<std::uint32_t>> seen;
        ElementBlock kept{block.type, {}, {}, {}};
        for (std::size_t e = 0; e < block.size(); ++e) {
          std::vector<std::uint32_t> key(block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * n),
                                         block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * n));
          std::ranges::sort(key);
          if (!seen.insert(std::move(key)).second) {
            ++removed;
            continue;
          }
          kept.tags.push_back(block.tags[e]);
          kept.entityTags.push_back(block.entityTags.empty() ? 0 : block.entityTags[e]);
          kept.connectivity.insert(kept.connectivity.end(), block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * n),
                                   block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * n));
        }
        block = std::move(kept);
      }
      if (removed > 0) model.warnings.push_back(std::format("{} duplicate CAD edges were merged", removed));
    }
    model.title = path.stem().string();
    if (model.elementCount() == 0) {
      model.warnings.push_back(std::format("the CAD file produced no {}D elements", dimension));
    }

    const auto sidecar = sidecarPath(path);
    if (options.readSidecar && std::filesystem::exists(sidecar)) {
      context.progress(0.9f, "reading sidecar");
      try {
        readSidecar(sidecar, model);
      } catch (const std::exception& error) {
        model.warnings.push_back(std::string("sidecar ignored: ") + error.what());
      }
    }
    context.progress(1.0f, "done");
    return model;
  }

  WriteReport writeStep(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context) {
    if (const auto problems = model.validate(); !problems.empty()) {
      throw std::invalid_argument("model is inconsistent: " + problems.front());
    }
    WriteReport report;
    report.path = path.string();

    std::vector<std::size_t> exportedElements;
    std::map<std::string, std::size_t> skipped;
    {
      detail::GmshSession session;
      gmsh::option::setString("Geometry.OCCTargetUnit", "M");
      std::vector<int> pointTag(model.nodes.size());
      for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& p = model.nodes[i].position;
        pointTag[i] = gmsh::model::occ::addPoint(p[0], p[1], p[2]);
        if (i % 4096 == 0) checkCancel(context);
      }
      context.progress(0.4f, "edges");
      std::size_t global = 0;
      for (const auto& block : model.blocks) {
        const auto& info = elementInfo(block.type);
        const bool isLine = block.type == ElementType::Line2 || block.type == ElementType::Line3;
        for (std::size_t e = 0; e < block.size(); ++e, ++global) {
          if (!isLine) {
            if (block.type != ElementType::Point1) ++skipped[std::string(info.name)];
            continue;
          }
          const auto a = block.connectivity[e * info.nodeCount];
          const auto b = block.connectivity[e * info.nodeCount + 1];
          gmsh::model::occ::addLine(pointTag[a], pointTag[b]);
          exportedElements.push_back(global);
        }
      }
      gmsh::model::occ::synchronize();
      context.progress(0.7f, "saving");
      gmsh::write(path.string());
    }
    for (const auto& [name, count] : skipped) {
      report.warnings.push_back(std::format("{} {} elements are not representable in STEP and were skipped", count, name));
    }
    if (std::ranges::any_of(model.blocks, [](const ElementBlock& b) { return b.type == ElementType::Line3 && b.size() > 0; })) {
      report.warnings.push_back("Line3 elements were written as straight edges (mid nodes dropped)");
    }

    if (options.writeSidecar) {
      const auto sidecar = sidecarPath(path);
      writeSidecar(sidecar, model, exportedElements);
      report.extraFiles.push_back(sidecar.string());
    }
    context.progress(1.0f, "done");
    return report;
  }

} // namespace anaf::IO::formats end
