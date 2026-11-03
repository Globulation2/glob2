// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <TextStream.h>
#include <StreamBackend.h>
#include <stdexcept>
#include <BinaryStream.h>
#include <ChecksumStream.h>
#include <DeferredStream.h>
#include <array>
#include <bit>
#include <vector>

TEST_CASE("TextStream/qualified save field names coexist with section inheritance")
{
    const std::string text = "base { EndOfGameStat::TYPE_UNITS = 7; } derived:base { extra = 9; }\n";
    GAGCore::TextInputStream in(new GAGCore::MemoryStreamBackend(std::string(text)));
    in.readEnterSection("derived");
    CHECK(in.readSint32("EndOfGameStat::TYPE_UNITS") == 7);
    CHECK(in.readSint32("extra") == 9);
    in.readLeaveSection();
}

TEST_CASE("TextStream/raw binary fields roundtrip and malformed hex is rejected")
{
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::TextOutputStream output(bytes);
    const std::string json="{\"label\":\"quoted \\\"value\\\"\"}";
    output.write(json.data(),json.size(),"catalog"); output.flush();
    auto* copy=new GAGCore::MemoryStreamBackend(*bytes); copy->seekFromStart(0);
    GAGCore::TextInputStream input(copy);
    std::string restored(json.size(),'\0');
    input.read(restored.data(),restored.size(),"catalog");
    CHECK(restored==json);
    for (const auto* malformed : {"a = 0;", "a = 0011;", "a = zz;"})
    {
        GAGCore::TextInputStream invalid(new GAGCore::MemoryStreamBackend(std::string(malformed)));
        unsigned char value=0;
        CHECK_THROWS_AS(invalid.read(&value,1,"a"),std::runtime_error);
    }
}


namespace
{
void namedScalars(GAGCore::OutputStream& out, std::string_view name)
{
    out.writeEnterSection(name);
    out.writeSint8(-2, name); out.writeUint8(0xa5, name);
    out.writeSint16(-3, name); out.writeUint16(0xbeef, name);
    out.writeSint32(-4, name); out.writeUint32(0x11223344, name);
    out.writeFloat(1.5f, name); out.writeDouble(2.5, name);
    out.writeText(std::string("A\0B", 3), name);
    const unsigned char raw[] = {0, 0xff, 0x80};
    out.write(raw, sizeof(raw), name);
    out.writeOffset32(0x01020304, name);
    const Uint16 values[] = {0, 0x1234, 0xffff};
    out.writeUint16Sections(values, 3, name);
    out.writeLeaveSection();
}
std::vector<unsigned char> scalarWireBytes()
{
    std::vector<unsigned char> bytes = {
        0xfe, 0xa5, 0xff, 0xfd, 0xbe, 0xef,
        0xff, 0xff, 0xff, 0xfc, 0x11, 0x22, 0x33, 0x44,
        0x3f, 0xc0, 0, 0};
    // Binary streams preserve the historical native 32-bit word order for doubles.
    for (Uint32 word : std::bit_cast<std::array<Uint32, 2>>(2.5))
        for (int shift : {24, 16, 8, 0}) bytes.push_back(Uint8(word >> shift));
    const unsigned char tail[] = {
        0, 0, 0, 3, 'A', 0, 'B', 0, 0xff, 0x80,
        1, 2, 3, 4, 0, 0, 0x12, 0x34, 0xff, 0xff};
    bytes.insert(bytes.end(), std::begin(tail), std::end(tail));
    return bytes;
}
}

TEST_CASE("StreamFieldNames/borrowed names preserve scalar bytes and checksums")
{
    const auto expected = scalarWireBytes();
    const std::string storage = "prefix_field_suffix";
    const std::string_view name(storage.data() + 7, 5); // Deliberately not NUL-terminated.
    auto* memory = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream binary(memory);
    namedScalars(binary, name);
    REQUIRE(memory->getPosition() == expected.size());
    CHECK(std::equal(expected.begin(), expected.end(),
        reinterpret_cast<const unsigned char*>(memory->getBuffer())));
    Uint32 expectedHash = 2166136261u;
    for (auto byte : expected) expectedHash = (expectedHash ^ byte) * 16777619u;
    GAGCore::ChecksumStream checksum;
    namedScalars(checksum, name);
    CHECK(checksum.value == expectedHash);
    CHECK(checksum.getPosition() == expected.size());
    GAGCore::ChecksumStream otherNames;
    namedScalars(otherNames, std::string(256, 'x'));
    CHECK(otherNames.value == expectedHash);
}

TEST_CASE("StreamFieldNames/text consumes view length and temporary names immediately")
{
    auto* memory = new GAGCore::MemoryStreamBackend;
    GAGCore::TextOutputStream text(memory);
    const std::string storage = "prefix_field_suffix";
    text.writeSint32(7, std::string_view(storage).substr(7, 5));
    text.writeEnterSection(std::string("group"));
    text.writeText("hello", std::string("label"));
    text.writeLeaveSection();
    const std::string expected = "field = 7;\ngroup\n{\n\tlabel = \"hello\";\n}\n";
    CHECK(std::string(memory->getBuffer(), memory->getPosition()) == expected);
    GAGCore::TextInputStream input(new GAGCore::MemoryStreamBackend(std::string(expected)));
    CHECK(input.readSint32("field") == 7);
    input.readEnterSection("group");
    CHECK(input.readText("label") == "hello");
    input.readLeaveSection();
}

TEST_CASE("StreamFieldNames/deferred offset bookkeeping does not retain borrowed names")
{
    GAGCore::DeferredStream output;
    output.writeOffset32(4, std::string(256, 'x'));
    const auto result = output.takeSnapshot().finish();
    std::vector<unsigned char> bytes;
    result.forEachRange(0, result.size(), [&](const auto* p, size_t n) {
        bytes.insert(bytes.end(), p, p + n);
    });
    CHECK(bytes == std::vector<unsigned char>{0, 0, 0, 4});
}
