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

#pragma once

#include <atomic>
#include <cstdint>
namespace anafGen{

[[nodiscard]] constexpr std::uint32_t permute_u32(std::uint32_t index) noexcept {
    index ^= index >> 16;
    index *= 0x85ebca6b;
    index ^= index >> 13;
    index *= 0xc2b2ae35;
    index ^= index >> 16;
    return index;
}

class IdGenerator {
private:
    std::atomic<std::uint32_t> m_counter_u32{0};
    std::atomic<std::uint8_t>  m_counter_u8{0};

public:
    constexpr IdGenerator() noexcept = default;
    explicit IdGenerator(std::uint32_t seed_u32, std::uint8_t seed_u8 = 0) noexcept 
        : m_counter_u32(seed_u32), m_counter_u8(seed_u8) {}

    [[nodiscard]] std::uint32_t next_u32() noexcept {
        const std::uint32_t idx = m_counter_u32.fetch_add(1, std::memory_order_relaxed);
        return permute_u32(idx);
    }

    void reset() noexcept {
        m_counter_u32.store(0, std::memory_order_relaxed);
    }
};

} //namespace anafGen
