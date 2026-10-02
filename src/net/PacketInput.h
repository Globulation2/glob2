// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <StreamBackend.h>
#include <stdexcept>

// Network payloads must fail on overread; saved-file backends retain their
// historical behavior and serialization format.
class PacketInput final : public GAGCore::MemoryStreamBackend
{
    const size_t length;
public:
    PacketInput(const void *bytes, size_t size)
        : MemoryStreamBackend(bytes, size), length(size) { seekFromStart(0); }
    void read(void *bytes, size_t size) override
    {
        const size_t position = getPosition();
        if (position > length || size > length - position)
            throw std::runtime_error("Truncated network message");
        MemoryStreamBackend::read(bytes, size);
    }
};
