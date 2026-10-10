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

// Legacy VTK ("# vtk DataFile Version x.y"), versions 2.0 ... 5.1, ASCII and binary
// (binary data is big-endian by specification).
//
// Read:  DATASET UNSTRUCTURED_GRID and POLYDATA; POINT_DATA / CELL_DATA with SCALARS,
//        COLOR_SCALARS, VECTORS, NORMALS, TEXTURE_COORDINATES, TENSORS, TENSORS6,
//        GLOBAL_IDS, PEDIGREE_IDS and FIELD arrays; METADATA blocks are skipped.
//        Dataset-level FIELD arrays (before POINTS) become MeshModel::globalData.
// Write: UNSTRUCTURED_GRID, version 4.2 (classic CELLS) or 5.1 (OFFSETS / CONNECTIVITY).
//        Global data (and TimeValue) as dataset-level FIELD; steps as in detail::flattenSteps().

#include "formats.hpp"
#include "../core/pathUtf8.hpp"
#include "../detail/modelCodec.hpp"
#include "../detail/textIo.hpp"
#include "../detail/vtkCommon.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <type_traits>
#include <string_view>
#include <vector>

namespace anaf::IO::formats {

  namespace {

    using detail::Cursor;
    using detail::ParseFailure;

    void checkCancel(const IoContext& context) {
      if (context.cancelled()) throw detail::CancelledFailure();
    }

