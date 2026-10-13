// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BinaryStream.h"
#include "GameHeader.h"
#include "StreamBackend.h"
#include <algorithm>
#include <string>

namespace glob2test
{
// Synthetic historical fixtures start from a current header. Remove the entire
// format-153 catalog and format-155 statistics delay before transforming older
// fields (formats152 and earlier). The catalog framing
// is the published chunk count followed by length-prefixed 256 KiB chunks.
inline void removeModernHeaderFields(std::string& bytes, const GameHeader& header,
                                        size_t followingBytes = 0)
{
    auto* memory = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    header.getExperiments().save(&output);
    output.flush();
    const size_t experimentBytes = memory->getPosition();
    const auto snapshot = header.getUnitCatalogSnapshot();
    constexpr size_t chunkBytes = 256 * 1024;
    const size_t catalogBytes = sizeof(Uint32) + snapshot.size() +
        sizeof(Uint32) * ((snapshot.size() + chunkBytes - 1) / chunkBytes);
    bytes.erase(bytes.size() - followingBytes - experimentBytes - catalogBytes, catalogBytes);
    // int32 latency, uint8 order rate, uint8 AI delay, then statistics delay.
    bytes.erase(sizeof(Sint32) + 2 * sizeof(Uint8), sizeof(Uint8));
}
}
