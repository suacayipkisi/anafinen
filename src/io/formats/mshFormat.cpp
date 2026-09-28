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

// Native Gmsh MSH reader / writer (no Gmsh library involved).
//
// Read:  MSH 1.0; 2.0 / 2.1 / 2.2 ASCII and binary; 4.0 ASCII; 4.1 ASCII and binary.
//        Sections: $MeshFormat, $PhysicalNames, $Entities, $Nodes/$NOD, $Elements/$ELM,
//        $NodeData, $ElementData (every time step). Other sections are skipped.
// Write: MSH 2.2 or 4.1, ASCII or binary.
//
// Why native: Gmsh's own writer renumbers tags and drops unreferenced nodes in MSH 2.2, writes
// ASCII doubles with 16 digits (not round-trip exact), and the Gmsh 4.15 build shipped by
// Fedora aborts while reading any binary MSH 4.1 file. Native code also needs no global lock.
//
// Mapping:
//   element sets <-> physical groups (4.1: through $Entities; 2.2: element tag list, an element
//                    that belongs to several sets is listed once per set with the same tag)
//   dimension-0 physical groups --> node sets (on read)
//   node sets, BCs, loads, attributes, fields <-> $NodeData / $ElementData (all steps)
//   global data, field step kinds and step labels <-> $AnafData (own section, always ASCII;
//                    Gmsh skips sections it does not know):
//       $AnafData
//       1                                  format version
//       <record count>
//       GLOBAL <components> <tuples>       then the quoted name on its own line, then the values
//       STEPS <N|E> <kind> <steps> <0|1>   then the quoted field name, then one quoted label per
//                                          step when the last flag is 1
//       Global names and labels escape '\\', '"' and line breaks with a backslash; field names
//       follow the $NodeData rule ('"' becomes '\'') so that both sections name a field alike.
//       $EndAnafData