    std::string upper(std::string_view text) {
      std::string out(text);
      for (char& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      return out;
    }

    // Size in bytes of a legacy VTK data type in binary files; 0 = unsupported.
    std::size_t typeSize(const std::string& type) {
      const std::string t = detail::toLower(type);
      if (t == "unsigned_char" || t == "char") return 1;
      if (t == "unsigned_short" || t == "short") return 2;
      if (t == "unsigned_int" || t == "int" || t == "float" || t == "vtkidtype") return 4; // VTK writes vtkIdType as int
      if (t == "unsigned_long" || t == "long" || t == "double" || t == "vtktypeint64" || t == "vtktypeuint64") return 8;
      return 0;
    }

    double loadAsDouble(const char* raw, const std::string& type) {
      const std::string t = detail::toLower(type);
      constexpr bool big = true;
      if (t == "unsigned_char") return detail::loadValue<std::uint8_t>(raw, big);
      if (t == "char") return detail::loadValue<std::int8_t>(raw, big);
      if (t == "unsigned_short") return detail::loadValue<std::uint16_t>(raw, big);
      if (t == "short") return detail::loadValue<std::int16_t>(raw, big);
      if (t == "unsigned_int") return detail::loadValue<std::uint32_t>(raw, big);
      if (t == "int" || t == "vtkidtype") return detail::loadValue<std::int32_t>(raw, big);
      if (t == "float") return detail::loadValue<float>(raw, big);
      if (t == "double") return detail::loadValue<double>(raw, big);
      if (t == "unsigned_long" || t == "vtktypeuint64") return static_cast<double>(detail::loadValue<std::uint64_t>(raw, big));
      return static_cast<double>(detail::loadValue<std::int64_t>(raw, big));
    }

    std::int64_t loadAsInt(const char* raw, const std::string& type) {
      const std::string t = detail::toLower(type);
      constexpr bool big = true;
      if (t == "unsigned_long" || t == "vtktypeuint64") return static_cast<std::int64_t>(detail::loadValue<std::uint64_t>(raw, big));
      if (t == "long" || t == "vtktypeint64") return detail::loadValue<std::int64_t>(raw, big);
      return static_cast<std::int64_t>(loadAsDouble(raw, type));
    }

    template <typename Out>
    std::vector<Out> readArray(Cursor& cursor, const std::size_t count, const std::string& type, const bool binary) {
      std::vector<Out> values(count);
      if (binary) {
        const std::size_t size = typeSize(type);
        if (size == 0) throw ParseFailure("unsupported binary data type '" + type + "'");
        // The keyword line (including its line break) has been consumed: raw data starts here.
        const auto raw = cursor.bytes(count * size);
        for (std::size_t i = 0; i < count; ++i) {
          if constexpr (std::is_integral_v<Out>) values[i] = static_cast<Out>(loadAsInt(raw.data() + i * size, type));
          else values[i] = static_cast<Out>(loadAsDouble(raw.data() + i * size, type));
        }
      } else {
        // Declared precision wins: ASCII "float" data holds float values, exactly as VTK reads it.
        const bool float32 = detail::toLower(type) == "float";
        for (std::size_t i = 0; i < count; ++i) {
          if constexpr (std::is_floating_point_v<Out>) {
            values[i] = float32 ? static_cast<Out>(cursor.number<float>()) : cursor.number<Out>();
          } else {
            values[i] = cursor.number<Out>();
          }
        }
      }
      return values;
    }

    // Keyword arguments up to the end of the keyword line.
    std::vector<std::string> lineArguments(Cursor& cursor) {
      std::vector<std::string> args;
      Cursor line(cursor.line());
      for (auto token = line.token(); !token.empty(); token = line.token()) args.emplace_back(token);
      return args;
    }

    void skipMetadata(Cursor& cursor) {
      // METADATA is followed by INFORMATION / COMPONENT_NAMES blocks, terminated by an empty line.
      cursor.line();
      while (!cursor.atEnd()) {
        const auto text = cursor.line();
        if (text.find_first_not_of(" \t\r") == std::string_view::npos) break;
      }
    }

    // Cell list in either the classic "n i0 i1 ..." layout or the 5.x OFFSETS / CONNECTIVITY layout.
    struct CellList {
      std::vector<std::vector<std::int64_t>> cells;
    };

    CellList readCellList(Cursor& cursor, const std::vector<std::string>& args, const bool binary, const bool offsetsLayout) {
      if (args.size() < 2) throw ParseFailure("malformed cell list header");
      CellList list;
      if (offsetsLayout) {
        const auto offsetCount = Cursor::parseNumber<std::size_t>(args[0]);
        const auto connectivitySize = Cursor::parseNumber<std::size_t>(args[1]);
        if (upper(cursor.token()) != "OFFSETS") throw ParseFailure("OFFSETS expected");
        const auto offsetType = lineArguments(cursor);
        const auto offsets = readArray<std::int64_t>(cursor, offsetCount, offsetType.empty() ? "vtktypeint64" : offsetType[0], binary);
        if (upper(cursor.token()) != "CONNECTIVITY") throw ParseFailure("CONNECTIVITY expected");
        const auto connectivityType = lineArguments(cursor);
        const auto connectivity = readArray<std::int64_t>(cursor, connectivitySize, connectivityType.empty() ? "vtktypeint64" : connectivityType[0], binary);
        for (std::size_t c = 0; c + 1 < offsets.size(); ++c) {
          if (offsets[c] < 0 || offsets[c + 1] < offsets[c] || static_cast<std::size_t>(offsets[c + 1]) > connectivity.size()) {
            throw ParseFailure("invalid cell offsets");
          }
          list.cells.emplace_back(connectivity.begin() + offsets[c], connectivity.begin() + offsets[c + 1]);
        }
      } else {
        const auto cellCount = Cursor::parseNumber<std::size_t>(args[0]);
        const auto totalSize = Cursor::parseNumber<std::size_t>(args[1]);
        const auto flat = readArray<std::int64_t>(cursor, totalSize, "int", binary);
        std::size_t pos = 0;
        for (std::size_t c = 0; c < cellCount; ++c) {
          if (pos >= flat.size()) throw ParseFailure("cell list shorter than declared");
          const auto n = static_cast<std::size_t>(flat[pos++]);
          if (pos + n > flat.size()) throw ParseFailure("cell list shorter than declared");
          list.cells.emplace_back(flat.begin() + static_cast<std::ptrdiff_t>(pos), flat.begin() + static_cast<std::ptrdiff_t>(pos + n));
          pos += n;
        }
      }
      return list;
    }

    struct RawField {
      std::string name;
      bool onPoints;
      int components;
      std::vector<double> values;
    };

  } // namespace end

