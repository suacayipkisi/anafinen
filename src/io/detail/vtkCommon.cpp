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

#include "vtkCommon.hpp"
#include "textIo.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace anaf::IO::detail {

  namespace {
    // VTK cell type ids that are not a fixed-size element in the type table.
    constexpr int kVtkPolyVertex = 2;
    constexpr int kVtkPolyLine = 4;
    constexpr int kVtkTriangleStrip = 6;
    constexpr int kVtkPolygon = 7;
    constexpr int kVtkPixel = 8;
    constexpr int kVtkVoxel = 11;
    constexpr int kVtkEmptyCell = 0;
  } // namespace end

  void VtkCellCollector::emit(const ElementType type, const std::span<const std::int64_t> vtkOrderedPoints, const std::size_t fileCell) {
    const auto& info = elementInfo(type);
    auto& block = m_model.blockFor(type);
    const std::size_t blockIndex = static_cast<std::size_t>(&block - m_model.blocks.data());
    if (m_sourceCellPerBlock.size() <= blockIndex) m_sourceCellPerBlock.resize(blockIndex + 1);

    std::array<std::uint32_t, 32> gmshOrdered{};
    for (int i = 0; i < info.nodeCount; ++i) {
      const std::int64_t point = vtkOrderedPoints[static_cast<std::size_t>(i)];
      if (point < 0) throw ParseFailure("negative point index in cell connectivity");
      const auto node = static_cast<std::uint32_t>(static_cast<std::size_t>(point) + m_nodeOffset);
      if (node >= m_model.nodes.size()) throw ParseFailure(std::format("cell references point {} beyond {} points", point, m_model.nodes.size()));
      const int target = info.vtkFromGmsh.empty() ? i : info.vtkFromGmsh[static_cast<std::size_t>(i)];
      gmshOrdered[static_cast<std::size_t>(target)] = node;
    }
    block.connectivity.insert(block.connectivity.end(), gmshOrdered.begin(), gmshOrdered.begin() + info.nodeCount);
    block.tags.push_back(0); // assigned in finish()
    block.entityTags.push_back(0);
    m_sourceCellPerBlock[blockIndex].push_back(fileCell);
  }

  void VtkCellCollector::addCell(const int vtkType, const std::span<const std::int64_t> points, const std::size_t fileCell) {
    if (const auto type = elementTypeFromVtk(vtkType)) {
      const auto& info = elementInfo(*type);
      if (static_cast<int>(points.size()) != info.nodeCount) {
        throw ParseFailure(std::format("VTK cell type {} expects {} points, got {}", vtkType, info.nodeCount, points.size()));
      }
      emit(*type, points, fileCell);
      return;
    }
    switch (vtkType) {
      case kVtkEmptyCell:
        return;
      case kVtkPolyVertex:
        for (std::size_t i = 0; i < points.size(); ++i) emit(ElementType::Point1, points.subspan(i, 1), fileCell);
        return;
      case kVtkPolyLine:
        for (std::size_t i = 0; i + 1 < points.size(); ++i) emit(ElementType::Line2, points.subspan(i, 2), fileCell);
        return;
      case kVtkTriangleStrip:
        for (std::size_t i = 0; i + 2 < points.size(); ++i) {
          // Every second triangle is flipped to keep a consistent orientation.
          const std::array<std::int64_t, 3> tri = (i % 2 == 0)
            ? std::array<std::int64_t, 3>{points[i], points[i + 1], points[i + 2]}
            : std::array<std::int64_t, 3>{points[i + 1], points[i], points[i + 2]};
          emit(ElementType::Tri3, tri, fileCell);
        }
        return;
      case kVtkPolygon:
        if (points.size() == 3) {
          emit(ElementType::Tri3, points, fileCell);
        } else if (points.size() == 4) {
          emit(ElementType::Quad4, points, fileCell);
        } else if (points.size() > 4) {
          if (!m_warnedPolygon) {
            m_model.warnings.push_back("polygons with more than 4 points were fan-triangulated");
            m_warnedPolygon = true;
          }
          for (std::size_t i = 1; i + 1 < points.size(); ++i) {
            const std::array<std::int64_t, 3> tri{points[0], points[i], points[i + 1]};
            emit(ElementType::Tri3, tri, fileCell);
          }
        }
        return;
      case kVtkPixel:
        if (points.size() == 4) {
          const std::array<std::int64_t, 4> quad{points[0], points[1], points[3], points[2]};
          emit(ElementType::Quad4, quad, fileCell);
          return;
        }
        break;
      case kVtkVoxel:
        if (points.size() == 8) {
          const std::array<std::int64_t, 8> hex{points[0], points[1], points[3], points[2], points[4], points[5], points[7], points[6]};
          emit(ElementType::Hex8, hex, fileCell);
          return;
        }
        break;
      default:
        break;
    }
    ++m_skipped[vtkType];
  }

  std::vector<std::size_t> VtkCellCollector::finish() {
    for (const auto& [type, count] : m_skipped) {
      m_model.warnings.push_back(std::format("{} VTK cells of unsupported type {} were skipped", count, type));
    }
    std::vector<std::size_t> sourceCell;
    sourceCell.reserve(m_model.elementCount());
    std::uint64_t nextTag = 1;
    for (std::size_t b = 0; b < m_model.blocks.size(); ++b) {
      auto& block = m_model.blocks[b];
      for (auto& tag : block.tags) {
        if (tag == 0) tag = nextTag;
        ++nextTag;
      }
      if (b < m_sourceCellPerBlock.size()) {
        sourceCell.insert(sourceCell.end(), m_sourceCellPerBlock[b].begin(), m_sourceCellPerBlock[b].end());
      }
    }
    return sourceCell;
  }

  std::vector<double> VtkCellCollector::remapCellData(const std::span<const double> fileValues, const int components,
                                                      const std::vector<std::size_t>& sourceCell) {
    const auto stride = static_cast<std::size_t>(components);
    std::vector<double> values(sourceCell.size() * stride, 0.0);
    for (std::size_t e = 0; e < sourceCell.size(); ++e) {
      const std::size_t from = sourceCell[e] * stride;
      if (from + stride > fileValues.size()) throw ParseFailure("cell data shorter than the number of cells");
      std::copy_n(fileValues.begin() + static_cast<std::ptrdiff_t>(from), stride, values.begin() + static_cast<std::ptrdiff_t>(e * stride));
    }
    return values;
  }

  VtkCells buildVtkCells(const MeshModel& model) {
    VtkCells cells;
    const std::size_t total = model.elementCount();
    cells.types.reserve(total);
    cells.offsets.reserve(total);
    for (const auto& block : model.blocks) {
      const auto& info = elementInfo(block.type);
      if (info.vtkType == 0) {
        cells.warnings.push_back(std::format("{} has no VTK equivalent", info.name));
      }
      for (std::size_t e = 0; e < block.size(); ++e) {
        const std::uint32_t* nodes = &block.connectivity[e * static_cast<std::size_t>(info.nodeCount)];
        for (int i = 0; i < info.nodeCount; ++i) {
          const int source = info.vtkFromGmsh.empty() ? i : info.vtkFromGmsh[static_cast<std::size_t>(i)];
          cells.connectivity.push_back(nodes[source]);
        }
        cells.offsets.push_back(static_cast<std::int64_t>(cells.connectivity.size()));
        cells.types.push_back(static_cast<std::uint8_t>(info.vtkType));
      }
    }
    return cells;
  }

  std::string encodeLegacyName(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
      if (c <= ' ' || c == '%' || c >= 127) {
        out += std::format("%{:02X}", static_cast<unsigned>(c));
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
    return out.empty() ? std::string("unnamed") : out;
  }

  std::string decodeLegacyName(const std::string& name) {
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
      if (name[i] == '%' && i + 2 < name.size()) {
        const auto hex = name.substr(i + 1, 2);
        unsigned value = 0;
        const auto result = std::from_chars(hex.data(), hex.data() + 2, value, 16);
        if (result.ec == std::errc{} && result.ptr == hex.data() + 2) {
          out.push_back(static_cast<char>(value));
          i += 2;
          continue;
        }
      }
      out.push_back(name[i]);
    }
    return out;
  }

} // namespace anaf::IO::detail end
