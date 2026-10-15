// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <bit>
#include <cstdint>

namespace GAGCore
{
// Ordered, non-cryptographic fingerprint of explicit scalar words. No object
// padding, pointers, native byte order, stream dispatch or ignored field names.
// This deliberately differs from the canonical-byte diagnostic checksum.
// MurmurHash3 x86_32, seed zero, over explicit little-endian words. Mixing
// rotates bits between positions; XOR/multiply alone lets paired high-bit
// changes cancel. Finalization includes the byte length and does not mutate
// the accumulator, so callers can inspect a prefix and continue appending.
class WordChecksum
{
    std::uint32_t state = 0;
    std::uint32_t byteLength = 0;
public:
    constexpr void addUint32(std::uint32_t word) {
        word *= 0xcc9e2d51u;
        word = std::rotl(word, 15);
        word *= 0x1b873593u;
        state = std::rotl(state ^ word, 13) * 5u + 0xe6546b64u;
        byteLength += 4;
    }
    constexpr void addUint64(std::uint64_t word) {
        addUint32(std::uint32_t(word));
        addUint32(std::uint32_t(word >> 32));
    }
    constexpr std::uint32_t value() const {
        auto hash = state ^ byteLength;
        hash ^= hash >> 16;
        hash *= 0x85ebca6bu;
        hash ^= hash >> 13;
        hash *= 0xc2b2ae35u;
        return hash ^ (hash >> 16);
    }
};
}
