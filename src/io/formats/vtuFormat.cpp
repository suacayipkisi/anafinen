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
        if (at == std::string_view::npos) throw detail::ParseFailure("malformed XML: missing '" + std::string(token) + "'");
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
        if (m_pos >= m_data.size() || m_data[m_pos] != '<') throw detail::ParseFailure("malformed XML: '<' expected");
        ++m_pos;
        XmlElement element;
        element.name = parseName();
        // Attributes.
        while (true) {
          skipSpace();
          if (m_pos >= m_data.size()) throw detail::ParseFailure("malformed XML: unterminated tag");
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
          if (m_pos >= m_data.size() || m_data[m_pos] != '=') throw detail::ParseFailure("malformed XML attribute '" + key + "'");
          ++m_pos;
          skipSpace();
          const char quote = m_data[m_pos];
          if (quote != '"' && quote != '\'') throw detail::ParseFailure("malformed XML attribute value");
          const auto end = m_data.find(quote, m_pos + 1);
          if (end == std::string_view::npos) throw detail::ParseFailure("unterminated XML attribute value");
          element.attributes[key] = decodeEntities(m_data.substr(m_pos + 1, end - m_pos - 1));
          m_pos = end + 1;
        }

        if (element.name == "AppendedData") {
          // Raw appended data may contain any byte, including '<': stop XML parsing here.
          m_appendedEncoding = element.attribute("encoding", "raw");
          const auto marker = m_data.find('_', m_pos);
          if (marker == std::string_view::npos) throw detail::ParseFailure("AppendedData without '_' marker");
          m_appendedStart = marker + 1;
          m_pos = m_data.size();
          return element;
        }

        // Content: child elements and character data.
        while (true) {
          const auto next = m_data.find('<', m_pos);
          if (next == std::string_view::npos) throw detail::ParseFailure("malformed XML: missing end tag for " + element.name);
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
      throw detail::ParseFailure("unsupported DataArray type '" + type + "'");
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
      if (at + layout.headerSize > bytes.size()) throw detail::ParseFailure("truncated binary header");
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

    // Payload bytes of a binary / appended array. Raw (not base64) uncompressed data is a view
    // into the file itself; anything decoded or decompressed lives in storage. Offset and length
    // instead of a view into storage: a moved short std::string would leave the view dangling.
    struct Payload {
      std::string storage;
      std::string_view file;
      bool owned{false};
      std::size_t offset{0};
      std::size_t length{0};

      std::string_view bytes() const { return (owned ? std::string_view(storage) : file).substr(offset, length); }
    };

    Payload decodeBinaryPayload(const std::string_view source, const bool base64, const Layout& layout) {
      // The first `bytes` bytes of the payload: decoded for base64, the file itself for raw data.
      std::string decoded;
      auto available = [&](const std::size_t bytes) -> std::string_view {
        if (base64) {
          decoded = base64AtLeast(source, bytes);
          return decoded;
        }
        if (bytes > source.size()) throw detail::ParseFailure("truncated binary array");
        return source.substr(0, bytes);
      };

      if (!layout.zlib) {
        const std::uint64_t length = headerWord(available(layout.headerSize), 0, layout);
        const std::string_view all = available(layout.headerSize + length);
        if (all.size() < layout.headerSize + length) throw detail::ParseFailure("truncated binary array");
        Payload payload{.storage = {}, .file = source, .owned = base64, .offset = layout.headerSize, .length = length};
        if (base64) payload.storage = std::move(decoded);
        return payload;
      }

      // Compressed: [blocks, blockSize, lastBlockSize, compressedSize x blocks] then the blocks.
      std::uint64_t blocks = 0, blockSize = 0, lastBlockSize = 0;
      {
        const std::string_view first = available(layout.headerSize * 3);
        blocks = headerWord(first, 0, layout);
        blockSize = headerWord(first, 1, layout);
        lastBlockSize = headerWord(first, 2, layout);
      }
      const std::size_t headerBytes = layout.headerSize * (3 + blocks);
      std::uint64_t compressedTotal = 0;
      {
        const std::string_view header = available(headerBytes);
        for (std::uint64_t b = 0; b < blocks; ++b) compressedTotal += headerWord(header, 3 + b, layout);
      }

      // For base64 the decoder flushes at padding, so a header and data encoded separately
      // (VTK's convention) decode the same as one joint base64 string.
      const std::string_view all = available(headerBytes + compressedTotal);
      if (all.size() < headerBytes + compressedTotal) throw detail::ParseFailure("truncated compressed array");

      // Every block is inflated straight into the output.
      Payload payload{.storage = {}, .file = {}, .owned = true, .offset = 0, .length = 0};
      std::string& out = payload.storage;
      out.resize(blocks * blockSize);
      std::size_t written = 0;
      std::size_t pos = headerBytes;
      for (std::uint64_t b = 0; b < blocks; ++b) {
        const auto compressedSize = headerWord(all, 3 + b, layout);
        const std::uint64_t expected = (b + 1 == blocks && lastBlockSize != 0) ? lastBlockSize : blockSize;
        if (written + expected > out.size()) out.resize(written + expected);
        uLongf length = static_cast<uLongf>(expected);
        const int status = uncompress(reinterpret_cast<Bytef*>(out.data() + written), &length,
                                      reinterpret_cast<const Bytef*>(all.data() + pos), static_cast<uLong>(compressedSize));
        if (status != Z_OK) throw detail::ParseFailure("zlib decompression failed");
        written += length;
        pos += compressedSize;
      }
      out.resize(written);
      payload.length = written;
      return payload;
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
        Payload payload;
        if (format == "binary") {
          // One text piece (the usual case) is used in place; split text is joined first.
          std::string joined;
          const std::string_view text = array.textPieces.size() == 1 ? array.textPieces.front() : std::string_view(joined = array.text());
          payload = decodeBinaryPayload(text, true, layout);
        } else if (format == "appended") {
          const auto offset = detail::Cursor::parseNumber<std::size_t>(array.attribute("offset", "0"));
          if (offset > layout.appended.size()) throw detail::ParseFailure("appended offset beyond the data section");
          payload = decodeBinaryPayload(layout.appended.substr(offset), layout.appendedBase64, layout);
        } else {
          throw detail::ParseFailure("unsupported DataArray format '" + format + "'");
        }
        const std::string_view bytes = payload.bytes();
        const std::size_t size = scalarSize(type);
        const std::size_t count = bytes.size() / size;
        values.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
          if constexpr (std::is_integral_v<Out>) values[i] = static_cast<Out>(integerAt(bytes.data() + i * size, type, layout.bigEndian));
          else values[i] = static_cast<Out>(scalarAt(bytes.data() + i * size, type, layout.bigEndian));
        }
      }
      if (expectedCount != 0 && values.size() != expectedCount) {
        throw detail::ParseFailure(std::format("DataArray '{}' has {} values, {} expected", array.attribute("Name"), values.size(), expectedCount));
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

  namespace {
    // The mesh of a .vtu as parsed (before decodeModelData() sets tags), for the later files of
    // a .pvd collection: they repeat it, so their Points / Cells arrays need not be decoded again.
    struct VtuMesh {
      std::vector<Node> nodes;
      std::vector<ElementBlock> blocks;
      std::vector<std::size_t> sourceCell; // file cell of every element (VtkCellCollector)
      std::size_t fileCells{0};
    };

    // known: reuse that mesh when the file's point and cell counts match it (else parse as usual);
    // found: receives the parsed mesh.
    MeshModel readVtuFile(const std::filesystem::path& path, const IoContext& context, const VtuMesh* known, VtuMesh* found);
  } // namespace end

  MeshModel readVtu(const std::filesystem::path& path, const ReadOptions&, const IoContext& context) {
    return readVtuFile(path, context, nullptr, nullptr);
  }

  namespace {
    MeshModel readVtuFile(const std::filesystem::path& path, const IoContext& context, const VtuMesh* known, VtuMesh* found) {
      const std::string content = detail::readWholeFile(path);
      XmlParser parser(content);
      const XmlElement root = parser.parseDocument();
      if (root.name != "VTKFile") throw detail::ParseFailure("not a VTK XML file (root element is '" + root.name + "')");
      if (root.attribute("type") == "Collection") throw detail::ParseFailure("this is a ParaView collection: read it as .pvd");
      if (root.attribute("type") != "UnstructuredGrid") {
        throw detail::ParseFailure("VTK XML type '" + root.attribute("type") + "' is not supported (UnstructuredGrid expected)");
      }
      Layout layout;
      layout.bigEndian = root.attribute("byte_order", "LittleEndian") == "BigEndian";
      layout.headerSize = root.attribute("header_type", "UInt32") == "UInt64" ? 8 : 4;
      const std::string compressor = root.attribute("compressor");
      if (!compressor.empty() && compressor != "vtkZLibDataCompressor") {
        throw detail::ParseFailure("compressor '" + compressor + "' is not supported (only vtkZLibDataCompressor)");
      }
      layout.zlib = !compressor.empty();
      if (parser.appendedStart() != std::string_view::npos) {
        layout.appended = std::string_view(content).substr(parser.appendedStart());
        layout.appendedBase64 = parser.appendedEncoding() == "base64";
      }

      const XmlElement* grid = root.child("UnstructuredGrid");
      if (!grid) throw detail::ParseFailure("UnstructuredGrid element missing");

      // A known mesh is reused only when this file has the same number of points and cells.
      bool reuse = false;
      if (known) {
        std::size_t pointTotal = 0, fileCellTotal = 0;
        for (const auto& piece : grid->children) {
          if (piece.name != "Piece") continue;
          pointTotal += detail::Cursor::parseNumber<std::size_t>(piece.attribute("NumberOfPoints", "0"));
          fileCellTotal += detail::Cursor::parseNumber<std::size_t>(piece.attribute("NumberOfCells", "0"));
        }
        reuse = pointTotal == known->nodes.size() && fileCellTotal == known->fileCells;
      }

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
      std::size_t pointsSeen = 0;

      std::size_t pieceIndex = 0;
      for (const auto& piece : grid->children) {
        if (piece.name != "Piece") continue;
        if (context.cancelled()) throw detail::CancelledFailure();
        const auto points = detail::Cursor::parseNumber<std::size_t>(piece.attribute("NumberOfPoints", "0"));
        const auto cellCount = detail::Cursor::parseNumber<std::size_t>(piece.attribute("NumberOfCells", "0"));
        const std::size_t pointOffset = reuse ? pointsSeen : model.nodes.size();
        pointsSeen += points;

        if (points > 0 && !reuse) {
          const XmlElement* pointsElement = piece.child("Points");
          const XmlElement* coordsArray = pointsElement ? pointsElement->child("DataArray") : nullptr;
          if (!coordsArray) throw detail::ParseFailure("Points/DataArray missing");
          const auto coords = readDataArray<double>(*coordsArray, layout, points * 3);
          for (std::size_t i = 0; i < points; ++i) {
            model.nodes.push_back(Node{pointOffset + i + 1, {coords[i * 3], coords[i * 3 + 1], coords[i * 3 + 2]}});
          }
        }
        if (cellCount > 0 && !reuse) {
          const XmlElement* cells = piece.child("Cells");
          const auto* connectivityArray = findArray(cells, "connectivity");
          const auto* offsetsArray = findArray(cells, "offsets");
          const auto* typesArray = findArray(cells, "types");
          if (!connectivityArray || !offsetsArray || !typesArray) throw detail::ParseFailure("Cells arrays missing");
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

      std::vector<std::size_t> parsedSourceCell;
      if (reuse) {
        model.nodes = known->nodes;
        model.blocks = known->blocks;
      } else {
        detail::VtkCellCollector collector(model, 0);
        std::int64_t begin = 0;
        for (std::size_t c = 0; c < types.size(); ++c) {
          const std::int64_t end = offsets[c];
          if (end < begin || static_cast<std::size_t>(end) > connectivity.size()) throw detail::ParseFailure("invalid cell offsets");
          collector.addCell(static_cast<int>(types[c]), std::span<const std::int64_t>(connectivity.data() + begin, static_cast<std::size_t>(end - begin)), c);
          begin = end;
        }
        parsedSourceCell = collector.finish();
        if (found) *found = VtuMesh{model.nodes, model.blocks, parsedSourceCell, types.size()};
      }
      const auto& sourceCell = reuse ? known->sourceCell : parsedSourceCell;
      const std::size_t fileCells = reuse ? known->fileCells : types.size();

      for (auto& [key, pending] : arrays) {
        const std::size_t entities = pending.onPoints ? model.nodes.size() : fileCells;
        pending.values.resize(entities * static_cast<std::size_t>(pending.components), 0.0);
        Field field;
        field.name = pending.name;
        field.location = pending.onPoints ? E_FieldLocation::Node : E_FieldLocation::Element;
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
  } // namespace end

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
          // Uncompressed: the UInt64 byte count and the values in one buffer, base64-encoded
          // straight into m_out (no block copy, no encoded temporary).
          std::string raw;
          raw.reserve(sizeof(std::uint64_t) + values.size() * sizeof(T));
          if (!m_compress) detail::appendValue(raw, static_cast<std::uint64_t>(values.size() * sizeof(T)), false);
          for (const T v : values) detail::appendValue(raw, v, false);
          m_out += "          ";
          if (m_compress) appendCompressed(m_out, raw);
          else detail::base64Append(m_out, raw);
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
      // vtkZLibDataCompressor layout; header and data are base64-encoded separately like VTK does.
      static void appendCompressed(std::string& target, const std::string& raw) {
        constexpr std::size_t blockSize = 32768;
        const std::size_t blocks = raw.empty() ? 0 : (raw.size() + blockSize - 1) / blockSize;
        std::string header;
        std::string data;
        detail::appendValue(header, static_cast<std::uint64_t>(blocks), false);
        detail::appendValue(header, static_cast<std::uint64_t>(blockSize), false);
        detail::appendValue(header, static_cast<std::uint64_t>(blocks == 0 ? 0 : raw.size() - (blocks - 1) * blockSize), false);
        for (std::size_t b = 0; b < blocks; ++b) {
          const std::size_t size = std::min(blockSize, raw.size() - b * blockSize);
          uLongf length = compressBound(static_cast<uLong>(size));
          std::string out(length, '\0');
          if (compress2(reinterpret_cast<Bytef*>(out.data()), &length, reinterpret_cast<const Bytef*>(raw.data() + b * blockSize),
                        static_cast<uLong>(size), Z_DEFAULT_COMPRESSION) != Z_OK) {
            throw std::runtime_error("zlib compression failed");
          }
          out.resize(length);
          detail::appendValue(header, static_cast<std::uint64_t>(length), false);
          data += out;
        }
        detail::base64Append(target, header);
        detail::base64Append(target, data);
      }

      bool m_binary;
      bool m_compress;
      std::string m_out;
    };

  } // namespace end

  namespace {

    std::string vtuDocument(const MeshModel& model, const detail::FlatData& flat, const WriteOptions& options,
                            const detail::VtkCells& cells, const IoContext& context) {
      const bool binary = options.encoding == E_Encoding::Binary || options.compress;
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
          if ((array.location == E_FieldLocation::Node) != onPoints) continue;
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
      if (field.stepKind == E_StepKind::Time && field.steps.size() > 1) timeSet.insert(field.times.begin(), field.times.end());
    }
    if (timeSet.empty()) {
      double time = 0.0;
      for (const auto& field : model.fields) {
        if (field.stepKind == E_StepKind::Time && !field.times.empty()) {
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

  MeshModel readPvd(const std::filesystem::path& path, const ReadOptions&, const IoContext& context) {
    const std::string content = detail::readWholeFile(path);
    XmlParser parser(content);
    const XmlElement root = parser.parseDocument();
    if (root.name != "VTKFile" || root.attribute("type") != "Collection") throw detail::ParseFailure("not a ParaView collection (.pvd)");
    const XmlElement* collection = root.child("Collection");
    if (!collection) throw detail::ParseFailure("Collection element missing");

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
    if (entries.empty()) throw detail::ParseFailure("the collection lists no data sets");
    std::ranges::stable_sort(entries, {}, &Entry::time);

    MeshModel model;
    // Time fields per (name, location), one step per file that contains them.
    struct History { Field field; std::size_t files{0}; bool constant{true}; };
    std::map<std::pair<std::string, int>, History> histories;
    VtuMesh firstMesh;
    std::vector<std::pair<std::string, int>> order;
    for (std::size_t k = 0; k < entries.size(); ++k) {
      const auto stepContext = subContext(context, static_cast<float>(k) / static_cast<float>(entries.size()),
                                          static_cast<float>(k + 1) / static_cast<float>(entries.size()),
                                          std::format("step {} of {}", k + 1, entries.size()));
      if (entries[k].file.extension() != ".vtu") {
        throw detail::ParseFailure("collection entry '" + pathToUtf8(entries[k].file) + "' is not a .vtu file");
      }
      // Later files repeat the first one's mesh: it is reused instead of decoded again (a file
      // with other point / cell counts is parsed in full and refused below).
      MeshModel step = readVtuFile(entries[k].file, stepContext, k == 0 ? nullptr : &firstMesh, k == 0 ? &firstMesh : nullptr);
      if (k == 0) {
        model = std::move(step);
        std::vector<Field> timeFields;
        std::erase_if(model.fields, [&](Field& field) {
          if (field.stepKind != E_StepKind::Time) return false;
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
        throw detail::ParseFailure(std::format("collection step {} has a different mesh ({} nodes, {} elements; {} / {} expected)",
          k + 1, step.nodes.size(), step.elementCount(), model.nodes.size(), model.elementCount()));
      }
      model.warnings.insert(model.warnings.end(), step.warnings.begin(), step.warnings.end());
      for (auto& field : step.fields) {
        if (field.stepKind != E_StepKind::Time || field.steps.empty()) continue;
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
