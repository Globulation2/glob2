// SPDX-License-Identifier: GPL-3.0-or-later
#include <BufferedFileStreamBackend.h>
#include <BinaryStream.h>
#include "../gnupg/sha1.c"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <io.h>
#define dup _dup
#define fileno _fileno
#define fdopen _fdopen
#else
#include <unistd.h>
#endif

static void require(bool ok)
{
    if (!ok) { std::fprintf(stderr, "Buffered file stream regression\n"); std::exit(1); }
}

static std::vector<unsigned char> serialize(bool buffered, unsigned char digest[20])
{
    FILE* file = std::tmpfile();
    require(file != nullptr);
    auto* backend = buffered ? static_cast<GAGCore::StreamBackend*>(new GAGCore::BufferedFileStreamBackend(file))
                             : new GAGCore::FileStreamBackend(file);
    GAGCore::BinaryOutputStream writer(backend);
    writer.enableSHA1();
    std::vector<unsigned char> large(65537);
    for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<unsigned char>(i * 37);
    for (int round = 0; round < 3; ++round)
    {
        for (unsigned i = 0; i < 20001; ++i)
        {
            writer.writeUint8(i, "byte");
            writer.writeSint32(-static_cast<int>(i), "signed");
            writer.writeUint32(i * 331, "unsigned");
            if ((i % 113) == 0) writer.writeText("wrapped boundaries", "text");
        }
        for (size_t count : {size_t(0),size_t(16383),size_t(16384),size_t(16385),large.size()})
            writer.write(large.data(), count, "block");
    }
    writer.finishSHA1(digest);
    const size_t end = writer.getPosition();
    writer.seekFromStart(0);
    writer.writeUint32(static_cast<Uint32>(end), "backpatched length");
    writer.seekFromEnd(-3);
    writer.writeUint8(91, "tail");
    writer.seekRelative(-2);
    backend->putc(92);
    writer.seekFromEnd(0);
    require(writer.getPosition() == end);
    writer.flush();
    writer.seekFromStart(0);
    std::vector<unsigned char> bytes(end);
    backend->read(bytes.data(), bytes.size());
    require(backend->getPosition() == end);
    writer.seekFromStart(0);
    unsigned char prefix[4];
    require(backend->readExact(prefix, sizeof(prefix)));
    require(std::memcmp(prefix, bytes.data(), sizeof(prefix)) == 0);
    writer.seekFromEnd(-1);
    require(!backend->readExact(prefix, sizeof(prefix)));
    writer.seekFromStart(3);
    require(backend->getChar() == bytes[3]);
    return bytes;
}

int main()
{
    unsigned char a[20], b[20];
    require(serialize(false,a) == serialize(true,b));
    require(std::memcmp(a,b,20) == 0);
    // The duplicated descriptor survives the backend's fclose.
    FILE* file = std::tmpfile();
    require(file != nullptr);
    FILE* verifier = fdopen(dup(fileno(file)), "rb");
    require(verifier != nullptr);
    { GAGCore::BufferedFileStreamBackend backend(file); backend.write("final buffered bytes",20); }
    std::rewind(verifier);
    char bytes[20];
    require(std::fread(bytes, 1, sizeof(bytes), verifier) == sizeof(bytes));
    require(std::memcmp(bytes,"final buffered bytes",20) == 0);
    std::fclose(verifier);
    std::puts("Buffered file stream: bytes, SHA1, boundaries, seeks, reads, flush and destruction passed");
}
