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

// VTK XML UnstructuredGrid (.vtu).
//
// Read:  file versions 0.1 / 1.0 / 2.x, header_type UInt32 or UInt64, LittleEndian or BigEndian,
//        DataArray format ascii / binary (base64) / appended (raw or base64), optional
//        vtkZLibDataCompressor, any number of <Piece> elements (merged).
//        <UnstructuredGrid><FieldData> becomes MeshModel::globalData.
// Write: version 1.0, UInt64 headers, little endian, ascii or inline base64, optional zlib.
//        Global data (and TimeValue) as FieldData; steps as in detail::flattenSteps().
//
// ParaView collection (.pvd): one .vtu per time step in the folder "<stem>/" next to the
// .pvd file. Time fields are split by time value; single-step fields go into every file, and
// every file carries its own TimeValue so that it also reads correctly on its own.
// On read, a field that is identical in every file is folded back into one step.

#include "../core/pathUtf8.hpp"
#include "formats.hpp"
#include "../detail/modelCodec.hpp"
#include "../detail/textIo.hpp"
#include "../detail/vtkCommon.hpp"

#include <zlib.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace anaf::IO::formats {

  namespace {

    using detail::ParseFailure;

    // ------------------------------------------------------------ minimal XML tree

    struct XmlElement {
      std::string name;
      std::map<std::string, std::string> attributes;
      std::vector<std::string_view> textPieces; // character data between child elements
      std::vector<XmlElement> children;

      // Character data of this element without its children (VTK 9 puts <InformationKey>
      // children in front of the values of a DataArray).
      std::string text() const {
        std::string out;
        for (const auto piece : textPieces) out.append(piece);
        return out;
      }

      const XmlElement* child(const std::string_view childName) const {
        for (const auto& c : children) if (c.name == childName) return &c;
        return nullptr;
      }
      std::string attribute(const std::string& key, const std::string& fallback = {}) const {
        const auto it = attributes.find(key);
        return it == attributes.end() ? fallback : it->second;
      }
    };

    std::string decodeEntities(std::string_view text) {
      std::string out;
      out.reserve(text.size());
      for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
          const auto end = text.find(';', i);
          if (end != std::string_view::npos) {
            const auto entity = text.substr(i + 1, end - i - 1);
            char replacement = 0;
            if (entity == "amp") replacement = '&';
            else if (entity == "lt") replacement = '<';
            else if (entity == "gt") replacement = '>';
            else if (entity == "quot") replacement = '"';
            else if (entity == "apos") replacement = '\'';
            if (replacement != 0) {
              out.push_back(replacement);
              i = end;
              continue;
            }
          }
        }
        out.push_back(text[i]);
      }
      return out;
    }

    std::string escapeXml(std::string_view text) {
      std::string out;
      for (const char c : text) {
        switch (c) {
          case '&': out += "&amp;"; break;
          case '<': out += "&lt;"; break;
          case '>': out += "&gt;"; break;
          case '"': out += "&quot;"; break;
          default: out.push_back(c);
        }
      }
      return out;
    }

    class XmlParser {
    public:
      explicit XmlParser(std::string_view data) : m_data(data) {}

      XmlElement parseDocument() {
        skipProlog();
        XmlElement root = parseElement();
        return root;
      }

      // Position right after the '_' marker of <AppendedData>, or npos.
      std::size_t appendedStart() const { return m_appendedStart; }
      std::string appendedEncoding() const { return m_appendedEncoding; }

    private:
      void skipSpace() { while (m_pos < m_data.size() && detail::isSpace(m_data[m_pos])) ++m_pos; }

      void skipProlog() {
        while (true) {
          skipSpace();
          if (m_data.substr(m_pos, 5) == "<?xml" || m_data.substr(m_pos, 2) == "<?") {
            m_pos = find("?>") + 2;
          } else if (m_data.substr(m_pos, 4) == "<!--") {
            m_pos = find("-->") + 3;
          } else if (m_data.substr(m_pos, 2) == "<!") {
            m_pos = find(">") + 1;
          } else {
            return;
          }
        }
      }

      std::size_t find(std::string_view token) const {
        const auto at = m_data.find(token, m_pos);
        if (at == std::string_view::npos) throw ParseFailure("malformed XML: missing '" + std::string(token) + "'");
        return at;
      }

      std::string parseName() {
        const std::size_t begin = m_pos;
        while (m_pos < m_data.size() && !detail::isSpace(m_data[m_pos]) && m_data[m_pos] != '>' && m_data[m_pos] != '/'
               && m_data[m_pos] != '=') {
          ++m_pos;
        }
        return std::string(m_data.substr(begin, m_pos - begin));
      }

      XmlElement parseElement() {
        if (m_pos >= m_data.size() || m_data[m_pos] != '<') throw ParseFailure("malformed XML: '<' expected");
        ++m_pos;
        XmlElement element;
        element.name = parseName();
        // Attributes.
        while (true) {
          skipSpace();
          if (m_pos >= m_data.size()) throw ParseFailure("malformed XML: unterminated tag");
          if (m_data[m_pos] == '/') {
            m_pos = find(">") + 1;
            return element; // self-closing
          }
          if (m_data[m_pos] == '>') {
            ++m_pos;
            break;
          }
          const std::string key = parseName();
          skipSpace();
          if (m_pos >= m_data.size() || m_data[m_pos] != '=') throw ParseFailure("malformed XML attribute '" + key + "'");
          ++m_pos;
          skipSpace();
          const char quote = m_data[m_pos];
          if (quote != '"' && quote != '\'') throw ParseFailure("malformed XML attribute value");
          const auto end = m_data.find(quote, m_pos + 1);
          if (end == std::string_view::npos) throw ParseFailure("unterminated XML attribute value");
          element.attributes[key] = decodeEntities(m_data.substr(m_pos + 1, end - m_pos - 1));
          m_pos = end + 1;
        }

        if (element.name == "AppendedData") {
          // Raw appended data may contain any byte, including '<': stop XML parsing here.
          m_appendedEncoding = element.attribute("encoding", "raw");
          const auto marker = m_data.find('_', m_pos);
          if (marker == std::string_view::npos) throw ParseFailure("AppendedData without '_' marker");
          m_appendedStart = marker + 1;
          m_pos = m_data.size();
          return element;
        }

        // Content: child elements and character data.
        while (true) {
          const auto next = m_data.find('<', m_pos);
          if (next == std::string_view::npos) throw ParseFailure("malformed XML: missing end tag for " + element.name);
          if (next > m_pos) element.textPieces.push_back(m_data.substr(m_pos, next - m_pos));
          m_pos = next;
          if (m_data.substr(m_pos, 4) == "<!--") {
            m_pos = find("-->") + 3;
            continue;
          }
          if (m_data.substr(m_pos, 2) == "</") {
            m_pos = find(">") + 1;
            return element;
          }
          element.children.push_back(parseElement());
          if (m_pos >= m_data.size()) return element; // reached AppendedData
        }
      }

      std::string_view m_data;
      std::size_t m_pos{0};
      std::size_t m_appendedStart{std::string_view::npos};
      std::string m_appendedEncoding{"raw"};
    };

    // ------------------------------------------------------------ DataArray decoding

    struct Layout {
      bool bigEndian{false};
      std::size_t headerSize{4}; // UInt32 or UInt64
      bool zlib{false};
      std::string_view appended;   // everything after the '_' marker
      bool appendedBase64{false};
    };

    std::size_t scalarSize(const std::string& type) {
      if (type == "Int8" || type == "UInt8" || type == "Char" || type == "UChar") return 1;
      if (type == "Int16" || type == "UInt16") return 2;
      if (type == "Int32" || type == "UInt32" || type == "Float32") return 4;
      if (type == "Int64" || type == "UInt64" || type == "Float64") return 8;
      throw ParseFailure("unsupported DataArray type '" + type + "'");
    }

    double scalarAt(const char* raw, const std::string& type, const bool big) {
      if (type == "Int8" || type == "Char") return detail::loadValue<std::int8_t>(raw, big);
      if (type == "UInt8" || type == "UChar") return detail::loadValue<std::uint8_t>(raw, big);
      if (type == "Int16") return detail::loadValue<std::int16_t>(raw, big);
      if (type == "UInt16") return detail::loadValue<std::uint16_t>(raw, big);
      if (type == "Int32") return detail::loadValue<std::int32_t>(raw, big);
      if (type == "UInt32") return detail::loadValue<std::uint32_t>(raw, big);
      if (type == "Int64") return static_cast<double>(detail::loadValue<std::int64_t>(raw, big));
      if (type == "UInt64") return static_cast<double>(detail::loadValue<std::uint64_t>(raw, big));
      if (type == "Float32") return detail::loadValue<float>(raw, big);
      return detail::loadValue<double>(raw, big);
    }

    std::int64_t integerAt(const char* raw, const std::string& type, const bool big) {
      if (type == "Int64") return detail::loadValue<std::int64_t>(raw, big);
      if (type == "UInt64") return static_cast<std::int64_t>(detail::loadValue<std::uint64_t>(raw, big));
      return static_cast<std::int64_t>(scalarAt(raw, type, big));
    }

    std::uint64_t headerWord(const std::string_view bytes, const std::size_t index, const Layout& layout) {
      const std::size_t at = index * layout.headerSize;
      if (at + layout.headerSize > bytes.size()) throw ParseFailure("truncated binary header");
      return layout.headerSize == 8 ? detail::loadValue<std::uint64_t>(bytes.data() + at, layout.bigEndian)
                                    : detail::loadValue<std::uint32_t>(bytes.data() + at, layout.bigEndian);
    }

    // Decodes base64 text until at least `byteCount` bytes are available. Whitespace is skipped;
    // separately encoded chunks (padding in the middle) are handled by base64Decode.
    std::string base64AtLeast(const std::string_view text, const std::size_t byteCount) {
      std::string compact;
      compact.reserve((byteCount + 2) / 3 * 4 + 8);
      std::size_t pos = 0;
      std::size_t wanted = (byteCount + 2) / 3 * 4;
      while (true) {
        while (compact.size() < wanted && pos < text.size()) {
          const char c = text[pos++];
          if (c == '<') {
            pos = text.size(); // end of an inline DataArray
            break;
          }
          if (!detail::isSpace(c)) compact.push_back(c);
        }
        std::string decoded = detail::base64Decode(compact);
        if (decoded.size() >= byteCount || pos >= text.size()) return decoded;
        wanted += 4; // padding groups between chunks yield fewer bytes than 3 per 4 chars
      }
    }

    // Returns the uncompressed payload bytes of a binary / appended array.
    std::string decodeBinaryPayload(const std::string_view source, const bool base64, const Layout& layout) {
      auto available = [&](const std::size_t bytes) -> std::string {
        if (base64) return base64AtLeast(source, bytes);
        if (bytes > source.size()) throw ParseFailure("truncated binary array");
        return std::string(source.substr(0, bytes));
      };

      if (!layout.zlib) {
        const std::string head = available(layout.headerSize);
        const std::uint64_t length = headerWord(head, 0, layout);
        const std::string all = available(layout.headerSize + length);
        if (all.size() < layout.headerSize + length) throw ParseFailure("truncated binary array");
        return all.substr(layout.headerSize, length);
      }

      // Compressed: [blocks, blockSize, lastBlockSize, compressedSize x blocks] then the blocks.
      const std::string first = available(layout.headerSize * 3);
      const std::uint64_t blocks = headerWord(first, 0, layout);
      const std::uint64_t blockSize = headerWord(first, 1, layout);
      const std::uint64_t lastBlockSize = headerWord(first, 2, layout);
      const std::size_t headerBytes = layout.headerSize * (3 + blocks);
      const std::string header = available(headerBytes);
      std::uint64_t compressedTotal = 0;
      for (std::uint64_t b = 0; b < blocks; ++b) compressedTotal += headerWord(header, 3 + b, layout);

      // For base64 the decoder flushes at padding, so a header and data encoded separately
      // (VTK's convention) decode the same as one joint base64 string.
      const std::string all = available(headerBytes + compressedTotal);
      if (all.size() < headerBytes + compressedTotal) throw ParseFailure("truncated compressed array");
      const std::string compressed = all.substr(headerBytes, compressedTotal);

      std::string out;
      out.reserve(blocks * blockSize);
      std::size_t pos = 0;
      for (std::uint64_t b = 0; b < blocks; ++b) {
        const auto compressedSize = headerWord(header, 3 + b, layout);
        const std::uint64_t expected = (b + 1 == blocks && lastBlockSize != 0) ? lastBlockSize : blockSize;
        std::string block(expected, '\0');
        uLongf length = static_cast<uLongf>(expected);
        const int status = uncompress(reinterpret_cast<Bytef*>(block.data()), &length,
                                      reinterpret_cast<const Bytef*>(compressed.data() + pos), static_cast<uLong>(compressedSize));
        if (status != Z_OK) throw ParseFailure("zlib decompression failed");
        out.append(block.data(), length);
        pos += compressedSize;
      }
      return out;
    }

    template <typename Out>
    std::vector<Out> readDataArray(const XmlElement& array, const Layout& layout, const std::size_t expectedCount) {
      const std::string type = array.attribute("type");
      const std::string format = array.attribute("format", "ascii");
      std::vector<Out> values;
      if (format == "ascii") {
        const std::string text = array.text();
        detail::Cursor cursor(text);
        values.reserve(expectedCount);
        const bool float32 = type == "Float32";
        for (auto token = cursor.token(); !token.empty(); token = cursor.token()) {
          auto value = detail::Cursor::parseNumber<Out>(token);
          // Declared precision wins: a Float32 array holds float values, as VTK reads it.
          if constexpr (std::is_floating_point_v<Out>) {
            if (float32) value = static_cast<Out>(static_cast<float>(value));
          }
          values.push_back(value);
        }
      } else {
        std::string payload;
        if (format == "binary") {
          payload = decodeBinaryPayload(array.text(), true, layout);
        } else if (format == "appended") {
          const auto offset = detail::Cursor::parseNumber<std::size_t>(array.attribute("offset", "0"));
          if (offset > layout.appended.size()) throw ParseFailure("appended offset beyond the data section");
          payload = decodeBinaryPayload(layout.appended.substr(offset), layout.appendedBase64, layout);
        } else {
          throw ParseFailure("unsupported DataArray format '" + format + "'");
        }
        const std::size_t size = scalarSize(type);
        const std::size_t count = payload.size() / size;
        values.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
          if constexpr (std::is_integral_v<Out>) values[i] = static_cast<Out>(integerAt(payload.data() + i * size, type, layout.bigEndian));
          else values[i] = static_cast<Out>(scalarAt(payload.data() + i * size, type, layout.bigEndian));
        }
      }
      if (expectedCount != 0 && values.size() != expectedCount) {
        throw ParseFailure(std::format("DataArray '{}' has {} values, {} expected", array.attribute("Name"), values.size(), expectedCount));
      }
      return values;
    }

    const XmlElement* findArray(const XmlElement* parent, const std::string& name) {
      if (!parent) return nullptr;
      for (const auto& c : parent->children) {
        if (c.name == "DataArray" && c.attribute("Name") == name) return &c;
      }
      return nullptr;
    }

  } // namespace end

  MeshModel readVtu(const std::filesystem::path& path, const ReadOptions&, const IoContext& context) {
    const std::string content = detail::readWholeFile(path);
    XmlParser parser(content);
    const XmlElement root = parser.parseDocument();
    if (root.name != "VTKFile") throw ParseFailure("not a VTK XML file (root element is '" + root.name + "')");
    if (root.attribute("type") == "Collection") throw ParseFailure("this is a ParaView collection: read it as .pvd");
    if (root.attribute("type") != "UnstructuredGrid") {
      throw ParseFailure("VTK XML type '" + root.attribute("type") + "' is not supported (UnstructuredGrid expected)");
    }
    Layout layout;
    layout.bigEndian = root.attribute("byte_order", "LittleEndian") == "BigEndian";
    layout.headerSize = root.attribute("header_type", "UInt32") == "UInt64" ? 8 : 4;
    const std::string compressor = root.attribute("compressor");
    if (!compressor.empty() && compressor != "vtkZLibDataCompressor") {
      throw ParseFailure("compressor '" + compressor + "' is not supported (only vtkZLibDataCompressor)");
    }
    layout.zlib = !compressor.empty();
    if (parser.appendedStart() != std::string_view::npos) {
      layout.appended = std::string_view(content).substr(parser.appendedStart());
      layout.appendedBase64 = parser.appendedEncoding() == "base64";
    }

    const XmlElement* grid = root.child("UnstructuredGrid");
    if (!grid) throw ParseFailure("UnstructuredGrid element missing");

    MeshModel model;
    if (const XmlElement* fieldData = grid->child("FieldData")) {
      for (const auto& array : fieldData->children) {
        if (array.name != "DataArray" || array.attribute("type") == "String") continue;
        GlobalArray global;
        global.name = array.attribute("Name");
        global.components = std::max(1, detail::Cursor::parseNumber<int>(array.attribute("NumberOfComponents", "1")));
        const auto tuples = detail::Cursor::parseNumber<std::size_t>(array.attribute("NumberOfTuples", "0"));
        global.values = readDataArray<double>(array, layout, tuples * static_cast<std::size_t>(global.components));
        model.globalData.push_back(std::move(global));
      }
    }
    // All pieces are concatenated first, then turned into elements in one pass.
    std::vector<std::int64_t> connectivity, offsets, types;
    struct PendingArray { std::string name; bool onPoints; int components; std::vector<double> values; };
    std::map<std::pair<std::string, bool>, PendingArray> arrays;
    std::size_t cellTotal = 0;

    std::size_t pieceIndex = 0;
    for (const auto& piece : grid->children) {
      if (piece.name != "Piece") continue;
      if (context.cancelled()) throw detail::CancelledFailure();
      const auto points = detail::Cursor::parseNumber<std::size_t>(piece.attribute("NumberOfPoints", "0"));
      const auto cellCount = detail::Cursor::parseNumber<std::size_t>(piece.attribute("NumberOfCells", "0"));
      const std::size_t pointOffset = model.nodes.size();

      if (points > 0) {
        const XmlElement* pointsElement = piece.child("Points");
        const XmlElement* coordsArray = pointsElement ? pointsElement->child("DataArray") : nullptr;
        if (!coordsArray) throw ParseFailure("Points/DataArray missing");
        const auto coords = readDataArray<double>(*coordsArray, layout, points * 3);
        for (std::size_t i = 0; i < points; ++i) {
          model.nodes.push_back(Node{pointOffset + i + 1, {coords[i * 3], coords[i * 3 + 1], coords[i * 3 + 2]}});
        }
      }
      if (cellCount > 0) {
        const XmlElement* cells = piece.child("Cells");
        const auto* connectivityArray = findArray(cells, "connectivity");
        const auto* offsetsArray = findArray(cells, "offsets");
        const auto* typesArray = findArray(cells, "types");
        if (!connectivityArray || !offsetsArray || !typesArray) throw ParseFailure("Cells arrays missing");
        const auto pieceOffsets = readDataArray<std::int64_t>(*offsetsArray, layout, cellCount);
        const auto pieceConnectivity = readDataArray<std::int64_t>(*connectivityArray, layout, 0);
        const auto pieceTypes = readDataArray<std::int64_t>(*typesArray, layout, cellCount);
        const auto base = static_cast<std::int64_t>(connectivity.size());
        for (const auto node : pieceConnectivity) connectivity.push_back(node + static_cast<std::int64_t>(pointOffset));
        for (const auto offset : pieceOffsets) offsets.push_back(offset + base);
        types.insert(types.end(), pieceTypes.begin(), pieceTypes.end());
      }

      for (const bool onPoints : {true, false}) {
        const XmlElement* data = piece.child(onPoints ? "PointData" : "CellData");
        if (!data) continue;
        const std::size_t entities = onPoints ? points : cellCount;
        for (const auto& array : data->children) {
          if (array.name != "DataArray") continue;
          const std::string type = array.attribute("type");
          if (type == "String") continue;
          const int components = detail::Cursor::parseNumber<int>(array.attribute("NumberOfComponents", "1"));
          auto values = readDataArray<double>(array, layout, entities * static_cast<std::size_t>(components));
          auto& pending = arrays[{array.attribute("Name"), onPoints}];
          if (pending.values.empty() && pieceIndex > 0) {
            // Array missing from earlier pieces: pad them with zeros.
            pending.values.assign((onPoints ? pointOffset : cellTotal) * static_cast<std::size_t>(components), 0.0);
          }
          pending.name = array.attribute("Name");
          pending.onPoints = onPoints;
          pending.components = components;
          pending.values.insert(pending.values.end(), values.begin(), values.end());
        }
      }
      cellTotal += cellCount;
      ++pieceIndex;
      context.progress(0.1f + 0.6f * static_cast<float>(pieceIndex) / static_cast<float>(grid->children.size()), "pieces");
    }

    detail::VtkCellCollector collector(model, 0);
    std::int64_t begin = 0;
    for (std::size_t c = 0; c < types.size(); ++c) {
      const std::int64_t end = offsets[c];
      if (end < begin || static_cast<std::size_t>(end) > connectivity.size()) throw ParseFailure("invalid cell offsets");
      collector.addCell(static_cast<int>(types[c]), std::span<const std::int64_t>(connectivity.data() + begin, static_cast<std::size_t>(end - begin)), c);
      begin = end;
    }
    const auto sourceCell = collector.finish();

    for (auto& [key, pending] : arrays) {
      const std::size_t entities = pending.onPoints ? model.nodes.size() : types.size();
      pending.values.resize(entities * static_cast<std::size_t>(pending.components), 0.0);
      Field field;
      field.name = pending.name;
      field.location = pending.onPoints ? FieldLocation::Node : FieldLocation::Element;
      field.components = pending.components;
      field.times = {0.0};
      field.steps.push_back(pending.onPoints ? std::move(pending.values)
                                             : detail::VtkCellCollector::remapCellData(pending.values, pending.components, sourceCell));
      model.fields.push_back(std::move(field));
    }

    detail::unflattenSteps(model);
    detail::decodeModelData(model, detail::CodecOptions{});
    context.progress(1.0f, "done");
    return model;
  }

  // ---------------------------------------------------------------- write

  namespace {

    class VtuWriter {
    public:
      VtuWriter(const bool binary, const bool compress) : m_binary(binary), m_compress(compress) {}

      template <typename T>
      void dataArray(const std::string& name, const char* vtkType, const int components, std::span<const T> values,
                     const bool withTuples = false) {
        m_out += std::format("        <DataArray type=\"{}\" Name=\"{}\"", vtkType, escapeXml(name));
        if (components != 1) m_out += std::format(" NumberOfComponents=\"{}\"", components);
        if (withTuples) m_out += std::format(" NumberOfTuples=\"{}\"", values.size() / static_cast<std::size_t>(std::max(components, 1)));
        m_out += std::format(" format=\"{}\">\n", m_binary ? "binary" : "ascii");
        if (m_binary) {
          std::string raw;
          raw.reserve(values.size() * sizeof(T));
          for (const T v : values) detail::appendValue(raw, v, false);
          m_out += "          ";
          m_out += m_compress ? compressed(raw) : uncompressed(raw);
          m_out.push_back('\n');
        } else {
          for (std::size_t i = 0; i < values.size(); ++i) {
            if (i % 9 == 0) m_out += "          ";
            detail::appendNumber(m_out, values[i]);
            m_out.push_back((i + 1) % 9 == 0 || i + 1 == values.size() ? '\n' : ' ');
          }
        }
        m_out += "        </DataArray>\n";
      }

      std::string& buffer() { return m_out; }

    private:
      static std::string uncompressed(const std::string& raw) {
        std::string block;
        detail::appendValue(block, static_cast<std::uint64_t>(raw.size()), false);
        block += raw;
        return detail::base64Encode(block);
      }

      // vtkZLibDataCompressor layout; header and data are base64-encoded separately like VTK does.
      static std::string compressed(const std::string& raw) {
        constexpr std::size_t kBlockSize = 32768;
        const std::size_t blocks = raw.empty() ? 0 : (raw.size() + kBlockSize - 1) / kBlockSize;
        std::string header;
        std::string data;
        detail::appendValue(header, static_cast<std::uint64_t>(blocks), false);
        detail::appendValue(header, static_cast<std::uint64_t>(kBlockSize), false);
        detail::appendValue(header, static_cast<std::uint64_t>(blocks == 0 ? 0 : raw.size() - (blocks - 1) * kBlockSize), false);
        for (std::size_t b = 0; b < blocks; ++b) {
          const std::size_t size = std::min(kBlockSize, raw.size() - b * kBlockSize);
          uLongf length = compressBound(static_cast<uLong>(size));
          std::string out(length, '\0');
          if (compress2(reinterpret_cast<Bytef*>(out.data()), &length, reinterpret_cast<const Bytef*>(raw.data() + b * kBlockSize),
                        static_cast<uLong>(size), Z_DEFAULT_COMPRESSION) != Z_OK) {
            throw std::runtime_error("zlib compression failed");
          }
          out.resize(length);
          detail::appendValue(header, static_cast<std::uint64_t>(length), false);
          data += out;
        }
        return detail::base64Encode(header) + detail::base64Encode(data);
      }

      bool m_binary;
      bool m_compress;
      std::string m_out;
    };

  } // namespace end

  namespace {

    std::string vtuDocument(const MeshModel& model, const detail::FlatData& flat, const WriteOptions& options,
                            const detail::VtkCells& cells, const IoContext& context) {
      const bool binary = options.encoding == Encoding::Binary || options.compress;
      VtuWriter writer(binary, options.compress);
      auto& out = writer.buffer();

      out += "<?xml version=\"1.0\"?>\n";
      if (!model.title.empty()) out += std::format("<!-- {} -->\n", escapeXml(model.title));
      out += "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" byte_order=\"LittleEndian\" header_type=\"UInt64\"";
      if (options.compress) out += " compressor=\"vtkZLibDataCompressor\"";
      out += ">\n  <UnstructuredGrid>\n";
      if (!flat.globals.empty()) {
        out += "    <FieldData>\n";
        for (const auto& global : flat.globals) {
          writer.dataArray(global.name, "Float64", global.components, std::span<const double>(global.values), true);
        }
        out += "    </FieldData>\n";
      }
      out += std::format("    <Piece NumberOfPoints=\"{}\" NumberOfCells=\"{}\">\n", model.nodes.size(), cells.types.size());

      for (const bool onPoints : {true, false}) {
        bool opened = false;
        for (const auto& array : flat.arrays) {
          if ((array.location == FieldLocation::Node) != onPoints) continue;
          if (!opened) {
            out += onPoints ? "      <PointData>\n" : "      <CellData>\n";
            opened = true;
          }
          writer.dataArray(array.name, "Float64", array.components, std::span<const double>(*array.values));
        }
        if (opened) out += onPoints ? "      </PointData>\n" : "      </CellData>\n";
        if (context.cancelled()) throw detail::CancelledFailure();
      }
      context.progress(0.5f, "geometry");

      std::vector<double> coords(model.nodes.size() * 3);
      for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        for (int a = 0; a < 3; ++a) coords[i * 3 + a] = model.nodes[i].position[a];
      }
      out += "      <Points>\n";
      writer.dataArray("Points", "Float64", 3, std::span<const double>(coords));
      out += "      </Points>\n      <Cells>\n";
      writer.dataArray("connectivity", "Int64", 1, std::span<const std::int64_t>(cells.connectivity));
      writer.dataArray("offsets", "Int64", 1, std::span<const std::int64_t>(cells.offsets));
      writer.dataArray("types", "UInt8", 1, std::span<const std::uint8_t>(cells.types));
      out += "      </Cells>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
      return std::move(out);
    }

    void requireValid(const MeshModel& model) {
      if (const auto problems = model.validate(); !problems.empty()) {
        throw std::invalid_argument("model is inconsistent: " + problems.front());
      }
    }

    // Progress of one sub-file mapped into [begin, end) of the whole operation.
    IoContext subContext(const IoContext& context, const float begin, const float end, std::string stage) {
      IoContext sub;
      sub.isCancelled = context.isCancelled;
      if (context.onProgress) {
        sub.onProgress = [&context, begin, end, stage = std::move(stage)](const float fraction, std::string_view) {
          context.progress(begin + (end - begin) * fraction, stage);
        };
      }
      return sub;
    }

  } // namespace end

  WriteReport writeVtu(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context) {
    requireValid(model);
    WriteReport report;
    report.path = pathToUtf8(path);
    const auto cells = detail::buildVtkCells(model);
    report.warnings.insert(report.warnings.end(), cells.warnings.begin(), cells.warnings.end());
    const auto encoded = detail::encodeModelData(model, detail::CodecOptions{true, true, options.writeTags});
    const auto flat = detail::flattenSteps(model, encoded, options.timeStep);
    report.warnings.insert(report.warnings.end(), flat.warnings.begin(), flat.warnings.end());
    const std::string out = vtuDocument(model, flat, options, cells, context);
    if (context.cancelled()) throw detail::CancelledFailure();
    detail::writeWholeFile(path, out);
    context.progress(1.0f, "done");
    return report;
  }

  // ---------------------------------------------------------------- ParaView collection (.pvd)

  WriteReport writePvd(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context) {
    requireValid(model);
    WriteReport report;
    report.path = pathToUtf8(path);

    // Distinct times of every Time field with a history; a model without one is a single step.
    std::set<double> timeSet;
    for (const auto& field : model.fields) {
      if (field.stepKind == StepKind::Time && field.steps.size() > 1) timeSet.insert(field.times.begin(), field.times.end());
    }
    if (timeSet.empty()) {
      double time = 0.0;
      for (const auto& field : model.fields) {
        if (field.stepKind == StepKind::Time && !field.times.empty()) {
          time = field.times.back();
          break;
        }
      }
      timeSet.insert(time);
    }
    const std::vector<double> times(timeSet.begin(), timeSet.end());

    const std::string stem = pathToUtf8(path.stem());
    const auto folder = path.parent_path() / path.stem();
    std::filesystem::create_directories(folder);
    const auto cells = detail::buildVtkCells(model);
    report.warnings.insert(report.warnings.end(), cells.warnings.begin(), cells.warnings.end());
    const auto encoded = detail::encodeModelData(model, detail::CodecOptions{true, true, options.writeTags});

    std::string collection = "<?xml version=\"1.0\"?>\n<VTKFile type=\"Collection\" version=\"1.0\" byte_order=\"LittleEndian\" header_type=\"UInt64\">\n  <Collection>\n";
    for (std::size_t k = 0; k < times.size(); ++k) {
      const double time = times[k];
      const auto pick = [time](const Field& field) -> const std::vector<double>* {
        if (field.steps.size() == 1) return &field.steps.front();
        for (std::size_t s = 0; s < field.steps.size(); ++s) {
          if (s < field.times.size() && field.times[s] == time) return &field.steps[s];
        }
        return nullptr;
      };
      const auto flat = detail::flattenSteps(model, encoded, -1, pick, time);
      if (k == 0) report.warnings.insert(report.warnings.end(), flat.warnings.begin(), flat.warnings.end());
      const std::string fileName = std::format("{}_{:04}.vtu", stem, k);
      const auto stepContext = subContext(context, static_cast<float>(k) / static_cast<float>(times.size()),
                                          static_cast<float>(k + 1) / static_cast<float>(times.size()),
                                          std::format("step {} of {}", k + 1, times.size()));
      const std::string document = vtuDocument(model, flat, options, cells, stepContext);
      if (context.cancelled()) throw detail::CancelledFailure();
      const auto stepPath = folder / pathFromUtf8(fileName);
      detail::writeWholeFile(stepPath, document);
      report.extraFiles.push_back(pathToUtf8(stepPath));

      std::string timeText;
      detail::appendNumber(timeText, time);
      // Forward slashes: the collection must stay readable on every platform.
      collection += std::format("    <DataSet timestep=\"{}\" group=\"\" part=\"0\" file=\"{}\"/>\n", timeText,
                                escapeXml(stem + "/" + fileName));
    }
    collection += "  </Collection>\n</VTKFile>\n";
    detail::writeWholeFile(path, collection);
    context.progress(1.0f, "done");
    return report;
  }

  MeshModel readPvd(const std::filesystem::path& path, const ReadOptions& options, const IoContext& context) {
    const std::string content = detail::readWholeFile(path);
    XmlParser parser(content);
    const XmlElement root = parser.parseDocument();
    if (root.name != "VTKFile" || root.attribute("type") != "Collection") throw ParseFailure("not a ParaView collection (.pvd)");
    const XmlElement* collection = root.child("Collection");
    if (!collection) throw ParseFailure("Collection element missing");

    struct Entry { double time; std::filesystem::path file; };
    std::vector<Entry> entries;
    std::size_t skippedParts = 0;
    for (const auto& dataSet : collection->children) {
      if (dataSet.name != "DataSet") continue;
      if (detail::Cursor::parseNumber<int>(dataSet.attribute("part", "0")) != 0) {
        ++skippedParts;
        continue;
      }
      auto file = pathFromUtf8(dataSet.attribute("file"));
      if (file.is_relative()) file = path.parent_path() / file;
      entries.push_back(Entry{detail::Cursor::parseNumber<double>(dataSet.attribute("timestep", "0")), std::move(file)});
    }
    if (entries.empty()) throw ParseFailure("the collection lists no data sets");
    std::ranges::stable_sort(entries, {}, &Entry::time);

    MeshModel model;
    // Time fields per (name, location), one step per file that contains them.
    struct History { Field field; std::size_t files{0}; bool constant{true}; };
    std::map<std::pair<std::string, int>, History> histories;
    std::vector<std::pair<std::string, int>> order;
    for (std::size_t k = 0; k < entries.size(); ++k) {
      const auto stepContext = subContext(context, static_cast<float>(k) / static_cast<float>(entries.size()),
                                          static_cast<float>(k + 1) / static_cast<float>(entries.size()),
                                          std::format("step {} of {}", k + 1, entries.size()));
      if (entries[k].file.extension() != ".vtu") {
        throw ParseFailure("collection entry '" + pathToUtf8(entries[k].file) + "' is not a .vtu file");
      }
      MeshModel step = readVtu(entries[k].file, options, stepContext);
      if (k == 0) {
        model = std::move(step);
        std::vector<Field> timeFields;
        std::erase_if(model.fields, [&](Field& field) {
          if (field.stepKind != StepKind::Time) return false;
          timeFields.push_back(std::move(field));
          return true;
        });
        for (auto& field : timeFields) {
          const std::pair<std::string, int> key{field.name, static_cast<int>(field.location)};
          field.times = {entries[k].time};
          histories[key] = History{std::move(field), 1, true};
          order.push_back(key);
        }
        continue;
      }
      if (step.nodes.size() != model.nodes.size() || step.elementCount() != model.elementCount()) {
        throw ParseFailure(std::format("collection step {} has a different mesh ({} nodes, {} elements; {} / {} expected)",
          k + 1, step.nodes.size(), step.elementCount(), model.nodes.size(), model.elementCount()));
      }
      model.warnings.insert(model.warnings.end(), step.warnings.begin(), step.warnings.end());
      for (auto& field : step.fields) {
        if (field.stepKind != StepKind::Time || field.steps.empty()) continue;
        const std::pair<std::string, int> key{field.name, static_cast<int>(field.location)};
        auto [it, inserted] = histories.try_emplace(key);
        auto& history = it->second;
        if (inserted) {
          history.field = field;
          history.field.steps.clear();
          history.field.times.clear();
          history.constant = false;
          order.push_back(key);
        } else if (history.field.components != field.components) {
          model.warnings.push_back(std::format("field '{}' changes its component count in step {}; step skipped", field.name, k + 1));
          continue;
        }
        history.constant = history.constant && !history.field.steps.empty() && history.field.steps.front() == field.steps.back();
        history.field.times.push_back(entries[k].time);
        history.field.steps.push_back(std::move(field.steps.back()));
        ++history.files;
      }
    }
    for (const auto& key : order) {
      auto& history = histories.at(key);
      if (history.constant && history.files == entries.size() && entries.size() > 1) {
        // Written once per file by writePvd(): a single-step field.
        history.field.steps.resize(1);
        history.field.times.resize(1);
      }
      model.fields.push_back(std::move(history.field));
    }
    if (skippedParts > 0) model.warnings.push_back(std::format("{} data sets with part > 0 were skipped (multi-part collections are not supported)", skippedParts));
    context.progress(1.0f, "done");
    return model;
  }

} // namespace anaf::IO::formats end
