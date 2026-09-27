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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <exception>
#include <fstream>
#include <stdexcept>

namespace anaf::FILE {

  namespace {

    constexpr int VTK_LINE_CELL_TYPE = 3; // legacy VTK cell type id for a 2-point line
    using Direction = std::array<double, 3>;

    std::vector<Direction> allowedBasisFromFixedDirections(const std::vector<Direction>& fixedDirections) {
      constexpr double tolerance = 1e-12;
      std::vector<Direction> fixedBasis;
      for (const auto& direction : fixedDirections) {
        Direction residual = direction;
        for (const auto& basis : fixedBasis) {
          const double projection = residual[0] * basis[0] + residual[1] * basis[1] + residual[2] * basis[2];
          for (std::size_t axis = 0; axis < 3; ++axis) residual[axis] -= projection * basis[axis];
        }
        const double norm = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1] + residual[2] * residual[2]);
        if (norm > tolerance) {
          for (double& value : residual) value /= norm;
          fixedBasis.push_back(residual);
          if (fixedBasis.size() == 3) return {};
        }
      }

      std::vector<Direction> allowedBasis;
      for (std::size_t axis = 0; axis < 3 && allowedBasis.size() + fixedBasis.size() < 3; ++axis) {
        Direction residual{};
        residual[axis] = 1.0;
        for (const auto& basis : fixedBasis) {
          const double projection = residual[0] * basis[0] + residual[1] * basis[1] + residual[2] * basis[2];
          for (std::size_t component = 0; component < 3; ++component) residual[component] -= projection * basis[component];
        }
        for (const auto& basis : allowedBasis) {
          const double projection = residual[0] * basis[0] + residual[1] * basis[1] + residual[2] * basis[2];
          for (std::size_t component = 0; component < 3; ++component) residual[component] -= projection * basis[component];
        }
        const double norm = std::sqrt(residual[0] * residual[0] + residual[1] * residual[1] + residual[2] * residual[2]);
        if (norm > tolerance) {
          for (double& value : residual) value /= norm;
          allowedBasis.push_back(residual);
        }
      }
      return allowedBasis;
    }

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

      constexpr std::array<const char*, 3> fixityNames{"FixityX", "FixityY", "FixityZ"};
      for (std::size_t axis = 0; axis < fixityNames.size(); ++axis) {
        file << "SCALARS " << fixityNames[axis] << " int 1\nLOOKUP_TABLE default\n";
        for (const auto& node : nodes) {
          file << (node.getMovable()[axis] ? 0 : 1) << "\n";
        }
      }

      file << "FIELD NodeConstraints 2\n";
      file << "AllowedMotionRank 1 " << nodes.size() << " int\n";
      for (const auto& node : nodes) file << node.getAllowedMotionDirections().size() << "\n";
      file << "AllowedMotionBasis 9 " << nodes.size() << " double\n";
      for (const auto& node : nodes) {
        const auto& directions = node.getAllowedMotionDirections();
        for (std::size_t basisIndex = 0; basisIndex < 3; ++basisIndex) {
          for (std::size_t axis = 0; axis < 3; ++axis) {
            const double value = basisIndex < directions.size() ? directions[basisIndex][axis] : 0.0;
            file << value << (basisIndex == 2 && axis == 2 ? '\n' : ' ');
          }
        }
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
      std::size_t activeDataCount{0};
      bool readingPointData{false};
      bool hasAllowedMotionBasis{false};
      std::vector<std::vector<Direction>> legacyFixedDirections;

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
          legacyFixedDirections.assign(count, {});
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
          activeDataCount = pointDataCount;
          readingPointData = true;
        } else if (token == "CELL_DATA") {
          iss >> cellDataCount;
          activeDataCount = cellDataCount;
          readingPointData = false;
        } else if (token == "VECTORS") {
          std::string name, dataType;
          iss >> name >> dataType;
          for (std::size_t i{0}; i < activeDataCount; ++i) {
            std::getline(file, line);
            std::istringstream vecStream(line);
            double dx{0.0}, dy{0.0}, dz{0.0};
            vecStream >> dx >> dy >> dz;
            if (readingPointData && i < nodes.size()) {
              if (name == "Displacement") {
                nodes[i].setDisplacements({dx, dy, dz});
              } else if (name.starts_with("FixityDirection_") && (dx != 0.0 || dy != 0.0 || dz != 0.0)) {
                legacyFixedDirections[i].push_back({dx, dy, dz});
              }
            }
          }
        } else if (token == "SCALARS") {
          std::string name, dataType;
          int numComponents{1};
          iss >> name >> dataType >> numComponents;
          
          // Consume LOOKUP_TABLE line
          std::getline(file, line); 
          
          for (std::size_t i{0}; i < activeDataCount; ++i) {
            std::getline(file, line);
            std::istringstream scalarStream(line);
            double val{0.0};
            scalarStream >> val;
            if (readingPointData && i < nodes.size()) {
              auto movable = nodes[i].getMovable();
              bool isFixityField{true};
              if (name == "FixityX") movable[0] = (val == 0.0);
              else if (name == "FixityY") movable[1] = (val == 0.0);
              else if (name == "FixityZ") movable[2] = (val == 0.0);
              else isFixityField = false;
              if (isFixityField) nodes[i].setMovable(movable);
            } else if (!readingPointData && i < elements.size()) {
              if (name == "Stress") elements[i].stress = val;
              else if (name == "MaterialID") elements[i].materialID = static_cast<std::uint32_t>(val);
              else if (name == "CrossSectionArea") elements[i].crossSectionArea = val;
            }
          }
        } else if (token == "FIELD") {
          std::string fieldName;
          std::size_t arrayCount{0};
          iss >> fieldName >> arrayCount;
          std::vector<int> motionRanks;
          std::vector<std::array<double, 9>> motionBases;
          bool hasMotionRanks{false};
          bool hasMotionBases{false};

          for (std::size_t arrayIndex = 0; arrayIndex < arrayCount; ++arrayIndex) {
            do {
              if (!std::getline(file, line)) throw std::runtime_error("Unexpected end of VTK FIELD data");
            } while (line.empty());

            std::istringstream arrayHeader(line);
            std::string arrayName, dataType;
            std::size_t components{0}, tuples{0};
            arrayHeader >> arrayName >> components >> tuples >> dataType;
            const bool isMotionRanks = readingPointData && arrayName == "AllowedMotionRank" && components == 1 && tuples == nodes.size();
            const bool isMotionBasis = readingPointData && arrayName == "AllowedMotionBasis" && components == 9 && tuples == nodes.size();
            std::vector<double> values;
            if (isMotionRanks || isMotionBasis) values.resize(components * tuples);
            for (std::size_t valueIndex = 0; valueIndex < components * tuples; ++valueIndex) {
              double value{0.0};
              if (!(file >> value)) throw std::runtime_error("Invalid numeric value in VTK FIELD data");
              if (!values.empty()) values[valueIndex] = value;
            }
            if (components * tuples > 0) std::getline(file, line);

            if (isMotionRanks) {
              motionRanks.resize(tuples);
              std::transform(values.begin(), values.end(), motionRanks.begin(), [](double value) { return static_cast<int>(value); });
              hasMotionRanks = true;
            } else if (isMotionBasis) {
              motionBases.resize(tuples);
              for (std::size_t tuple = 0; tuple < tuples; ++tuple) {
                std::copy_n(values.begin() + static_cast<std::ptrdiff_t>(tuple * 9), 9, motionBases[tuple].begin());
              }
              hasMotionBases = true;
            }
          }

          if (hasMotionRanks && hasMotionBases) {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
              if (motionRanks[i] < 0 || motionRanks[i] > 3) throw std::runtime_error("Invalid AllowedMotionRank in VTK FIELD data");
              std::vector<Direction> directions;
              directions.reserve(static_cast<std::size_t>(motionRanks[i]));
              for (int basisIndex = 0; basisIndex < motionRanks[i]; ++basisIndex) {
                const auto offset = static_cast<std::size_t>(basisIndex) * 3;
                directions.push_back({motionBases[i][offset], motionBases[i][offset + 1], motionBases[i][offset + 2]});
              }
              nodes[i].setAllowedMotionDirections(std::move(directions));
            }
            hasAllowedMotionBasis = true;
          }
        }
      }

      if (!hasAllowedMotionBasis) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
          auto& fixedDirections = legacyFixedDirections[i];
          const auto& movable = nodes[i].getMovable();
          for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!movable[axis]) {
              Direction direction{};
              direction[axis] = 1.0;
              fixedDirections.push_back(direction);
            }
          }
          if (!fixedDirections.empty()) nodes[i].setAllowedMotionDirections(allowedBasisFromFixedDirections(fixedDirections));
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