  MeshModel readVtkLegacy(const std::filesystem::path& path, const ReadOptions&, const IoContext& context) {
    const std::string content = detail::readWholeFile(path);
    Cursor cursor(content);
    context.progress(0.05f, "parsing header");

    const auto header = cursor.line();
    const auto versionPos = header.find("Version");
    if (header.find("vtk") == std::string_view::npos || versionPos == std::string_view::npos) {
      throw ParseFailure("not a legacy VTK file (missing '# vtk DataFile Version' header)");
    }
    const double version = Cursor::parseNumber<double>(Cursor(header.substr(versionPos + 7)).token());
    const bool offsetsLayout = version >= 5.0;

    MeshModel model;
    model.title = std::string(cursor.line());
    const std::string encoding = upper(cursor.token());
    if (encoding != "ASCII" && encoding != "BINARY") throw ParseFailure("expected ASCII or BINARY, got '" + encoding + "'");
    const bool binary = encoding == "BINARY";

    if (upper(cursor.token()) != "DATASET") throw ParseFailure("DATASET expected");
    const std::string dataset = upper(cursor.token());
    if (dataset != "UNSTRUCTURED_GRID" && dataset != "POLYDATA") {
      throw ParseFailure("dataset '" + dataset + "' is not supported (UNSTRUCTURED_GRID or POLYDATA expected)");
    }
    const bool polydata = dataset == "POLYDATA";

    CellList cells;
    std::vector<int> cellTypes;
    // POLYDATA sections, stored with their implied VTK cell type (vertex/line/polygon/strip).
    std::vector<std::pair<int, CellList>> polySections;
    std::vector<RawField> rawFields;
    bool onPoints = true;
    bool inAttributes = false; // after the first POINT_DATA / CELL_DATA
    std::size_t pointCount = 0;
    std::size_t attributeCount = 0;

    auto readAttribute = [&](const std::string& name, const int components, const std::string& type) {
      auto values = readArray<double>(cursor, attributeCount * static_cast<std::size_t>(components), type, binary);
      rawFields.push_back(RawField{detail::decodeLegacyName(name), onPoints, components, std::move(values)});
    };

    while (true) {
      const auto keywordView = cursor.token();
      if (keywordView.empty()) break;
      checkCancel(context);
      const std::string keyword = upper(keywordView);

      if (keyword == "POINTS") {
        const auto args = lineArguments(cursor);
        if (args.size() < 2) throw ParseFailure("malformed POINTS header");
        pointCount = Cursor::parseNumber<std::size_t>(args[0]);
        const auto coords = readArray<double>(cursor, pointCount * 3, args[1], binary);
        model.nodes.resize(pointCount);
        for (std::size_t i = 0; i < pointCount; ++i) {
          model.nodes[i] = Node{i + 1, {coords[i * 3], coords[i * 3 + 1], coords[i * 3 + 2]}};
        }
        context.progress(0.3f, "points");
      } else if (keyword == "CELLS") {
        cells = readCellList(cursor, lineArguments(cursor), binary, offsetsLayout);
        context.progress(0.5f, "cells");
      } else if (keyword == "CELL_TYPES") {
        const auto args = lineArguments(cursor);
        const auto count = Cursor::parseNumber<std::size_t>(args.at(0));
        const auto types = readArray<std::int64_t>(cursor, count, "int", binary);
        cellTypes.assign(types.begin(), types.end());
      } else if (keyword == "VERTICES" || keyword == "LINES" || keyword == "POLYGONS" || keyword == "TRIANGLE_STRIPS") {
        const int implied = keyword == "VERTICES" ? 2 : keyword == "LINES" ? 4 : keyword == "POLYGONS" ? 7 : 6;
        polySections.emplace_back(implied, readCellList(cursor, lineArguments(cursor), binary, offsetsLayout));
      } else if (keyword == "POINT_DATA" || keyword == "CELL_DATA") {
        const auto args = lineArguments(cursor);
        attributeCount = Cursor::parseNumber<std::size_t>(args.at(0));
        onPoints = keyword == "POINT_DATA";
        inAttributes = true;
      } else if (keyword == "SCALARS") {
        const auto args = lineArguments(cursor);
        if (args.size() < 2) throw ParseFailure("malformed SCALARS header");
        const int components = args.size() >= 3 ? Cursor::parseNumber<int>(args[2]) : 1;
        if (upper(cursor.peekToken()) == "LOOKUP_TABLE") {
          cursor.token();
          cursor.line();
        }
        readAttribute(args[0], components, args[1]);
      } else if (keyword == "COLOR_SCALARS") {
        const auto args = lineArguments(cursor);
        const int components = Cursor::parseNumber<int>(args.at(1));
        readAttribute(args.at(0), components, binary ? "unsigned_char" : "float");
      } else if (keyword == "LOOKUP_TABLE") {
        // Standalone color table: 4 values (RGBA) per entry; not model data.
        const auto args = lineArguments(cursor);
        const auto entries = Cursor::parseNumber<std::size_t>(args.at(1));
        readArray<double>(cursor, entries * 4, binary ? "unsigned_char" : "float", binary);
      } else if (keyword == "VECTORS" || keyword == "NORMALS") {
        const auto args = lineArguments(cursor);
        readAttribute(args.at(0), 3, args.at(1));
      } else if (keyword == "TEXTURE_COORDINATES") {
        const auto args = lineArguments(cursor);
        readAttribute(args.at(0), Cursor::parseNumber<int>(args.at(1)), args.at(2));
      } else if (keyword == "TENSORS" || keyword == "TENSORS6") {
        const auto args = lineArguments(cursor);
        readAttribute(args.at(0), keyword == "TENSORS" ? 9 : 6, args.at(1));
      } else if (keyword == "GLOBAL_IDS" || keyword == "PEDIGREE_IDS") {
        const auto args = lineArguments(cursor);
        readAttribute(args.at(0), 1, args.at(1));
      } else if (keyword == "FIELD") {
        const auto args = lineArguments(cursor);
        const auto arrays = Cursor::parseNumber<std::size_t>(args.at(1));
        for (std::size_t a = 0; a < arrays; ++a) {
          const std::string arrayName(cursor.token());
          if (upper(arrayName) == "NULL_ARRAY") continue;
          if (upper(arrayName) == "METADATA") {
            skipMetadata(cursor);
            --a;
            continue;
          }
          const auto header2 = lineArguments(cursor);
          if (header2.size() < 3) throw ParseFailure("malformed FIELD array header");
          const int components = Cursor::parseNumber<int>(header2[0]);
          const auto tuples = Cursor::parseNumber<std::size_t>(header2[1]);
          auto values = readArray<double>(cursor, tuples * static_cast<std::size_t>(components), header2[2], binary);
          // Dataset-level FIELD arrays (before POINT_DATA / CELL_DATA) are not attached to entities.
          if (!inAttributes) {
            model.globalData.push_back(GlobalArray{detail::decodeLegacyName(arrayName), components, std::move(values)});
          } else if (tuples == attributeCount) {
            rawFields.push_back(RawField{detail::decodeLegacyName(arrayName), onPoints, components, std::move(values)});
          }
        }
      } else if (keyword == "METADATA") {
        skipMetadata(cursor);
      } else {
        throw ParseFailure("unexpected keyword '" + std::string(keywordView) + "'");
      }
    }

    // Build elements.
    detail::VtkCellCollector collector(model, 0);
    std::size_t fileCell = 0;
    if (polydata) {
      for (const auto& [implied, list] : polySections) {
        for (const auto& cell : list.cells) collector.addCell(implied, cell, fileCell++);
      }
    } else {
      if (cellTypes.size() != cells.cells.size()) {
        throw ParseFailure(std::format("{} cells but {} cell types", cells.cells.size(), cellTypes.size()));
      }
      for (std::size_t c = 0; c < cells.cells.size(); ++c) collector.addCell(cellTypes[c], cells.cells[c], fileCell++);
    }
    const auto sourceCell = collector.finish();
    context.progress(0.8f, "attributes");

    for (auto& raw : rawFields) {
      Field field;
      field.name = raw.name;
      field.location = raw.onPoints ? E_FieldLocation::Node : E_FieldLocation::Element;
      field.components = raw.components;
      field.times = {0.0};
      if (raw.onPoints) {
        if (raw.values.size() != pointCount * static_cast<std::size_t>(raw.components)) {
          model.warnings.push_back(std::format("point array '{}' has the wrong size and was skipped", raw.name));
          continue;
        }
        field.steps.push_back(std::move(raw.values));
      } else {
        if (raw.values.size() != fileCell * static_cast<std::size_t>(raw.components)) {
          model.warnings.push_back(std::format("cell array '{}' has the wrong size and was skipped", raw.name));
          continue;
        }
        field.steps.push_back(detail::VtkCellCollector::remapCellData(raw.values, raw.components, sourceCell));
      }
      model.fields.push_back(std::move(field));
    }

    detail::unflattenSteps(model);
    detail::decodeModelData(model, detail::CodecOptions{});
    context.progress(1.0f, "done");
    return model;
  }

