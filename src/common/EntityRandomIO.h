// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "EntityRandom.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <charconv>

// Stream has uint32 fields: encode each uint64 as high then low words.
inline void saveEntityRandom(GAGCore::OutputStream *stream, const EntityRandom& random)
{
    const auto state = random.exportState();
    stream->writeEnterSection("entityRandom");
    stream->writeUint32(std::uint32_t(state.value >> 32), "stateHigh");
    stream->writeUint32(std::uint32_t(state.value), "stateLow");
    stream->writeUint32(std::uint32_t(state.increment >> 32), "incrementHigh");
    stream->writeUint32(std::uint32_t(state.increment), "incrementLow");
    stream->writeLeaveSection();
}

inline void loadEntityRandom(GAGCore::InputStream *stream, EntityRandom& random)
{
    GAGCore::BinaryInputStream::CheckedReads checked(stream);
    stream->readEnterSection("entityRandom");
    auto readWord = [&](const char* name) -> std::uint32_t {
        if (auto* text = dynamic_cast<GAGCore::TextInputStream*>(stream)) {
            if (!text->hasField(name)) throw std::runtime_error("Missing entity RNG field");
            const auto value = text->readText(name);
            std::uint32_t word = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), word);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
                throw std::runtime_error("Invalid entity RNG field");
            return word;
        }
        return stream->readUint32(name);
    };
    const auto stateHigh = readWord("stateHigh");
    const auto stateLow = readWord("stateLow");
    const auto incrementHigh = readWord("incrementHigh");
    const auto incrementLow = readWord("incrementLow");
    stream->readLeaveSection();
    random.importState({(std::uint64_t(stateHigh) << 32) | stateLow,
        (std::uint64_t(incrementHigh) << 32) | incrementLow});
}
