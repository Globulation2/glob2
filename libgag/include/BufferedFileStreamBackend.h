// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <StreamBackend.h>
#include <cstring>

namespace GAGCore
{
// Batch field-sized writes before entering stdio. File positions include pending
// bytes; seeks, reads, explicit flushes and destruction drain them in order.
class BufferedFileStreamBackend : public FileStreamBackend
{
    char buffer[16384];
    size_t used = 0;

protected:
    virtual void writeBufferedData(const void* data, size_t size)
    { FileStreamBackend::write(data, size); }
    size_t bufferedSize() const { return used; }

    void drain()
    {
        if (used)
        {
            const size_t size = used;
            used = 0;
            writeBufferedData(buffer, size);
        }
    }
public:
    explicit BufferedFileStreamBackend(FILE* fp) : FileStreamBackend(fp) {}
    // Destruction drains through the unchecked base implementation; callers
    // requiring error reporting must explicitly flush or close first.
    ~BufferedFileStreamBackend() override { drain(); }
    BufferedFileStreamBackend(const BufferedFileStreamBackend&) = delete;
    BufferedFileStreamBackend& operator=(const BufferedFileStreamBackend&) = delete;

    void write(const void* data, size_t size) override
    {
        const char* bytes = static_cast<const char*>(data);
        if (size > sizeof(buffer) - used)
        {
            drain();
            if (size >= sizeof(buffer))
            {
                writeBufferedData(data, size);
                return;
            }
        }
        if (size) std::memcpy(buffer + used, bytes, size);
        used += size;
    }
    void putc(int c) override
    {
        const unsigned char byte = static_cast<unsigned char>(c);
        write(&byte, 1);
    }
    void flush() override { drain(); FileStreamBackend::flush(); }
    void read(void* data, size_t size) override { flush(); FileStreamBackend::read(data, size); }
    bool readExact(void* data, size_t size) override { flush(); return FileStreamBackend::readExact(data, size); }
    int getChar() override { flush(); return FileStreamBackend::getChar(); }
    void seekFromStart(int displacement) override { drain(); FileStreamBackend::seekFromStart(displacement); }
    void seekFromEnd(int displacement) override { drain(); FileStreamBackend::seekFromEnd(displacement); }
    void seekRelative(int displacement) override { drain(); FileStreamBackend::seekRelative(displacement); }
    size_t getPosition() override { return FileStreamBackend::getPosition() + used; }
};
}
