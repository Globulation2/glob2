// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <TextStream.h>
#include <StreamBackend.h>
#include <stdexcept>

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
