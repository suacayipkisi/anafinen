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

#include "fileVTK.hpp"

#include <log/anaf_info.hpp>

#include <cstdint>
#include <sstream>
#include <exception>
#include <fstream>
#include <stdexcept>

namespace anaf::FILE {

  namespace {

    constexpr int VTK_LINE_CELL_TYPE = 3; // legacy VTK cell type id for a 2-point line

    void writeGeometry(std::ofstream& file, const MeshImportData& data) {
      const auto& nodes = data.getNodes();
      const auto& elements = data.getElements();
      file << "# vtk DataFile Version 2.0\nCreated by anafinen\nASCII\nDATASET UNSTRUCTURED_GRID\n";
      file << "POINTS " << nodes.size() << " double\n";
      for (const auto& node : nodes) {
        file << node.getLocX() << " " << node.getLocY() << " " << node.getLocZ() << "\n";
      }
      file << "\nCELLS " << elements.size() << " " << elements.size() * 3 << "\n";
      for (const auto& element : elements) file << "2 " << element.node1 << " " << element.node2 << "\n";
      file << "\nCELL_TYPES " << elements.size() << "\n";
      for (std::size_t i{0}; i < elements.size(); ++i) file << VTK_LINE_CELL_TYPE << "\n";
    }

    // Appends legacy-VTK POINT_DATA/CELL_DATA sections by hand (ParaView and
    // similar tools read this natively).
    void appendResultFields(std::ofstream& file, const MeshImportData& data) {
      const auto& nodes = data.getNodes();
      file << "\nPOINT_DATA " << nodes.size() << "\nVECTORS Displacement double\n";
      for (const auto& node : nodes) {
        const auto& d = node.getDisplacement();
        file << d[0] << " " << d[1] << " " << d[2] << "\n";
      }

      const auto& elements = data.getElements();
      file << "\nCELL_DATA " << elements.size() << "\n";
      file << "SCALARS Stress double 1\nLOOKUP_TABLE default\n";
      for (const auto& element : elements) file << element.stress << "\n";
      file << "SCALARS MaterialID double 1\nLOOKUP_TABLE default\n";
      for (const auto& element : elements) file << element.materialID << "\n";
      file << "SCALARS CrossSectionArea double 1\nLOOKUP_TABLE default\n";
      for (const auto& element : elements) file << element.crossSectionArea << "\n";
    }

    void readVtkFile(const std::string& filePath, std::vector<FEM::TRUSS::Node>& nodes, std::vector<LineElement>& elements) {
      std::ifstream file(filePath);
      if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filePath);
      }

      std::string line;
      std::size_t pointDataCount{0};
      std::size_t cellDataCount{0};

      while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "POINTS") {
          std::size_t count{0};
          std::string dataType;
          iss >> count >> dataType;
          nodes.clear();
          nodes.reserve(count);
          for (std::size_t i{0}; i < count; ++i) {
            std::getline(file, line);
            std::istringstream pointStream(line);
            double x{0.0}, y{0.0}, z{0.0};
            pointStream >> x >> y >> z;
            nodes.emplace_back(static_cast<std::uint32_t>(i), x, y, z);
          }
        } else if (token == "CELLS") {
          std::size_t numCells{0}, listSize{0};
          iss >> numCells >> listSize;
          elements.clear();
          elements.reserve(numCells);
          for (std::size_t i{0}; i < numCells; ++i) {
            std::getline(file, line);
            std::istringstream cellStream(line);
            std::size_t nPoints{0};
            cellStream >> nPoints;
            std::vector<std::uint32_t> indices(nPoints);
            for (auto& idx : indices) {
              cellStream >> idx;
            }
            if (nPoints == 2) {
              elements.push_back(LineElement{indices[0], indices[1]});
            }
          }
        } else if (token == "POINT_DATA") {
          iss >> pointDataCount;
        } else if (token == "CELL_DATA") {
          iss >> cellDataCount;
        } else if (token == "VECTORS") {
          std::string name, dataType;
          iss >> name >> dataType;
          for (std::size_t i{0}; i < pointDataCount; ++i) {
            std::getline(file, line);
            std::istringstream vecStream(line);
            double dx{0.0}, dy{0.0}, dz{0.0};
            vecStream >> dx >> dy >> dz;
            if (name == "Displacement" && i < nodes.size()) {
              nodes[i].setDisplacements({dx, dy, dz});
            }
          }
        } else if (token == "SCALARS") {
          std::string name, dataType;
          int numComponents{1};
          iss >> name >> dataType >> numComponents;
          
          // Consume LOOKUP_TABLE line
          std::getline(file, line); 
          
          for (std::size_t i{0}; i < cellDataCount; ++i) {
            std::getline(file, line);
            std::istringstream scalarStream(line);
            double val{0.0};
            scalarStream >> val;
            if (i < elements.size()) {
              if (name == "Stress") elements[i].stress = val;
              else if (name == "MaterialID") elements[i].materialID = static_cast<std::uint32_t>(val);
              else if (name == "CrossSectionArea") elements[i].crossSectionArea = val;
            }
          }
        }
      }
    }

  } // anonymous namespace

  std::shared_ptr<MeshImportData> importVTK(const std::string& filePath) {
    auto data = std::make_shared<MeshImportData>();
    data->setSourcePath(filePath);
    try {
      std::vector<FEM::TRUSS::Node> nodes;
      std::vector<LineElement> elements;
      readVtkFile(filePath, nodes, elements);
      data->setNodes(std::move(nodes));
      data->setElements(std::move(elements));
      data->setSuccess(true);
      anaf::LOG::info("Imported VTK file '{}': {} nodes, {} elements",
        filePath, data->getNodes().size(), data->getElements().size());
    } catch (const std::exception& ex) {
      data->setErrorMessage(ex.what());
      anaf::LOG::error("Failed to import VTK file '{}': {}", filePath, ex.what());
    }
    return data;
  }

  bool exportVTK(const std::string& filePath, const MeshImportData& data) {
    try {
      std::ofstream file(filePath);
      if (!file) throw std::runtime_error("could not open '" + filePath + "' for writing");
      writeGeometry(file, data);
      appendResultFields(file, data);
      anaf::LOG::info("Exported VTK file '{}': {} nodes, {} elements",
        filePath, data.getNodes().size(), data.getElements().size());
      return true;
    } catch (const std::exception& ex) {
      anaf::LOG::error("Failed to export VTK file '{}': {}", filePath, ex.what());
      return false;
    }
  }

} // namespace anaf::FILE end
