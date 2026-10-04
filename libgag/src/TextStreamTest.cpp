// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <TextStream.h>
#include <StreamBackend.h>
TEST_SUITE("TextStream")
{
TEST_CASE("legacy scoped statistics keys preserve section inheritance")
{
    const std::string text="base { plain = 3; EndOfGameStat::TYPE_UNITS = 7; } copy : base { nested::Stat::field = 9; }";
    auto *backend=new GAGCore::MemoryStreamBackend;
    backend->write(text.data(),text.size());backend->seekFromStart(0);
    GAGCore::TextInputStream stream(backend);
    stream.readEnterSection("copy");
    CHECK(stream.readUint32("plain")==3);
    CHECK(stream.readUint32("EndOfGameStat::TYPE_UNITS")==7);
    CHECK(stream.readUint32("nested::Stat::field")==9);
}
}
