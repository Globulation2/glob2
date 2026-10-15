// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <WordChecksum.h>
#include "ChecksumSchedule.h"
#include <array>

TEST_SUITE("WordChecksum")
{
    TEST_CASE("fixed word vectors use unsigned arithmetic and explicit half ordering")
    {
        GAGCore::WordChecksum hash;
        for (auto word : {0u, 1u, 0xffffffffu, 0x80000000u, 0x12345678u}) hash.addUint32(word);
        CHECK(hash.value == 0x02302befu);
        GAGCore::WordChecksum wide, halves;
        wide.addUint64(0x0123456789abcdefULL);
        halves.addUint32(0x89abcdefu); halves.addUint32(0x01234567u);
        CHECK(wide.value == halves.value);
    }
    TEST_CASE("word positions and individual bit changes are visible")
    {
        const std::array<unsigned,4> words{0, 1, 0x80000000u, 0xffffffffu};
        const auto digest=[](auto values) { GAGCore::WordChecksum h; for(auto v:values) h.addUint32(v); return h.value; };
        const auto original=digest(words);
        auto reordered=words; std::swap(reordered[0],reordered[1]);
        CHECK(digest(reordered)!=original);
        for(unsigned i=0;i<words.size();++i) for(unsigned bit=0;bit<32;++bit) {
            auto changed=words; changed[i]^=1u<<bit; CHECK(digest(changed)!=original);
        }
    }
    TEST_CASE("secondary coverage follows absolute ticks across save boundaries and wrap")
    {
        CHECK(SimulationChecksum::secondaryDue(0));
        CHECK_FALSE(SimulationChecksum::secondaryDue(31));
        CHECK(SimulationChecksum::secondaryDue(32));
        CHECK_FALSE(SimulationChecksum::secondaryDue(33));
        CHECK(SimulationChecksum::secondaryDue(0xffffffe0u));
        CHECK_FALSE(SimulationChecksum::secondaryDue(0xffffffffu));
    }
}