  // ---------------------------------------------------------------- write

  namespace {

    class LegacyWriter {
    public:
      explicit LegacyWriter(const bool binary) : m_binary(binary) {}

      void text(std::string_view line) { m_out.append(line); }

      void doubles(std::span<const double> values, const int perLine) {
        if (m_binary) {
          for (const double v : values) detail::appendValue(m_out, v, true);
          m_out.push_back('\n');
          return;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
          detail::appendNumber(m_out, values[i]);
          m_out.push_back((i + 1) % static_cast<std::size_t>(perLine) == 0 || i + 1 == values.size() ? '\n' : ' ');
        }
      }

      template <typename Integer>
      void integers(std::span<const Integer> values, const int perLine) {
        if (m_binary) {
          for (const Integer v : values) detail::appendValue(m_out, v, true);
          m_out.push_back('\n');
          return;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
          detail::appendNumber(m_out, values[i]);
          m_out.push_back((i + 1) % static_cast<std::size_t>(perLine) == 0 || i + 1 == values.size() ? '\n' : ' ');
        }
      }

      std::string& buffer() { return m_out; }

    private:
      bool m_binary;
      std::string m_out;
    };

    void writeAttributes(LegacyWriter& writer, const std::vector<const detail::FlatArray*>& arrays) {
      std::vector<const detail::FlatArray*> generic;
      for (const auto* array : arrays) {
        const std::string name = detail::encodeLegacyName(array->name);
        if (array->components == 1) {
          writer.text(std::format("SCALARS {} double 1\nLOOKUP_TABLE default\n", name));
          writer.doubles(*array->values, 9);
        } else if (array->components == 3) {
          writer.text(std::format("VECTORS {} double\n", name));
          writer.doubles(*array->values, 3);
        } else {
          generic.push_back(array);
        }
      }
      if (!generic.empty()) {
        writer.text(std::format("FIELD FieldData {}\n", generic.size()));
        for (const auto* array : generic) {
          const std::size_t tuples = array->values->size() / static_cast<std::size_t>(array->components);
          writer.text(std::format("{} {} {} double\n", detail::encodeLegacyName(array->name), array->components, tuples));
          writer.doubles(*array->values, array->components);
        }
      }
    }

  } // namespace end

