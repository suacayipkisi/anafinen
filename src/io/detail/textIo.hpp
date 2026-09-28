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

#pragma once

// Internal helpers shared by the text/binary format implementations. Not part of the public IO API.

#include "../core/pathUtf8.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace anaf::IO::detail {

  // Thrown by format implementations; converted to IoError at the API boundary.
  struct ParseFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  struct CancelledFailure : std::runtime_error {
    CancelledFailure() : std::runtime_error("operation cancelled") {}
  };

  // Shortest representation that parses back to the identical double (round-trip exact).
  inline void appendNumber(std::string& out, const double value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
  }

  template <typename Integer>
    requires std::is_integral_v<Integer>
  inline void appendNumber(std::string& out, const Integer value) {
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
  }

  inline std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw ParseFailure("cannot open '" + pathToUtf8(path) + "'");
    std::string content;
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size > 0) {
      content.resize(static_cast<std::size_t>(size));
      file.seekg(0, std::ios::beg);
      file.read(content.data(), size);
    }
    return content;
  }

  inline void writeWholeFile(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot open '" + pathToUtf8(path) + "' for writing");
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file) throw std::runtime_error("failed while writing '" + pathToUtf8(path) + "'");
  }

  inline bool isSpace(const char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  }

  inline std::string toLower(std::string_view text) {
    std::string lower(text);
    for (char& c : lower) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return lower;
  }

  // Forward-only cursor over a file held in memory. Handles mixed text/binary content
  // (legacy VTK binary, appended VTU data) by exposing both token and raw byte access.
  class Cursor {
  public:
    explicit Cursor(std::string_view data) : m_data(data) {}

    bool atEnd() const noexcept { return m_pos >= m_data.size(); }
    std::size_t position() const noexcept { return m_pos; }
    void seek(const std::size_t pos) noexcept { m_pos = pos; }
    std::string_view data() const noexcept { return m_data; }

    void skipSpace() noexcept {
      while (m_pos < m_data.size() && isSpace(m_data[m_pos])) ++m_pos;
    }

    // Next whitespace-delimited token; empty at end of input.
    std::string_view token() noexcept {
      skipSpace();
      const std::size_t begin = m_pos;
      while (m_pos < m_data.size() && !isSpace(m_data[m_pos])) ++m_pos;
      return m_data.substr(begin, m_pos - begin);
    }

    std::string_view peekToken() noexcept {
      const std::size_t saved = m_pos;
      const auto next = token();
      m_pos = saved;
      return next;
    }

    // Rest of the current line without the line break; consumes the break.
    std::string_view line() noexcept {
      const std::size_t begin = m_pos;
      while (m_pos < m_data.size() && m_data[m_pos] != '\n') ++m_pos;
      std::size_t end = m_pos;
      if (end > begin && m_data[end - 1] == '\r') --end;
      if (m_pos < m_data.size()) ++m_pos;
      return m_data.substr(begin, end - begin);
    }

    // Consumes exactly one line break (after a keyword line, before binary data).
    void skipLineBreak() noexcept {
      while (m_pos < m_data.size() && (m_data[m_pos] == ' ' || m_data[m_pos] == '\t')) ++m_pos;
      if (m_pos < m_data.size() && m_data[m_pos] == '\r') ++m_pos;
      if (m_pos < m_data.size() && m_data[m_pos] == '\n') ++m_pos;
    }

    std::string_view bytes(const std::size_t count) {
      if (m_pos + count > m_data.size()) throw ParseFailure("unexpected end of file in binary data");
      const auto view = m_data.substr(m_pos, count);
      m_pos += count;
      return view;
    }

    template <typename Number>
    Number number() {
      const auto text = token();
      if (text.empty()) throw ParseFailure("unexpected end of file, number expected");
      return parseNumber<Number>(text);
    }

    template <typename Number>
    static Number parseNumber(std::string_view text) {
      Number value{};
      if constexpr (std::is_floating_point_v<Number>) {
        // Legacy writers may emit "nan", "inf", "1.#INF" or Fortran-style "1.0D+03".
        std::string normalized;
        if (text.find_first_of("dD") != std::string_view::npos) {
          normalized = std::string(text);
          for (char& c : normalized) if (c == 'd' || c == 'D') c = 'e';
          text = normalized;
        }
        if (!text.empty() && text.front() == '+') text.remove_prefix(1);
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{}) throw ParseFailure("invalid number '" + std::string(text) + "'");
      } else {
        if (!text.empty() && text.front() == '+') text.remove_prefix(1);
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{}) {
          // Integers written as floating point ("3.0") are accepted.
          double asDouble{};
          const auto fallback = std::from_chars(text.data(), text.data() + text.size(), asDouble);
          if (fallback.ec != std::errc{}) throw ParseFailure("invalid integer '" + std::string(text) + "'");
          value = static_cast<Number>(asDouble);
        }
      }
      return value;
    }

  private:
    std::string_view m_data;
    std::size_t m_pos{0};
  };

  // Byte order helpers for binary formats.
  template <typename T>
  inline T byteSwap(T value) noexcept {
    if constexpr (sizeof(T) == 1) {
      return value;
    } else {
      auto bytes = std::bit_cast<std::array<unsigned char, sizeof(T)>>(value);
      for (std::size_t i = 0; i < sizeof(T) / 2; ++i) std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
      return std::bit_cast<T>(bytes);
    }
  }

  template <typename T>
  inline T loadValue(const char* source, const bool bigEndian) noexcept {
    T value;
    std::memcpy(&value, source, sizeof(T));
    if (bigEndian != (std::endian::native == std::endian::big)) value = byteSwap(value);
    return value;
  }

  template <typename T>
  inline void appendValue(std::string& out, T value, const bool bigEndian) {
    if (bigEndian != (std::endian::native == std::endian::big)) value = byteSwap(value);
    char raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.append(raw, sizeof(T));
  }

  // RFC 4648 base64 (used by VTU inline binary / appended base64 data).
  std::string base64Encode(std::string_view bytes);
  // Decodes `text`, ignoring whitespace. Stops at the end or at the first padding group.
  std::string base64Decode(std::string_view text);

} // namespace anaf::IO::detail end
