// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <TextStream.h>
#include <StreamBackend.h>

TEST_CASE("TextStream/qualified save field names coexist with section inheritance")
{
    const std::string text = "base { EndOfGameStat::TYPE_UNITS = 7; } derived:base { extra = 9; }\n";
    GAGCore::TextInputStream in(new GAGCore::MemoryStreamBackend(std::string(text)));
    in.readEnterSection("derived");
    CHECK(in.readSint32("EndOfGameStat::TYPE_UNITS") == 7);
    CHECK(in.readSint32("extra") == 9);
    in.readLeaveSection();
}
