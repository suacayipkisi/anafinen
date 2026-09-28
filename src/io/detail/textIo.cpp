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

#include "textIo.hpp"

#include <array>

namespace anaf::IO::detail {

  namespace {
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    constexpr std::array<int, 256> makeDecodeTable() {
      std::array<int, 256> table{};
      for (auto& entry : table) entry = -1;
      for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kAlphabet[i])] = i;
      return table;
    }
    constexpr auto kDecode = makeDecodeTable();

    // Decodes one group of up to 4 sextets (padding already stripped) into `out`.
    void flushGroup(const std::array<int, 4>& group, const int filled, std::string& out) {
      if (filled < 2) {
        if (filled == 1) throw ParseFailure("truncated base64 data");
        return;
      }
      const unsigned value = (static_cast<unsigned>(group[0]) << 18) | (static_cast<unsigned>(group[1]) << 12)
        | (static_cast<unsigned>(filled > 2 ? group[2] : 0) << 6) | static_cast<unsigned>(filled > 3 ? group[3] : 0);
      out.push_back(static_cast<char>((value >> 16) & 0xFF));
      if (filled > 2) out.push_back(static_cast<char>((value >> 8) & 0xFF));
      if (filled > 3) out.push_back(static_cast<char>(value & 0xFF));
    }
  } // namespace end

  std::string base64Encode(const std::string_view bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
      const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8)
        | static_cast<unsigned char>(bytes[i + 2]);
      out.push_back(kAlphabet[(value >> 18) & 63]);
      out.push_back(kAlphabet[(value >> 12) & 63]);
      out.push_back(kAlphabet[(value >> 6) & 63]);
      out.push_back(kAlphabet[value & 63]);
    }
    const std::size_t rest = bytes.size() - i;
    if (rest > 0) {
      unsigned value = static_cast<unsigned char>(bytes[i]) << 16;
      if (rest == 2) value |= static_cast<unsigned char>(bytes[i + 1]) << 8;
      out.push_back(kAlphabet[(value >> 18) & 63]);
      out.push_back(kAlphabet[(value >> 12) & 63]);
      out.push_back(rest == 2 ? kAlphabet[(value >> 6) & 63] : '=');
      out.push_back('=');
    }
    return out;
  }

  std::string base64Decode(const std::string_view text) {
    std::string out;
    out.reserve(text.size() / 4 * 3);
    std::array<int, 4> group{};
    int filled = 0;
    for (const char c : text) {
      if (isSpace(c)) continue;
      if (c == '=') {
        // Padding closes the current group. VTK writes some blocks back to back,
        // so decoding continues with the next group after the padding.
        flushGroup(group, filled, out);
        filled = 0;
        continue;
      }
      const int sextet = kDecode[static_cast<unsigned char>(c)];
      if (sextet < 0) throw ParseFailure("invalid character in base64 data");
      group[filled++] = sextet;
      if (filled == 4) {
        flushGroup(group, 4, out);
        filled = 0;
      }
    }
    flushGroup(group, filled, out);
    return out;
  }

} // namespace anaf::IO::detail end
