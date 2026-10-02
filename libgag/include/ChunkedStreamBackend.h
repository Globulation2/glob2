// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <StreamBackend.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace GAGCore
{
// A move-only snapshot. Growing the block table never copies snapshot bytes.
class ChunkedBuffer
{
public:
    static constexpr size_t blockSize = 1024 * 1024;
    ChunkedBuffer() = default;
    ChunkedBuffer(const ChunkedBuffer&) = delete;
    ChunkedBuffer& operator=(const ChunkedBuffer&) = delete;
    ChunkedBuffer(ChunkedBuffer&& other) noexcept
        : blocks(std::move(other.blocks)), length(std::exchange(other.length, 0)) {}
    ChunkedBuffer& operator=(ChunkedBuffer&& other) noexcept
    {
        if (this != &other)
        {
            blocks = std::move(other.blocks);
            length = std::exchange(other.length, 0);
        }
        return *this;
    }
    size_t size() const { return length; }
    size_t allocatedCapacity() const { return blocks.size() * blockSize; }
    size_t tableCapacity() const { return blocks.capacity() * sizeof(blocks[0]); }

    template<class Visitor> void forEachRange(size_t offset, size_t count, Visitor&& visit) const
    {
        if (offset > length || count > length - offset)
            throw std::out_of_range("Snapshot range exceeds contents");
        while (count)
        {
            const size_t n = std::min(count, blockSize - offset % blockSize);
            visit(blocks[offset / blockSize].get() + offset % blockSize, n);
            offset += n;
            count -= n;
        }
    }
    void readAt(size_t offset, void* output, size_t count) const
    {
        auto* bytes = static_cast<unsigned char*>(output);
        forEachRange(offset, count, [&bytes](const unsigned char* data, size_t n) {
            std::memcpy(bytes, data, n); bytes += n;
        });
    }
    void writeAt(size_t offset, const void* input, size_t count)
    {
        if (!count && offset <= length) return;
        if (count > std::numeric_limits<size_t>::max() - offset)
            throw std::length_error("Snapshot size overflow");
        const size_t end = offset + count;
        ensureCapacity(end);
        // Match MemoryStreamBackend's zero-filled gap on a seek past the end.
        if (offset > length)
        {
            size_t position = length;
            while (position < offset)
            {
                const size_t n = std::min(offset - position, blockSize - position % blockSize);
                std::memset(blocks[position / blockSize].get() + position % blockSize, 0, n);
                position += n;
            }
        }
        const auto* bytes = static_cast<const unsigned char*>(input);
        while (count)
        {
            const size_t n = std::min(count, blockSize - offset % blockSize);
            std::memcpy(blocks[offset / blockSize].get() + offset % blockSize, bytes, n);
            bytes += n; offset += n; count -= n;
        }
        length = std::max(length, end);
    }
    // Inflate directly into the unused part of the last block. Only committed
    // bytes become readable, including when zlib rejects a corrupt stream.
    std::pair<unsigned char*, size_t> prepareAppend()
    {
        if (length == std::numeric_limits<size_t>::max())
            throw std::length_error("Snapshot size overflow");
        ensureCapacity(length + 1);
        return {blocks[length / blockSize].get() + length % blockSize, blockSize - length % blockSize};
    }
    void commitAppend(size_t count)
    {
        if (count > blockSize - length % blockSize || count > allocatedCapacity() - length)
            throw std::out_of_range("Snapshot append exceeds allocation");
        length += count;
    }
private:
    void ensureCapacity(size_t end)
    {
        const size_t needed = end / blockSize + (end % blockSize != 0);
        while (blocks.size() < needed)
            blocks.emplace_back(new unsigned char[blockSize]); // deliberately uninitialized
    }
    std::vector<std::unique_ptr<unsigned char[]>> blocks;
    size_t length = 0;
};

class ChunkedStreamBackend final : public StreamBackend
{
public:
    ChunkedStreamBackend() = default;
    explicit ChunkedStreamBackend(ChunkedBuffer&& contents) : buffer(std::move(contents)) {}
    void write(const void* data, size_t size) override { buffer.writeAt(index, data, size); index += size; }
    void flush() override {}
    void read(void* data, size_t size) override
    {
        if (!readExact(data, size)) std::memset(data, 0, size);
    }
    bool readExact(void* data, size_t size) override
    {
        if (!size) return true;
        if (index > buffer.size() || size > buffer.size() - index) return false;
        buffer.readAt(index, data, size); index += size; return true;
    }
    void putc(int c) override { const unsigned char byte = c; write(&byte, 1); }
    int getChar() override { unsigned char byte = 0; read(&byte, 1); return byte; }
    void seekFromStart(int displacement) override
    { index = std::min(static_cast<size_t>(displacement), buffer.size()); }
    void seekFromEnd(int displacement) override
    { index = static_cast<size_t>(std::max<int64_t>(0, static_cast<int64_t>(buffer.size()) - displacement)); }
    void seekRelative(int displacement) override
    { index = static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(index) + displacement, 0, buffer.size())); }
    size_t getPosition() override { return index; }
    bool isEndOfStream() override { return index >= buffer.size(); }
    bool isValid() override { return true; }
    const ChunkedBuffer& contents() const { return buffer; }
    ChunkedBuffer takeContents() { index = 0; return std::move(buffer); }
private:
    ChunkedBuffer buffer;
    size_t index = 0;
};
}
