// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include "fileSTEP.hpp"
#include "gmshRuntime.hpp"

#include <log/anaf_info.hpp>

#include <gmsh.h>

#include <cstdint>
#include <exception>
#include <fstream>
#include <unordered_map>

namespace anaf::FILE {

  namespace {

    constexpr int GMSH_LINE_ELEMENT_TYPE = 1; // Gmsh's MSH type id for a 2-node line

    struct NodeKey {
      double x{}, y{}, z{};
      bool operator==(const NodeKey& other) const {
        constexpr double eps = 1e-6;
        return std::abs(x - other.x) < eps && 
              std::abs(y - other.y) < eps && 
              std::abs(z - other.z) < eps;
      }
    };

    // Pulls every node and 2-node line element out of the currently loaded (meshed) gmsh model.
    void extractNodesAndLines(MeshImportData& data) {
      std::vector<std::size_t> nodeTags;
      std::vector<double> coords;
      std::vector<double> parametricCoords;
      // includeBoundary=false: for a whole-model (dim=-1) query it would otherwise
      // re-list each curve's endpoint nodes a second time (once as their own point
      // entity, once as a curve boundary), inflating the node count.
      gmsh::model::mesh::getNodes(nodeTags, coords, parametricCoords, -1, -1, false, false);

      std::vector<FEM::TRUSS::Node> nodes;
      nodes.reserve(nodeTags.size());
      std::unordered_map<std::size_t, std::uint32_t> tagToIndex;
      tagToIndex.reserve(nodeTags.size());
      for (std::size_t i{0}; i < nodeTags.size(); ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        tagToIndex[nodeTags[i]] = index;
        nodes.emplace_back(index, coords[3 * i], coords[3 * i + 1], coords[3 * i + 2]);
      }
      data.setNodes(std::move(nodes));

      std::vector<int> elementTypes;
      std::vector<std::vector<std::size_t>> elementTags;
      std::vector<std::vector<std::size_t>> elementNodeTags;
      gmsh::model::mesh::getElements(elementTypes, elementTags, elementNodeTags);

      std::vector<LineElement> elements;
      for (std::size_t type{0}; type < elementTypes.size(); ++type) {
        if (elementTypes[type] != GMSH_LINE_ELEMENT_TYPE) continue;
        const auto& tags = elementNodeTags[type];
        for (std::size_t e{0}; e + 1 < tags.size(); e += 2) {
          const auto it1 = tagToIndex.find(tags[e]);
          const auto it2 = tagToIndex.find(tags[e + 1]);
          if (it1 == tagToIndex.end() || it2 == tagToIndex.end()) continue;
          elements.push_back(LineElement{it1->second, it2->second});
        }
      }
      data.setElements(std::move(elements));
    }

    // STEP is a pure CAD geometry format: gmsh's OCC-based writer does not persist
    // physical groups or entity names (verified empirically - both come back empty
    // after a write/reopen round trip), so there is no channel inside the STEP file
    // itself for material/result data. Instead we keep a small sidecar text file
    // next to it, matched by node/element position, read only by this app.
    std::string sidecarPath(const std::string& stepFilePath) {
      return stepFilePath + ".anafFields";
    }

    void writeSidecarFields(const std::string& stepFilePath, const MeshImportData& data) {
      std::ofstream file(sidecarPath(stepFilePath));
      if (!file) return;

      file << "# anafinen auxiliary FEA data (Coordinate-Mapped)\n";
      const auto& nodes = data.getNodes();
      file << "NODES " << nodes.size() << "\n";
      for (const auto& node : nodes) {
        const auto& d = node.getDisplacement();
        file << node.getLocX() << " " << node.getLocY() << " " << node.getLocZ() << " "
            << d[0] << " " << d[1] << " " << d[2] << "\n";
      }

      const auto& elements = data.getElements();
      file << "ELEMENTS " << elements.size() << "\n";
      for (const auto& element : elements) {
        // match elements by their midpoint
        const auto& n1 = nodes[element.node1];
        const auto& n2 = nodes[element.node2];
        const double mx = 0.5 * (n1.getLocX() + n2.getLocX());
        const double my = 0.5 * (n1.getLocY() + n2.getLocY());
        const double mz = 0.5 * (n1.getLocZ() + n2.getLocZ());
        file << mx << " " << my << " " << mz << " "
            << element.materialID << " " << element.crossSectionArea << " " << element.stress << "\n";
      }
    }

