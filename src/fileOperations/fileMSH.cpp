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

#include "fileMSH.hpp"
#include "gmshRuntime.hpp"

#include <log/anaf_info.hpp>

#include <gmsh.h>

#include <cstdint>
#include <exception>
#include <unordered_map>

namespace anaf::FILE {

  namespace {

    // gmsh's .msh type id for a 2-node line
    constexpr int GMSH_LINE_ELEMENT_TYPE = 1;

    // Pulls every node and 2-node line element out of the currently loaded gmsh model.
    void extractNodesAndLines(MeshImportData& data) {
      std::vector<std::size_t> nodeTags;
      std::vector<double> coords;
      std::vector<double> parametricCoords;
      gmsh::model::mesh::getNodes(nodeTags, coords, parametricCoords, -1, -1, true, false);

      std::vector<FEM::TRUSS::Node> nodes;
      nodes.reserve(nodeTags.size());
      std::unordered_map<std::size_t, std::uint32_t> nodeTagToIndex;
      nodeTagToIndex.reserve(nodeTags.size());
      for (std::size_t i{0}; i < nodeTags.size(); ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        nodeTagToIndex[nodeTags[i]] = index;
        nodes.emplace_back(index, coords[3 * i], coords[3 * i + 1], coords[3 * i + 2]);
      }

      std::vector<int> elementTypes;
      std::vector<std::vector<std::size_t>> elementTagsByType;
      std::vector<std::vector<std::size_t>> elementNodeTagsByType;
      gmsh::model::mesh::getElements(elementTypes, elementTagsByType, elementNodeTagsByType);

      std::vector<LineElement> elements;
      std::unordered_map<std::size_t, std::uint32_t> elementTagToIndex;
      for (std::size_t type{0}; type < elementTypes.size(); ++type) {
        if (elementTypes[type] != GMSH_LINE_ELEMENT_TYPE) continue;
        const auto& tags = elementNodeTagsByType[type];
        const auto& ownTags = elementTagsByType[type];
        for (std::size_t e{0}, elemIdx{0}; e + 1 < tags.size(); e += 2, ++elemIdx) {
          const auto it1 = nodeTagToIndex.find(tags[e]);
          const auto it2 = nodeTagToIndex.find(tags[e + 1]);
          if (it1 == nodeTagToIndex.end() || it2 == nodeTagToIndex.end()) continue;
          elementTagToIndex[ownTags[elemIdx]] = static_cast<std::uint32_t>(elements.size());
          elements.push_back(LineElement{it1->second, it2->second});
        }
      }

      // overlay named $NodeData/$ElementData views written by exportMSH, if any
      std::vector<int> viewTags;
      gmsh::view::getTags(viewTags);
      for (const int viewTag : viewTags) {
        std::string viewName;
        gmsh::option::getString("View[" + std::to_string(gmsh::view::getIndex(viewTag)) + "].Name", viewName);

        std::string dataType;
        std::vector<std::size_t> tags;
        std::vector<std::vector<double>> values;
        double time{};
        int numComponents{};
        gmsh::view::getModelData(viewTag, 0, dataType, tags, values, time, numComponents);

        if (dataType == "NodeData" && viewName == "Displacement") {
          for (std::size_t i{0}; i < tags.size(); ++i) {
            const auto it = nodeTagToIndex.find(tags[i]);
            if (it == nodeTagToIndex.end()) continue;
            nodes[it->second].setDisplacements({values[i][0], values[i][1], values[i][2]});
          }
        } else if (dataType == "ElementData") {
          for (std::size_t i{0}; i < tags.size(); ++i) {
            const auto it = elementTagToIndex.find(tags[i]);
            if (it == elementTagToIndex.end()) continue;
            auto& element = elements[it->second];
            if (viewName == "Stress") element.stress = values[i][0];
            else if (viewName == "MaterialID") element.materialID = static_cast<std::uint32_t>(values[i][0]);
            else if (viewName == "CrossSectionArea") element.crossSectionArea = values[i][0];
          }
        }
      }

      data.setNodes(std::move(nodes));
      data.setElements(std::move(elements));
    }