#include "formats.hpp"
#include "../detail/modelCodec.hpp"
#include "../detail/textIo.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace anaf::IO::formats {

  namespace {

    using detail::Cursor;
    using detail::ParseFailure;

    // Map key used instead of std::pair<int, int>. Gmsh keeps its physical names in a
    // std::map<std::pair<int, int>, std::string>; the same instantiation here would be exported
    // from the executable and interpose libgmsh's own copy. With a Gmsh SDK built by an older
    // GCC (AUR gmsh-bin) that mixes two libstdc++ implementations and loses physical names.
    // A type with internal linkage keeps every instantiation local to this file.
    struct IntPair {
      int first;
      int second;
      auto operator<=>(const IntPair&) const = default;
    };

    void checkCancel(const IoContext& context) {
      if (context.cancelled()) throw detail::CancelledFailure();
    }

    // Node counts of Gmsh element types that anafinen does not model, so they can be skipped
    // in binary files (types from GmshDefines.h).
    std::optional<int> gmshNodeCount(const int gmshType) {
      if (const auto type = elementTypeFromGmsh(gmshType)) return elementInfo(*type).nodeCount;
      static const std::map<int, int> extra{
        {13, 18}, {14, 14}, {20, 9}, {21, 10}, {22, 12}, {23, 15}, {24, 15}, {25, 21}, {26, 4}, {27, 5}, {28, 6},
        {29, 20}, {30, 35}, {31, 56}, {36, 16}, {37, 25}, {38, 36}, {92, 64}, {93, 125},
      };
      const auto it = extra.find(gmshType);
      return it == extra.end() ? std::nullopt : std::optional<int>(it->second);
    }

    // Quoted, backslash-escaped string of $AnafData (see the header).
    std::string escapedQuoted(std::string_view text) {
      std::string out = "\"";
      for (const char c : text) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n') out += "\\n";
        else out.push_back(c);
      }
      return out + "\"\n";
    }

    std::string unescapeQuoted(std::string_view line) {
      const auto first = line.find('"');
      const auto last = line.rfind('"');
      if (first == std::string_view::npos || last <= first) return std::string(line);
      std::string out;
      for (std::size_t i = first + 1; i < last; ++i) {
        if (line[i] == '\\' && i + 1 < last) {
          ++i;
          out.push_back(line[i] == 'n' ? '\n' : line[i]);
        } else {
          out.push_back(line[i]);
        }
      }
      return out;
    }

    std::string quotedText(std::string_view line) {
      const auto first = line.find('"');
      const auto last = line.rfind('"');
      if (first != std::string_view::npos && last > first) return std::string(line.substr(first + 1, last - first - 1));
      return std::string(line);
    }

    // ================================================================ reader

    class MshReader {
    public:
      MshReader(std::string_view data, const IoContext& context) : m_cursor(data), m_context(context) {}

      MeshModel read() {
        while (true) {
          const auto token = m_cursor.token();
          if (token.empty()) break;
          checkCancel(m_context);
          if (token.front() != '$') throw ParseFailure("MSH: section header expected, got '" + std::string(token.substr(0, 40)) + "'");
          const std::string section(token.substr(1));
          m_cursor.line();
          if (section == "MeshFormat") readFormat();
          else if (section == "PhysicalNames") readPhysicalNames();
          else if (section == "Entities") readEntities();
          else if (section == "Nodes") m_version >= 4.0 ? readNodes4() : readNodes2();
          else if (section == "NOD") readNodes1();
          else if (section == "Elements") m_version >= 4.0 ? readElements4() : readElements2();
          else if (section == "ELM") readElements1();
          else if (section == "NodeData") readData(FieldLocation::Node, section);
          else if (section == "ElementData") readData(FieldLocation::Element, section);
          else if (section == "AnafData") readAnafData();
          else skipSection(section);
        }
        if (!m_sawNodes) throw ParseFailure("MSH: no $Nodes section");
        finishSets();
        finishFields();
        applyAnafData();
        for (const auto& [type, count] : m_skippedTypes) {
          m_model.warnings.push_back(std::format("{} elements of unsupported Gmsh type {} were skipped", count, type));
        }
        detail::decodeModelData(m_model, detail::CodecOptions{true, false, false});
        return std::move(m_model);
      }

    private:
      // ---------------------------------------------------------- primitives

      bool binary() const { return m_binary; }

      template <typename T>
      T raw() {
        const auto bytes = m_cursor.bytes(sizeof(T));
        return detail::loadValue<T>(bytes.data(), m_bigEndian);
      }

      // Values that are ASCII tokens in text files and fixed-size binary values in binary files.
      std::uint64_t sizeValue() { return binary() ? raw<std::uint64_t>() : m_cursor.number<std::uint64_t>(); }
      int intValue() { return binary() ? raw<std::int32_t>() : m_cursor.number<int>(); }
      double doubleValue() { return binary() ? raw<double>() : m_cursor.number<double>(); }

      void expectEnd(const std::string& section) {
        const auto token = m_cursor.token();
        if (token != "$End" + section && token != "$END" + section) {
          throw ParseFailure("MSH: $End" + section + " expected, got '" + std::string(token.substr(0, 40)) + "'");
        }
      }

      void skipSection(const std::string& section) {
        const std::string end = "\n$End" + section;
        const auto data = m_cursor.data();
        const auto at = data.find(end, m_cursor.position() > 0 ? m_cursor.position() - 1 : 0);
        if (at == std::string_view::npos) throw ParseFailure("MSH: unterminated section $" + section);
        m_cursor.seek(at + end.size());
        m_cursor.line();
      }

      // ---------------------------------------------------------- sections

      void readFormat() {
        m_version = m_cursor.number<double>();
        m_binary = m_cursor.number<int>() == 1;
        const int dataSize = m_cursor.number<int>();
        if (dataSize != 8) throw ParseFailure(std::format("MSH: data size {} not supported (8 expected)", dataSize));
        if (m_version < 2.0 || m_version >= 5.0) throw ParseFailure(std::format("MSH: version {} not supported", m_version));
        if (m_binary && m_version < 4.1 && m_version >= 4.0) throw ParseFailure("MSH: binary 4.0 is not supported (use 4.1 or 2.2)");
        m_cursor.line();
        if (m_binary) {
          const auto marker = m_cursor.bytes(4);
          std::int32_t one;
          std::memcpy(&one, marker.data(), 4);
          if (one != 1) {
            if (detail::byteSwap(one) != 1) throw ParseFailure("MSH: invalid binary endianness marker");
            m_bigEndian = std::endian::native == std::endian::little;
          }
          m_cursor.skipLineBreak();
        }
        expectEnd("MeshFormat");
      }

      void readPhysicalNames() {
        const auto count = m_cursor.number<std::size_t>();
        for (std::size_t i = 0; i < count; ++i) {
          const int dim = m_cursor.number<int>();
          const int tag = m_cursor.number<int>();
          m_cursor.skipSpace();
          m_physicalNames[{dim, tag}] = quotedText(m_cursor.line());
        }
        expectEnd("PhysicalNames");
      }

      void readEntities() {
        std::array<std::uint64_t, 4> counts{};
        for (auto& c : counts) c = sizeValue();
        const bool v40 = m_version < 4.1;
        for (int dim = 0; dim < 4; ++dim) {
          for (std::uint64_t e = 0; e < counts[static_cast<std::size_t>(dim)]; ++e) {
            const int tag = intValue();
            const int coordinates = (dim == 0 && !v40) ? 3 : 6;
            for (int k = 0; k < coordinates; ++k) doubleValue();
            const auto physicalCount = sizeValue();
            std::vector<int> physicals;
            for (std::uint64_t p = 0; p < physicalCount; ++p) physicals.push_back(intValue());
            if (!physicals.empty()) m_entityPhysicals[{dim, tag}] = std::move(physicals);
            if (dim > 0) {
              const auto bounding = sizeValue();
              for (std::uint64_t b = 0; b < bounding; ++b) intValue();
            }
          }
        }
        if (binary()) m_cursor.skipLineBreak();
        expectEnd("Entities");
      }

      void addNode(const std::uint64_t tag, const std::array<double, 3>& position) {
        const auto index = static_cast<std::uint32_t>(m_model.nodes.size());
        if (!m_nodeIndex.emplace(tag, index).second) throw ParseFailure(std::format("MSH: duplicate node tag {}", tag));
        m_model.nodes.push_back(Node{tag, position});
      }

      void readNodes1() {
        m_version = 1.0;
        const auto count = m_cursor.number<std::size_t>();
        m_model.nodes.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
          const auto tag = m_cursor.number<std::uint64_t>();
          const double x = m_cursor.number<double>();
          const double y = m_cursor.number<double>();
          const double z = m_cursor.number<double>();
          addNode(tag, {x, y, z});
        }
        m_sawNodes = true;
        expectEnd("NOD");
      }

      void readNodes2() {
        const auto count = m_cursor.number<std::size_t>();
        m_model.nodes.reserve(count);
        if (binary()) m_cursor.skipLineBreak();
        for (std::size_t i = 0; i < count; ++i) {
          const std::uint64_t tag = binary() ? static_cast<std::uint64_t>(raw<std::int32_t>()) : m_cursor.number<std::uint64_t>();
          const double x = doubleValue();
          const double y = doubleValue();
          const double z = doubleValue();
          addNode(tag, {x, y, z});
        }
        if (binary()) m_cursor.skipLineBreak();
        m_sawNodes = true;
        expectEnd("Nodes");
      }

      void readNodes4() {
        const bool v40 = m_version < 4.1;
        const auto blocks = sizeValue();
        const auto total = sizeValue();
        if (!v40) {
          sizeValue(); // min tag
          sizeValue(); // max tag
        }
        m_model.nodes.reserve(total);
        for (std::uint64_t b = 0; b < blocks; ++b) {
          int dim = 0;
          if (v40) {
            intValue(); // entity tag
            dim = intValue();
          } else {
            dim = intValue();
            intValue(); // entity tag
          }
          const int parametric = intValue();
          const auto count = sizeValue();
          const int extra = parametric ? std::min(dim, 3) : 0;
          if (v40) {
            for (std::uint64_t i = 0; i < count; ++i) {
              const auto nodeTag = m_cursor.number<std::uint64_t>();
              const double x = m_cursor.number<double>();
              const double y = m_cursor.number<double>();
              const double z = m_cursor.number<double>();
              for (int k = 0; k < extra; ++k) m_cursor.number<double>();
              addNode(nodeTag, {x, y, z});
            }
          } else {
            std::vector<std::uint64_t> tags(count);
            for (auto& t : tags) t = sizeValue();
            for (std::uint64_t i = 0; i < count; ++i) {
              const double x = doubleValue();
              const double y = doubleValue();
              const double z = doubleValue();
              for (int k = 0; k < extra; ++k) doubleValue();
              addNode(tags[i], {x, y, z});
            }
          }
          checkCancel(m_context);
        }
        if (binary()) m_cursor.skipLineBreak();
        m_sawNodes = true;
        m_context.progress(0.3f, "nodes");
        expectEnd("Nodes");
      }

      std::uint32_t nodeIndexOf(const std::uint64_t tag) const {
        const auto it = m_nodeIndex.find(tag);
        if (it == m_nodeIndex.end()) throw ParseFailure(std::format("MSH: element references unknown node {}", tag));
        return it->second;
      }

      // Adds one element (or, for a tag already seen, only records the extra physical group).
      void addElement(const int gmshType, const std::uint64_t tag, const std::vector<std::uint64_t>& nodeTags, const int entity,
                      const int dim, const std::vector<int>& physicals) {
        const auto type = elementTypeFromGmsh(gmshType);
        if (!type) {
          ++m_skippedTypes[gmshType];
          return;
        }
        ElementLocation location;
        if (const auto existing = m_elementByTag.find(tag); existing != m_elementByTag.end()) {
          if (m_model.blocks[existing->second.block].type != *type) {
            throw ParseFailure(std::format("MSH: element tag {} used for two different element types", tag));
          }
          location = existing->second; // MSH 2 lists an element once per physical group
        } else {
          auto& block = m_model.blockFor(*type);
          location = ElementLocation{static_cast<std::size_t>(&block - m_model.blocks.data()), block.size()};
          block.tags.push_back(tag);
          block.entityTags.push_back(entity);
          for (const auto nodeTag : nodeTags) block.connectivity.push_back(nodeIndexOf(nodeTag));
          m_elementByTag.emplace(tag, location);
        }
        for (const int physical : physicals) {
          if (physical != 0) m_physicalMembers[{dim, physical}].push_back(location);
        }
      }

      static int dimensionOf(const int gmshType) {
        const auto type = elementTypeFromGmsh(gmshType);
        return type ? elementInfo(*type).dimension : 0;
      }

      void readElements1() {
        const auto count = m_cursor.number<std::size_t>();
        std::vector<std::uint64_t> nodes;
        for (std::size_t i = 0; i < count; ++i) {
          const auto tag = m_cursor.number<std::uint64_t>();
          const int type = m_cursor.number<int>();
          const int physical = m_cursor.number<int>();
          const int entity = m_cursor.number<int>();
          const auto n = m_cursor.number<std::size_t>();
          nodes.resize(n);
          for (auto& node : nodes) node = m_cursor.number<std::uint64_t>();
          addElement(type, tag, nodes, entity, dimensionOf(type), {physical});
        }
        expectEnd("ELM");
      }

      void readElements2() {
        const auto count = m_cursor.number<std::size_t>();
        std::vector<std::uint64_t> nodes;
        if (!binary()) {
          for (std::size_t i = 0; i < count; ++i) {
            const auto tag = m_cursor.number<std::uint64_t>();
            const int type = m_cursor.number<int>();
            const int tagCount = m_cursor.number<int>();
            std::vector<int> tags(static_cast<std::size_t>(std::max(tagCount, 0)));
            for (auto& t : tags) t = m_cursor.number<int>();
            const auto n = gmshNodeCount(type);
            if (!n) {
              m_cursor.line(); // unknown type: the rest of the line holds its nodes
              ++m_skippedTypes[type];
              continue;
            }
            nodes.resize(static_cast<std::size_t>(*n));
            for (auto& node : nodes) node = m_cursor.number<std::uint64_t>();
            addElement(type, tag, nodes, tags.size() > 1 ? tags[1] : 0, dimensionOf(type),
                       tags.empty() ? std::vector<int>{} : std::vector<int>{tags[0]});
          }
        } else {
          m_cursor.skipLineBreak();
          std::size_t read = 0;
          while (read < count) {
            const int type = raw<std::int32_t>();
            const int following = raw<std::int32_t>();
            const int tagCount = raw<std::int32_t>();
            const auto n = gmshNodeCount(type);
            if (!n) throw ParseFailure(std::format("MSH: element type {} is not supported in binary files", type));
            if (following <= 0) throw ParseFailure("MSH: invalid binary element block");
            nodes.resize(static_cast<std::size_t>(*n));
            for (int e = 0; e < following; ++e) {
              const auto tag = static_cast<std::uint64_t>(raw<std::int32_t>());
              std::vector<int> tags(static_cast<std::size_t>(std::max(tagCount, 0)));
              for (auto& t : tags) t = raw<std::int32_t>();
              for (auto& node : nodes) node = static_cast<std::uint64_t>(raw<std::int32_t>());
              addElement(type, tag, nodes, tags.size() > 1 ? tags[1] : 0, dimensionOf(type),
                         tags.empty() ? std::vector<int>{} : std::vector<int>{tags[0]});
            }
            read += static_cast<std::size_t>(following);
          }
          m_cursor.skipLineBreak();
        }
        m_context.progress(0.6f, "elements");
        expectEnd("Elements");
      }

      void readElements4() {
        const bool v40 = m_version < 4.1;
        const auto blocks = sizeValue();
        sizeValue(); // total
        if (!v40) {
          sizeValue(); // min tag
          sizeValue(); // max tag
        }
        std::vector<std::uint64_t> nodes;
        for (std::uint64_t b = 0; b < blocks; ++b) {
          int dim = 0;
          int entity = 0;
          if (v40) {
            entity = intValue();
            dim = intValue();
          } else {
            dim = intValue();
            entity = intValue();
          }
          const int type = intValue();
          const auto count = sizeValue();
          const auto n = gmshNodeCount(type);
          if (!n) {
            if (binary()) throw ParseFailure(std::format("MSH: element type {} is not supported in binary files", type));
            m_cursor.line();
            for (std::uint64_t e = 0; e < count; ++e) m_cursor.line();
            m_skippedTypes[type] += count;
            continue;
          }
          const auto physicalsIt = m_entityPhysicals.find({dim, entity});
          const std::vector<int> physicals = physicalsIt == m_entityPhysicals.end() ? std::vector<int>{} : physicalsIt->second;
          nodes.resize(static_cast<std::size_t>(*n));
          for (std::uint64_t e = 0; e < count; ++e) {
            const auto tag = sizeValue();
            for (auto& node : nodes) node = sizeValue();
            addElement(type, tag, nodes, entity, dim, physicals);
          }
          checkCancel(m_context);
        }
        if (binary()) m_cursor.skipLineBreak();
        m_context.progress(0.6f, "elements");
        expectEnd("Elements");
      }

      struct RawStep {
        double time{0.0};
        std::vector<std::pair<std::uint64_t, std::vector<double>>> entries;
      };
      struct RawField {
        std::string name;
        FieldLocation location{FieldLocation::Node};
        int components{1};
        std::map<int, RawStep> steps;
        bool inconsistent{false};
      };

      void readData(const FieldLocation location, const std::string& section) {
        std::vector<std::string> strings(m_cursor.number<std::size_t>());
        for (auto& s : strings) {
          m_cursor.skipSpace();
          s = quotedText(m_cursor.line());
        }
        std::vector<double> reals(m_cursor.number<std::size_t>());
        for (auto& r : reals) r = m_cursor.number<double>();
        std::vector<long long> integers(m_cursor.number<std::size_t>());
        for (auto& i : integers) i = m_cursor.number<long long>();
        if (integers.size() < 3) throw ParseFailure("MSH: $" + section + " needs at least 3 integer tags");
        const int step = static_cast<int>(integers[0]);
        const int components = static_cast<int>(integers[1]);
        const auto count = static_cast<std::size_t>(integers[2]);
        if (components < 1) throw ParseFailure("MSH: invalid component count in $" + section);

        const std::string name = strings.empty() ? std::string("unnamed") : strings[0];
        auto [it, inserted] = m_fields.try_emplace({name, static_cast<int>(location)});
        auto& field = it->second;
        if (inserted) {
          field.name = name;
          field.location = location;
          field.components = components;
        } else if (field.components != components) {
          field.inconsistent = true;
        }
        auto& target = field.steps[step];
        target.time = reals.empty() ? 0.0 : reals[0];
        if (binary()) m_cursor.skipLineBreak();
        target.entries.reserve(target.entries.size() + count);
        for (std::size_t i = 0; i < count; ++i) {
          const std::uint64_t tag = binary() ? static_cast<std::uint64_t>(raw<std::int32_t>()) : m_cursor.number<std::uint64_t>();
          std::vector<double> values(static_cast<std::size_t>(components));
          for (auto& v : values) v = doubleValue();
          target.entries.emplace_back(tag, std::move(values));
        }
        if (binary()) m_cursor.skipLineBreak();
        expectEnd(section);
      }

      struct StepInfo {
        StepKind kind{StepKind::Time};
        std::vector<std::string> labels;
      };

      std::string quotedLine(const bool escaped) {
        m_cursor.skipSpace();
        const auto line = m_cursor.line();
        return escaped ? unescapeQuoted(line) : quotedText(line);
      }

      void readAnafData() {
        const int version = m_cursor.number<int>();
        if (version != 1) {
          m_model.warnings.push_back(std::format("MSH: $AnafData version {} is not supported and was skipped", version));
          skipSection("AnafData");
          return;
        }
        const auto records = m_cursor.number<std::size_t>();
        for (std::size_t r = 0; r < records; ++r) {
          const auto kind = m_cursor.token();
          if (kind == "GLOBAL") {
            GlobalArray global;
            global.components = m_cursor.number<int>();
            const auto tuples = m_cursor.number<std::size_t>();
            if (global.components < 1) throw ParseFailure("MSH: invalid component count in $AnafData");
            global.name = quotedLine(true);
            global.values.resize(tuples * static_cast<std::size_t>(global.components));
            for (auto& v : global.values) v = m_cursor.number<double>();
            m_model.globalData.push_back(std::move(global));
          } else if (kind == "STEPS") {
            const bool onNodes = m_cursor.token() == "N";
            const std::string kindName(m_cursor.token());
            const auto steps = m_cursor.number<std::size_t>();
            const bool labelled = m_cursor.number<int>() != 0;
            StepInfo info;
            const auto stepKind = stepKindFromName(kindName);
            if (!stepKind) m_model.warnings.push_back("MSH: unknown step kind '" + kindName + "' read as Time");
            info.kind = stepKind.value_or(StepKind::Time);
            const std::string name = quotedLine(false);
            if (labelled) {
              info.labels.resize(steps);
              for (auto& label : info.labels) label = quotedLine(true);
            }
            m_stepInfo[{name, static_cast<int>(onNodes ? FieldLocation::Node : FieldLocation::Element)}] = std::move(info);
          } else {
            throw ParseFailure("MSH: unknown $AnafData record '" + std::string(kind) + "'");
          }
        }
        expectEnd("AnafData");
      }

      void applyAnafData() {
        for (auto& field : m_model.fields) {
          const auto it = m_stepInfo.find({field.name, static_cast<int>(field.location)});
          if (it == m_stepInfo.end()) continue;
          field.stepKind = it->second.kind;
          if (it->second.labels.size() == field.steps.size()) field.stepLabels = it->second.labels;
          else if (!it->second.labels.empty()) m_model.warnings.push_back(std::format("field '{}': step labels do not match its steps", field.name));
        }
      }

      // ---------------------------------------------------------- finishing

      std::vector<std::size_t> blockOffsets() const {
        std::vector<std::size_t> offsets(m_model.blocks.size(), 0);
        for (std::size_t b = 1; b < m_model.blocks.size(); ++b) offsets[b] = offsets[b - 1] + m_model.blocks[b - 1].size();
        return offsets;
      }

      void finishSets() {
        const auto offsets = blockOffsets();
        for (const auto& [key, members] : m_physicalMembers) {
          const auto [dim, physical] = key;
          EntitySet set;
          const auto name = m_physicalNames.find({dim, physical});
          set.name = name != m_physicalNames.end() ? name->second : std::format("Physical{}D_{}", dim, physical);
          set.tag = physical;
          set.dimension = dim;
          std::set<std::uint32_t> unique;
          if (dim == 0) {
            set.kind = SetKind::Node;
            for (const auto& location : members) {
              const auto& block = m_model.blocks[location.block];
              const int n = elementInfo(block.type).nodeCount;
              for (int k = 0; k < n; ++k) unique.insert(block.connectivity[location.local * static_cast<std::size_t>(n) + k]);
            }
          } else {
            set.kind = SetKind::Element;
            for (const auto& location : members) unique.insert(static_cast<std::uint32_t>(offsets[location.block] + location.local));
          }
          set.members.assign(unique.begin(), unique.end());
          // Groups sharing a name (one per dimension) form one set.
          const auto existing = std::ranges::find_if(m_model.sets, [&](const EntitySet& other) {
            return other.kind == set.kind && other.name == set.name;
          });
          if (existing != m_model.sets.end()) {
            std::set<std::uint32_t> merged(existing->members.begin(), existing->members.end());
            merged.insert(set.members.begin(), set.members.end());
            existing->members.assign(merged.begin(), merged.end());
            existing->dimension = std::max(existing->dimension, set.dimension);
            existing->tag = -1;
          } else {
            m_model.sets.push_back(std::move(set));
          }
        }
      }

      void finishFields() {
        const auto offsets = blockOffsets();
        std::unordered_map<std::uint64_t, std::uint32_t> elementIndex;
        elementIndex.reserve(m_elementByTag.size());
        for (const auto& [tag, location] : m_elementByTag) {
          elementIndex.emplace(tag, static_cast<std::uint32_t>(offsets[location.block] + location.local));
        }
        for (auto& [key, rawField] : m_fields) {
          if (rawField.inconsistent) {
            m_model.warnings.push_back(std::format("field '{}' changes its component count between steps and was skipped", rawField.name));
            continue;
          }
          const bool onNodes = rawField.location == FieldLocation::Node;
          const std::size_t entities = onNodes ? m_model.nodes.size() : m_model.elementCount();
          const auto components = static_cast<std::size_t>(rawField.components);
          Field field;
          field.name = rawField.name;
          field.location = rawField.location;
          field.components = rawField.components;
          std::size_t unmatched = 0;
          for (auto& [step, data] : rawField.steps) {
            std::vector<double> values(entities * components, 0.0);
            for (const auto& [tag, entry] : data.entries) {
              const auto& index = onNodes ? m_nodeIndex : elementIndex;
              const auto found = index.find(tag);
              if (found == index.end()) {
                ++unmatched;
                continue;
              }
              std::copy(entry.begin(), entry.end(), values.begin() + static_cast<std::ptrdiff_t>(found->second * components));
            }
            field.times.push_back(data.time);
            field.steps.push_back(std::move(values));
          }
          if (unmatched > 0) m_model.warnings.push_back(std::format("field '{}': {} entries refer to unknown tags", rawField.name, unmatched));
          m_model.fields.push_back(std::move(field));
        }
      }

      Cursor m_cursor;
      const IoContext& m_context;
      MeshModel m_model;
      double m_version{2.2};
      bool m_binary{false};
      bool m_bigEndian{false};
      bool m_sawNodes{false};
      std::map<IntPair, std::string> m_physicalNames;
      std::map<IntPair, std::vector<int>> m_entityPhysicals;
      std::unordered_map<std::uint64_t, std::uint32_t> m_nodeIndex;
      std::unordered_map<std::uint64_t, ElementLocation> m_elementByTag;
      std::map<IntPair, std::vector<ElementLocation>> m_physicalMembers;
      std::map<std::pair<std::string, int>, RawField> m_fields;
      std::map<std::pair<std::string, int>, StepInfo> m_stepInfo;
      std::map<int, std::size_t> m_skippedTypes;
    };

    // ================================================================ writer

    class MshWriter {
    public:
      MshWriter(const MeshModel& model, const WriteOptions& options, WriteReport& report)
        : m_model(model), m_binary(options.encoding == Encoding::Binary), m_v41(options.mshVersion == MshVersion::V4_1), m_report(report) {
        prepareTags();
        preparePartitions();
      }

      std::string write(const IoContext& context) {
        header();
        physicalNames();
        if (m_v41) {
          entities();
          nodes41();
          checkCancel(context);
          elements41();
        } else {
          nodes22();
          checkCancel(context);
          elements22();
        }
        context.progress(0.6f, "data");
        const auto encoded = detail::encodeModelData(m_model, detail::CodecOptions{true, false, false});
        for (const auto* group : {&m_model.fields, &encoded}) {
          for (const auto& field : *group) {
            data(field);
            checkCancel(context);
          }
        }
        anafData();
        return std::move(m_out);
      }

    private:
      struct Entity {
        int dim;
        int source;
        int tag;
        std::vector<int> sets;
        std::vector<int> physicals;
      };

      // ---------------------------------------------------------- output primitives

      template <typename T>
      void rawValue(const T value) { detail::appendValue(m_out, value, false); }

      template <typename Number>
      void number(const Number value) { detail::appendNumber(m_out, value); }

      void text(std::string_view s) { m_out.append(s); }

      // ---------------------------------------------------------- preparation

      void prepareTags() {
        constexpr auto int32Max = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
        m_nodeTags.resize(m_model.nodes.size());
        std::unordered_set<std::uint64_t> seen;
        bool usable = true;
        for (const auto& node : m_model.nodes) {
          if (node.tag == 0 || !seen.insert(node.tag).second || (!m_v41 && node.tag > int32Max)) usable = false;
        }
        for (std::size_t i = 0; i < m_model.nodes.size(); ++i) m_nodeTags[i] = usable ? m_model.nodes[i].tag : i + 1;
        if (!usable) m_report.warnings.push_back("node tags were not unique/positive (or too large for MSH 2.2); nodes renumbered 1..N");

        const std::size_t elementTotal = m_model.elementCount();
        m_elementTags.resize(elementTotal);
        std::unordered_set<std::uint64_t> seenElements;
        bool usableElements = true;
        std::size_t global = 0;
        for (const auto& block : m_model.blocks) {
          for (std::size_t e = 0; e < block.size(); ++e, ++global) {
            const auto tag = block.tags[e];
            m_elementTags[global] = tag;
            if (tag == 0 || !seenElements.insert(tag).second || (!m_v41 && tag > int32Max)) usableElements = false;
          }
        }
        if (!usableElements) {
          for (std::size_t i = 0; i < elementTotal; ++i) m_elementTags[i] = i + 1;
          m_report.warnings.push_back("element tags were not unique/positive (or too large for MSH 2.2); elements renumbered 1..N");
        }
      }

      // Groups elements into entities (same dimension, source entity, element type and set
      // membership) and assigns one physical tag per (set, dimension).
      void preparePartitions() {
        const std::size_t elementTotal = m_model.elementCount();
        std::vector<std::vector<int>> setsOfElement(elementTotal);
        for (std::size_t s = 0; s < m_model.sets.size(); ++s) {
          if (m_model.sets[s].kind != SetKind::Element) continue;
          for (const auto member : m_model.sets[s].members) setsOfElement[member].push_back(static_cast<int>(s));
        }
        m_elementEntity.resize(elementTotal);
        // Element type is part of the key: Gmsh drops elements when one entity mixes element
        // orders (e.g. Tri3 with Tri6), so every entity holds a single element type.
        std::map<std::tuple<int, int, int, std::vector<int>>, std::size_t> partitionIndex;
        std::map<IntPair, int> partitionsPerSource;
        std::size_t global = 0;
        for (const auto& block : m_model.blocks) {
          const int dim = elementInfo(block.type).dimension;
          for (std::size_t e = 0; e < block.size(); ++e, ++global) {
            const int source = block.entityTags.empty() ? 0 : block.entityTags[e];
            const auto [it, inserted] = partitionIndex.try_emplace(
              std::make_tuple(dim, source, static_cast<int>(block.type), setsOfElement[global]), m_entities.size());
            if (inserted) {
              m_entities.push_back(Entity{dim, source, 0, setsOfElement[global], {}});
              ++partitionsPerSource[{dim, source}];
            }
            m_elementEntity[global] = it->second;
          }
        }
        // Entity tags: keep the source tag when a source entity maps to exactly one partition.
        std::map<int, std::set<int>> used;
        for (auto& entity : m_entities) {
          if (entity.source > 0 && partitionsPerSource[{entity.dim, entity.source}] == 1) {
            entity.tag = entity.source;
            used[entity.dim].insert(entity.source);
          }
        }
        for (auto& entity : m_entities) {
          if (entity.tag != 0) continue;
          int next = 1;
          while (used[entity.dim].contains(next)) ++next;
          entity.tag = next;
          used[entity.dim].insert(next);
        }
        // Physical tags: keep the set's tag when it is free in that dimension.
        std::map<int, std::set<int>> usedPhysical;
        std::set<int> requested;
        for (const auto& set : m_model.sets) {
          if (set.kind == SetKind::Element && set.tag > 0) requested.insert(set.tag);
        }
        for (std::size_t s = 0; s < m_model.sets.size(); ++s) {
          const auto& set = m_model.sets[s];
          if (set.kind != SetKind::Element) continue;
          std::set<int> dims;
          for (const auto& entity : m_entities) {
            if (std::ranges::find(entity.sets, static_cast<int>(s)) != entity.sets.end()) dims.insert(entity.dim);
          }
          for (const int dim : dims) {
            int tag = set.tag;
            if (tag <= 0 || usedPhysical[dim].contains(tag)) {
              tag = 1;
              while (usedPhysical[dim].contains(tag) || requested.contains(tag)) ++tag;
            }
            usedPhysical[dim].insert(tag);
            m_physicalTag[{static_cast<int>(s), dim}] = tag;
          }
        }
        for (auto& entity : m_entities) {
          for (const int s : entity.sets) entity.physicals.push_back(m_physicalTag.at({s, entity.dim}));
        }
      }

      // ---------------------------------------------------------- sections

      void header() {
        text(std::format("$MeshFormat\n{} {} 8\n", m_v41 ? "4.1" : "2.2", m_binary ? 1 : 0));
        if (m_binary) {
          rawValue<std::int32_t>(1);
          text("\n");
        }
        text("$EndMeshFormat\n");
      }

      void physicalNames() {
        if (m_physicalTag.empty()) return;
        text(std::format("$PhysicalNames\n{}\n", m_physicalTag.size()));
        for (const auto& [key, tag] : m_physicalTag) {
          std::string name = m_model.sets[static_cast<std::size_t>(key.first)].name;
          std::ranges::replace(name, '"', '\'');
          text(std::format("{} {} \"{}\"\n", key.second, tag, name));
        }
        text("$EndPhysicalNames\n");
      }

      std::array<double, 6> boundingBox(const std::size_t entityIndex) const {
        constexpr double big = std::numeric_limits<double>::max();
        std::array<double, 6> box{big, big, big, -big, -big, -big};
        std::size_t global = 0;
        for (const auto& block : m_model.blocks) {
          const auto n = static_cast<std::size_t>(elementInfo(block.type).nodeCount);
          for (std::size_t e = 0; e < block.size(); ++e, ++global) {
            if (m_elementEntity[global] != entityIndex) continue;
            for (std::size_t k = 0; k < n; ++k) {
              const auto& p = m_model.nodes[block.connectivity[e * n + k]].position;
              for (std::size_t a = 0; a < 3; ++a) {
                box[a] = std::min(box[a], p[a]);
                box[a + 3] = std::max(box[a + 3], p[a]);
              }
            }
          }
        }
        return box;
      }

      // Nodes live in one block on the highest-dimensional entity; elements of any entity may use them.
      std::pair<int, int> hostEntity() const {
        if (m_entities.empty()) return {0, 1};
        const auto it = std::ranges::max_element(m_entities, {}, &Entity::dim);
        return {it->dim, it->tag};
      }

      void entities() {
        std::array<std::uint64_t, 4> counts{};
        for (const auto& entity : m_entities) ++counts[static_cast<std::size_t>(entity.dim)];
        if (m_entities.empty()) counts[0] = 1; // host point entity for a node-only model

        text("$Entities\n");
        if (m_binary) {
          for (const auto c : counts) rawValue<std::uint64_t>(c);
        } else {
          text(std::format("{} {} {} {}\n", counts[0], counts[1], counts[2], counts[3]));
        }
        if (m_entities.empty()) writeEntity(0, 1, {0, 0, 0, 0, 0, 0}, {});
        for (int dim = 0; dim < 4; ++dim) {
          for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].dim == dim) writeEntity(dim, m_entities[i].tag, boundingBox(i), m_entities[i].physicals);
          }
        }
        if (m_binary) text("\n");
        text("$EndEntities\n");
      }

      void writeEntity(const int dim, const int tag, const std::array<double, 6>& box, const std::vector<int>& physicals) {
        const std::size_t coordinates = dim == 0 ? 3 : 6;
        if (m_binary) {
          rawValue<std::int32_t>(tag);
          for (std::size_t k = 0; k < coordinates; ++k) rawValue<double>(box[k]);
          rawValue<std::uint64_t>(physicals.size());
          for (const int p : physicals) rawValue<std::int32_t>(p);
          if (dim > 0) rawValue<std::uint64_t>(0);
        } else {
          number(tag);
          for (std::size_t k = 0; k < coordinates; ++k) {
            text(" ");
            number(box[k]);
          }
          text(std::format(" {}", physicals.size()));
          for (const int p : physicals) text(std::format(" {}", p));
          if (dim > 0) text(" 0");
          text("\n");
        }
      }

      static std::pair<std::uint64_t, std::uint64_t> tagRange(const std::vector<std::uint64_t>& tags) {
        if (tags.empty()) return {0, 0};
        const auto [lo, hi] = std::ranges::minmax_element(tags);
        return {*lo, *hi};
      }

      void nodes41() {
        const auto [hostDim, hostTag] = hostEntity();
        const auto [lo, hi] = tagRange(m_nodeTags);
        const std::uint64_t blocks = m_model.nodes.empty() ? 0 : 1;
        text("$Nodes\n");
        if (m_binary) {
          for (const std::uint64_t v : {blocks, static_cast<std::uint64_t>(m_model.nodes.size()), lo, hi}) rawValue<std::uint64_t>(v);
          if (blocks) {
            rawValue<std::int32_t>(hostDim);
            rawValue<std::int32_t>(hostTag);
            rawValue<std::int32_t>(0);
            rawValue<std::uint64_t>(m_model.nodes.size());
            for (const auto tag : m_nodeTags) rawValue<std::uint64_t>(tag);
            for (const auto& node : m_model.nodes) {
              for (const double c : node.position) rawValue<double>(c);
            }
          }
          text("\n");
        } else {
          text(std::format("{} {} {} {}\n", blocks, m_model.nodes.size(), lo, hi));
          if (blocks) {
            text(std::format("{} {} 0 {}\n", hostDim, hostTag, m_model.nodes.size()));
            for (const auto tag : m_nodeTags) {
              number(tag);
              text("\n");
            }
            for (const auto& node : m_model.nodes) {
              number(node.position[0]);
              text(" ");
              number(node.position[1]);
              text(" ");
              number(node.position[2]);
              text("\n");
            }
          }
        }
        text("$EndNodes\n");
      }

      void elements41() {
        struct Member { std::size_t global; const ElementBlock* block; std::size_t local; };
        std::map<std::pair<std::size_t, int>, std::vector<Member>> blocks; // (entity, gmsh type)
        std::size_t global = 0;
        for (const auto& block : m_model.blocks) {
          const int gmshType = elementInfo(block.type).gmshType;
          for (std::size_t e = 0; e < block.size(); ++e, ++global) blocks[{m_elementEntity[global], gmshType}].push_back({global, &block, e});
        }
        const auto [lo, hi] = tagRange(m_elementTags);
        text("$Elements\n");
        if (m_binary) {
          for (const std::uint64_t v : {static_cast<std::uint64_t>(blocks.size()), static_cast<std::uint64_t>(m_model.elementCount()), lo, hi}) {
            rawValue<std::uint64_t>(v);
          }
        } else {
          text(std::format("{} {} {} {}\n", blocks.size(), m_model.elementCount(), lo, hi));
        }
        for (const auto& [key, members] : blocks) {
          const auto& entity = m_entities[key.first];
          const auto nodeCount = static_cast<std::size_t>(elementInfo(members.front().block->type).nodeCount);
          if (m_binary) {
            rawValue<std::int32_t>(entity.dim);
            rawValue<std::int32_t>(entity.tag);
            rawValue<std::int32_t>(key.second);
            rawValue<std::uint64_t>(members.size());
          } else {
            text(std::format("{} {} {} {}\n", entity.dim, entity.tag, key.second, members.size()));
          }
          for (const auto& member : members) {
            const std::uint32_t* nodes = &member.block->connectivity[member.local * nodeCount];
            if (m_binary) {
              rawValue<std::uint64_t>(m_elementTags[member.global]);
              for (std::size_t k = 0; k < nodeCount; ++k) rawValue<std::uint64_t>(m_nodeTags[nodes[k]]);
            } else {
              number(m_elementTags[member.global]);
              for (std::size_t k = 0; k < nodeCount; ++k) {
                text(" ");
                number(m_nodeTags[nodes[k]]);
              }
              text("\n");
            }
          }
        }
        if (m_binary) text("\n");
        text("$EndElements\n");
      }

      void nodes22() {
        text(std::format("$Nodes\n{}\n", m_model.nodes.size()));
        for (std::size_t i = 0; i < m_model.nodes.size(); ++i) {
          const auto& p = m_model.nodes[i].position;
          if (m_binary) {
            rawValue<std::int32_t>(static_cast<std::int32_t>(m_nodeTags[i]));
            for (const double c : p) rawValue<double>(c);
          } else {
            number(m_nodeTags[i]);
            for (const double c : p) {
              text(" ");
              number(c);
            }
            text("\n");
          }
        }
        if (m_binary) text("\n");
        text("$EndNodes\n");
      }

      void elements22() {
        // An element in several sets is listed once per physical group with the same tag
        // (Gmsh's own convention); readers merge the duplicates.
        struct Line { std::size_t global; const ElementBlock* block; std::size_t local; int physical; int entity; };
        std::vector<Line> lines;
        std::size_t global = 0;
        for (const auto& block : m_model.blocks) {
          for (std::size_t e = 0; e < block.size(); ++e, ++global) {
            const auto& entity = m_entities[m_elementEntity[global]];
            if (entity.physicals.empty()) {
              lines.push_back({global, &block, e, 0, entity.tag});
            } else {
              for (const int physical : entity.physicals) lines.push_back({global, &block, e, physical, entity.tag});
            }
          }
        }
        text(std::format("$Elements\n{}\n", lines.size()));
        std::size_t i = 0;
        while (i < lines.size()) {
          std::size_t j = i;
          while (j < lines.size() && lines[j].block->type == lines[i].block->type) ++j; // binary blocks share one type
          const auto& info = elementInfo(lines[i].block->type);
          const auto nodeCount = static_cast<std::size_t>(info.nodeCount);
          if (m_binary) {
            rawValue<std::int32_t>(info.gmshType);
            rawValue<std::int32_t>(static_cast<std::int32_t>(j - i));
            rawValue<std::int32_t>(2);
          }
          for (std::size_t k = i; k < j; ++k) {
            const auto& line = lines[k];
            const std::uint32_t* nodes = &line.block->connectivity[line.local * nodeCount];
            if (m_binary) {
              rawValue<std::int32_t>(static_cast<std::int32_t>(m_elementTags[line.global]));
              rawValue<std::int32_t>(line.physical);
              rawValue<std::int32_t>(line.entity);
              for (std::size_t n = 0; n < nodeCount; ++n) rawValue<std::int32_t>(static_cast<std::int32_t>(m_nodeTags[nodes[n]]));
            } else {
              text(std::format("{} {} 2 {} {}", m_elementTags[line.global], info.gmshType, line.physical, line.entity));
              for (std::size_t n = 0; n < nodeCount; ++n) {
                text(" ");
                number(m_nodeTags[nodes[n]]);
              }
              text("\n");
            }
          }
          i = j;
        }
        if (m_binary) text("\n");
        text("$EndElements\n");
      }

      void data(const Field& field) {
        const bool onNodes = field.location == FieldLocation::Node;
        const auto& tags = onNodes ? m_nodeTags : m_elementTags;
        const char* section = onNodes ? "NodeData" : "ElementData";
        const auto components = static_cast<std::size_t>(field.components);
        std::string name = field.name;
        std::ranges::replace(name, '"', '\'');
        for (std::size_t step = 0; step < field.steps.size(); ++step) {
          const auto& values = field.steps[step];
          text(std::format("${}\n1\n\"{}\"\n1\n", section, name));
          number(step < field.times.size() ? field.times[step] : 0.0);
          text(std::format("\n3\n{}\n{}\n{}\n", step, field.components, tags.size()));
          for (std::size_t e = 0; e < tags.size(); ++e) {
            if (m_binary) {
              rawValue<std::int32_t>(static_cast<std::int32_t>(tags[e]));
              for (std::size_t c = 0; c < components; ++c) rawValue<double>(values[e * components + c]);
            } else {
              number(tags[e]);
              for (std::size_t c = 0; c < components; ++c) {
                text(" ");
                number(values[e * components + c]);
              }
              text("\n");
            }
          }
          if (m_binary) text("\n");
          text(std::format("$End{}\n", section));
        }
      }

      static std::string quoted(std::string text) {
        std::ranges::replace(text, '"', '\'');
        std::ranges::replace(text, '\n', ' ');
        return "\"" + text + "\"\n";
      }

      void anafData() {
        std::size_t records = m_model.globalData.size();
        for (const auto& field : m_model.fields) {
          if (field.stepKind != StepKind::Time || !field.stepLabels.empty()) ++records;
        }
        if (records == 0) return;
        text(std::format("$AnafData\n1\n{}\n", records));
        for (const auto& global : m_model.globalData) {
          text(std::format("GLOBAL {} {}\n", global.components, global.tuples()));
          text(escapedQuoted(global.name));
          const auto components = static_cast<std::size_t>(global.components);
          for (std::size_t i = 0; i < global.values.size(); ++i) {
            number(global.values[i]);
            text((i + 1) % components == 0 ? "\n" : " ");
          }
        }
        for (const auto& field : m_model.fields) {
          if (field.stepKind == StepKind::Time && field.stepLabels.empty()) continue;
          text(std::format("STEPS {} {} {} {}\n", field.location == FieldLocation::Node ? "N" : "E", stepKindName(field.stepKind),
            field.steps.size(), field.stepLabels.empty() ? 0 : 1));
          text(quoted(field.name));
          for (const auto& label : field.stepLabels) text(escapedQuoted(label));
        }
        text("$EndAnafData\n");
      }

      const MeshModel& m_model;
      bool m_binary;
      bool m_v41;
      WriteReport& m_report;
      std::string m_out;
      std::vector<std::uint64_t> m_nodeTags;
      std::vector<std::uint64_t> m_elementTags;
      std::vector<Entity> m_entities;
      std::vector<std::size_t> m_elementEntity;
      std::map<IntPair, int> m_physicalTag; // (set index, dim) -> physical tag
    };

  } // namespace end

  MeshModel readMsh(const std::filesystem::path& path, const ReadOptions&, const IoContext& context) {
    const std::string content = detail::readWholeFile(path);
    context.progress(0.05f, "parsing");
    MshReader reader(content, context);
    MeshModel model = reader.read();
    context.progress(1.0f, "done");
    return model;
  }

  WriteReport writeMsh(const std::filesystem::path& path, const MeshModel& model, const WriteOptions& options, const IoContext& context) {
    if (const auto problems = model.validate(); !problems.empty()) {
      throw std::invalid_argument("model is inconsistent: " + problems.front());
    }
    WriteReport report;
    report.path = path.string();
    MshWriter writer(model, options, report);
    const std::string content = writer.write(context);
    checkCancel(context);
    detail::writeWholeFile(path, content);
    context.progress(1.0f, "done");
    return report;
  }

} // namespace anaf::IO::formats end