  WriteReport writeVtkLegacy(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context) {
    if (const auto problems = model.validate(); !problems.empty()) {
      throw std::invalid_argument("model is inconsistent: " + problems.front());
    }
    WriteReport report;
    report.path = pathToUtf8(path);
    const bool binary = options.encoding == E_Encoding::Binary;
    const bool v51 = options.vtkVersion == E_VtkLegacyVersion::V5_1;
    LegacyWriter writer(binary);

    std::string title = model.title.empty() ? "anafinen mesh" : model.title;
    std::ranges::replace(title, '\n', ' ');
    if (title.size() > 255) title.resize(255);
    writer.text(std::format("# vtk DataFile Version {}\n{}\n{}\nDATASET UNSTRUCTURED_GRID\n", v51 ? "5.1" : "4.2", title,
      binary ? "BINARY" : "ASCII"));

    const auto encoded = detail::encodeModelData(model, detail::CodecOptions{true, true, options.writeTags});
    const auto flat = detail::flattenSteps(model, encoded, options.timeStep);
    report.warnings.insert(report.warnings.end(), flat.warnings.begin(), flat.warnings.end());
    // Dataset-level field data goes between DATASET and POINTS, where VTK writes it.
    std::size_t globalCount = 0;
    for (const auto& global : flat.globals) globalCount += global.tuples() > 0 ? 1 : 0;
    if (globalCount > 0) {
      writer.text(std::format("FIELD FieldData {}\n", globalCount));
      for (const auto& global : flat.globals) {
        if (global.tuples() == 0) continue;
        writer.text(std::format("{} {} {} double\n", detail::encodeLegacyName(global.name), global.components, global.tuples()));
        writer.doubles(global.values, global.components);
      }
    }

    std::vector<double> coords(model.nodes.size() * 3);
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
      for (int a = 0; a < 3; ++a) coords[i * 3 + a] = model.nodes[i].position[a];
    }
    writer.text(std::format("POINTS {} double\n", model.nodes.size()));
    writer.doubles(coords, 3);
    context.progress(0.3f, "points");
    if (context.cancelled()) throw detail::CancelledFailure();