    // Writes the nodes/line elements of `data` into the current (empty) gmsh model
    // as a single discrete 1D entity, ready for gmsh::write().
    void buildGmshModelFromData(const MeshImportData& data) {
      const int entityTag = gmsh::model::addDiscreteEntity(1);

      const auto& nodes = data.getNodes();
      std::vector<std::size_t> nodeTags(nodes.size());
      std::vector<double> coords(nodes.size() * 3);
      for (std::size_t i{0}; i < nodes.size(); ++i) {
        nodeTags[i] = i + 1; // gmsh tags are 1-based and must be strictly positive
        coords[3 * i] = nodes[i].getLocX();
        coords[3 * i + 1] = nodes[i].getLocY();
        coords[3 * i + 2] = nodes[i].getLocZ();
      }
      gmsh::model::mesh::addNodes(1, entityTag, nodeTags, coords);

      const auto& elements = data.getElements();
      std::vector<std::size_t> elementTags(elements.size());
      std::vector<std::size_t> elementNodeTags(elements.size() * 2);
      for (std::size_t i{0}; i < elements.size(); ++i) {
        elementTags[i] = i + 1;
        elementNodeTags[2 * i] = elements[i].node1 + 1;
        elementNodeTags[2 * i + 1] = elements[i].node2 + 1;
      }
      gmsh::model::mesh::addElements(1, entityTag, {GMSH_LINE_ELEMENT_TYPE}, {elementTags}, {elementNodeTags});
    }

    void appendResultFields(const std::string& filePath, const MeshImportData& data) {
      const auto& nodes = data.getNodes();
      const auto& elements = data.getElements();

      // displacement View
      const int dispView = gmsh::view::add("Displacement");
      std::vector<std::size_t> nodeTags(nodes.size());
      std::vector<std::vector<double>> dispData(nodes.size(), std::vector<double>(3));
      for (std::size_t i{0}; i < nodes.size(); ++i) {
        nodeTags[i] = i + 1;
        const auto& d = nodes[i].getDisplacement();
        dispData[i] = {d[0], d[1], d[2]};
      }
      gmsh::view::addModelData(dispView, 0, "Displacement", "NodeData", nodeTags, dispData);

      // element Views Helper
      const auto addElementScalarView = [&](const std::string& name, auto getter) {
        const int viewTag = gmsh::view::add(name);
        std::vector<std::size_t> elemTags(elements.size());
        std::vector<std::vector<double>> elemData(elements.size(), std::vector<double>(1));
        for (std::size_t i{0}; i < elements.size(); ++i) {
          elemTags[i] = i + 1;
          elemData[i][0] = getter(elements[i]);
        }
        gmsh::view::addModelData(viewTag, 0, name, "ElementData", elemTags, elemData);
      };

      addElementScalarView("Stress", [](const LineElement& e) { return e.stress; });
      addElementScalarView("MaterialID", [](const LineElement& e) { return static_cast<double>(e.materialID); });
      addElementScalarView("CrossSectionArea", [](const LineElement& e) { return e.crossSectionArea; });

      // writes both mesh and view in one time as standard 4.1 format
      gmsh::write(filePath);
    }

  } // anonymous namespace

  std::shared_ptr<MeshImportData> importMSH(const std::string& filePath) {
    auto data = std::make_shared<MeshImportData>();
    data->setSourcePath(filePath);
    try {
      resetGmshSession();
      gmsh::open(filePath);
      extractNodesAndLines(*data);
      data->setSuccess(true);
      anaf::LOG::info("Imported MSH file '{}': {} nodes, {} elements",
        filePath, data->getNodes().size(), data->getElements().size());
    } catch (const std::exception& ex) {
      data->setErrorMessage(ex.what());
      anaf::LOG::error("Failed to import MSH file '{}': {}", filePath, ex.what());
    }
    return data;
  }

  bool exportMSH(const std::string& filePath, const MeshImportData& data) {
    try {
      resetGmshSession();
      buildGmshModelFromData(data);
      gmsh::write(filePath);
      appendResultFields(filePath, data);
      anaf::LOG::info("Exported MSH file '{}': {} nodes, {} elements",
        filePath, data.getNodes().size(), data.getElements().size());
      return true;
    } catch (const std::exception& ex) {
      anaf::LOG::error("Failed to export MSH file '{}': {}", filePath, ex.what());
      return false;
    }
  }

} // namespace anaf::FILE end
