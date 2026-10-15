// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore
{
// Ordered, non-cryptographic fingerprint of explicit scalar words. No object
// padding, pointers, native byte order, stream dispatch or ignored field names.
// This deliberately differs from the canonical-byte diagnostic checksum.
class WordChecksum
{
public:
    std::uint32_t value = 2166136261u;
    constexpr void addUint32(std::uint32_t word) { value = (value ^ word) * 16777619u; }
    constexpr void addUint64(std::uint64_t word) {
        addUint32(std::uint32_t(word));
        addUint32(std::uint32_t(word >> 32));
    }
};
}