    auto cells = detail::buildVtkCells(model);
    report.warnings.insert(report.warnings.end(), cells.warnings.begin(), cells.warnings.end());
    const std::size_t cellCount = cells.types.size();
    if (v51) {
      std::vector<std::int64_t> offsets(cellCount + 1, 0);
      std::copy(cells.offsets.begin(), cells.offsets.end(), offsets.begin() + 1);
      writer.text(std::format("CELLS {} {}\nOFFSETS vtktypeint64\n", cellCount + 1, cells.connectivity.size()));
      writer.integers(std::span<const std::int64_t>(offsets), 9);
      writer.text("CONNECTIVITY vtktypeint64\n");
      writer.integers(std::span<const std::int64_t>(cells.connectivity), 9);
    } else {
      // Classic layout: count followed by the point ids for every cell, 32-bit ints.
      std::vector<std::int32_t> classic;
      classic.reserve(cellCount + cells.connectivity.size());
      std::int64_t begin = 0;
      for (std::size_t c = 0; c < cellCount; ++c) {
        classic.push_back(static_cast<std::int32_t>(cells.offsets[c] - begin));
        for (std::int64_t k = begin; k < cells.offsets[c]; ++k) classic.push_back(static_cast<std::int32_t>(cells.connectivity[static_cast<std::size_t>(k)]));
        begin = cells.offsets[c];
      }
      writer.text(std::format("CELLS {} {}\n", cellCount, classic.size()));
      if (binary) {
        writer.integers(std::span<const std::int32_t>(classic), 1);
      } else {
        std::size_t pos = 0;
        for (std::size_t c = 0; c < cellCount; ++c) {
          const auto n = static_cast<std::size_t>(classic[pos]);
          writer.integers(std::span<const std::int32_t>(classic.data() + pos, n + 1), static_cast<int>(n + 1));
          pos += n + 1;
        }
      }
    }
    std::vector<std::int32_t> types(cells.types.begin(), cells.types.end());
    writer.text(std::format("CELL_TYPES {}\n", cellCount));
    writer.integers(std::span<const std::int32_t>(types), 1);
    context.progress(0.6f, "attributes");

    std::vector<const detail::FlatArray*> pointArrays;
    std::vector<const detail::FlatArray*> cellArrays;
    for (const auto& array : flat.arrays) (array.location == E_FieldLocation::Node ? pointArrays : cellArrays).push_back(&array);
    if (!pointArrays.empty()) {
      writer.text(std::format("POINT_DATA {}\n", model.nodes.size()));
      writeAttributes(writer, pointArrays);
    }
    if (!cellArrays.empty() && cellCount > 0) {
      writer.text(std::format("CELL_DATA {}\n", cellCount));
      writeAttributes(writer, cellArrays);
    }

    if (context.cancelled()) throw detail::CancelledFailure();
    detail::writeWholeFile(path, writer.buffer());
    context.progress(1.0f, "done");
    return report;
  }

} // namespace anaf::IO::formats end