    void readSidecarFields(const std::string& stepFilePath, std::vector<FEM::TRUSS::Node>& nodes, std::vector<LineElement>& elements) {
      std::ifstream file(sidecarPath(stepFilePath));
      if (!file) return;

      std::string token;
      while (file >> token) {
        if (token[0] == '#') {
          std::string discard;
          std::getline(file, discard);
          continue;
        }
        if (token == "NODES") {
          std::size_t count{0};
          file >> count;
          for (std::size_t i{0}; i < count; ++i) {
            double x{}, y{}, z{}, dx{}, dy{}, dz{};
            file >> x >> y >> z >> dx >> dy >> dz;
            // assign to the mesh node at the same position
            for (auto& node : nodes) {
              if (std::hypot(node.getLocX() - x, node.getLocY() - y, node.getLocZ() - z) < 1e-5) {
                node.setDisplacements({dx, dy, dz});
                break;
              }
            }
          }
        } else if (token == "ELEMENTS") {
          std::size_t count{0};
          file >> count;
          for (std::size_t i{0}; i < count; ++i) {
            double mx{}, my{}, mz{}, matID{}, area{}, stress{};
            file >> mx >> my >> mz >> matID >> area >> stress;
            for (auto& element : elements) {
              const auto& n1 = nodes[element.node1];
              const auto& n2 = nodes[element.node2];
              const double curMx = 0.5 * (n1.getLocX() + n2.getLocX());
              const double curMy = 0.5 * (n1.getLocY() + n2.getLocY());
              const double curMz = 0.5 * (n1.getLocZ() + n2.getLocZ());
              if (std::hypot(curMx - mx, curMy - my, curMz - mz) < 1e-4) {
                element.materialID = static_cast<std::uint32_t>(matID);
                element.crossSectionArea = area;
                element.stress = stress;
                break;
              }
            }
          }
        }
      }
    }

  } // anonymous namespace

  std::shared_ptr<MeshImportData> importSTEP(const std::string& filePath) {
    auto data = std::make_shared<MeshImportData>();
    data->setSourcePath(filePath);
    try {
      resetGmshSession();
      // force exactly one mesh element per curve - otherwise gmsh's default
      // characteristic length subdivides straight lines into several segments
      gmsh::option::setNumber("Mesh.MeshSizeMin", 1e22);
      gmsh::option::setNumber("Mesh.MeshSizeMax", 1e22);
      gmsh::vectorpair importedEntities;
      gmsh::model::occ::importShapes(filePath, importedEntities);
      // STEP doesn't guarantee shared topology across curves that only touch at a
      // point - fuse coincident vertices/edges so shared endpoints become one mesh node.
      gmsh::model::occ::removeAllDuplicates();
      gmsh::model::occ::synchronize();
      gmsh::model::mesh::generate(1); // only mesh curves into 2-node line elements
      extractNodesAndLines(*data);

      std::vector<FEM::TRUSS::Node> nodes(data->getNodes());
      std::vector<LineElement> elements(data->getElements());
      readSidecarFields(filePath, nodes, elements);
      data->setNodes(std::move(nodes));
      data->setElements(std::move(elements));

      data->setSuccess(true);
      anaf::LOG::info("Imported STEP file '{}': {} nodes, {} elements",
        filePath, data->getNodes().size(), data->getElements().size());
    } catch (const std::exception& ex) {
      data->setErrorMessage(ex.what());
      anaf::LOG::error("Failed to import STEP file '{}': {}", filePath, ex.what());
    }
    return data;
  }

  bool exportSTEP(const std::string& filePath, const MeshImportData& data) {
    try {
      resetGmshSession();

      const auto& nodes = data.getNodes();
      std::vector<int> pointTagByIndex(nodes.size());
      for (std::size_t i{0}; i < nodes.size(); ++i) {
        pointTagByIndex[i] = gmsh::model::occ::addPoint(
          nodes[i].getLocX(), nodes[i].getLocY(), nodes[i].getLocZ());
      }

      for (const auto& element : data.getElements()) {
        if (element.node1 >= pointTagByIndex.size() || element.node2 >= pointTagByIndex.size()) continue;
        gmsh::model::occ::addLine(pointTagByIndex[element.node1], pointTagByIndex[element.node2]);
      }

      gmsh::model::occ::synchronize();
      gmsh::write(filePath);
      writeSidecarFields(filePath, data);
      anaf::LOG::info("Exported STEP file '{}': {} nodes, {} elements",
        filePath, data.getNodes().size(), data.getElements().size());
      return true;
    } catch (const std::exception& ex) {
      anaf::LOG::error("Failed to export STEP file '{}': {}", filePath, ex.what());
      return false;
    }
  }

} // namespace anaf::FILE end
