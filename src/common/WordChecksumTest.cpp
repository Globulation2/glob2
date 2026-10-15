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
        CHECK(hash.value() == 0xbe4aafacu);
        // MurmurHash3 x86_32 (seed zero) reference vectors for the byte
        // strings "abcd" and "abcdefgh", packed explicitly little-endian.
        GAGCore::WordChecksum text;
        CHECK(text.value() == 0u);
        text.addUint32(0x64636261u);
        CHECK(text.value() == 0x43ed676au);
        CHECK(text.value() == 0x43ed676au);
        text.addUint32(0x68676665u);
        CHECK(text.value() == 0x49ddccc4u);
        GAGCore::WordChecksum wide, halves;
        wide.addUint64(0x0123456789abcdefULL);
        halves.addUint32(0x89abcdefu); halves.addUint32(0x01234567u);
        CHECK(wide.value() == halves.value());
    }
    TEST_CASE("word positions and individual bit changes are visible")
    {
        const std::array<unsigned,4> words{0, 1, 0x80000000u, 0xffffffffu};
        const auto digest=[](auto values) { GAGCore::WordChecksum h; for(auto v:values) h.addUint32(v); return h.value(); };
        const auto original=digest(words);
        auto reordered=words; std::swap(reordered[0],reordered[1]);
        CHECK(digest(reordered)!=original);
        for(unsigned i=0;i<words.size();++i) for(unsigned bit=0;bit<32;++bit) {
            auto changed=words; changed[i]^=1u<<bit; CHECK(digest(changed)!=original);
        }
    }
    TEST_CASE("paired bit changes do not cancel as in the XOR multiply mixer")
    {
        const auto digest=[](auto values) { GAGCore::WordChecksum h; for(auto v:values) h.addUint32(v); return h.value(); };
        const std::array<std::uint32_t,2> zeros{0,0}, highBits{0x80000000u,0x80000000u};
        CHECK(digest(zeros) == 0x63852afcu);
        CHECK(digest(highBits) == 0xf933b812u);
        CHECK(digest(zeros) != digest(highBits));
        for (const auto words : {std::array<std::uint32_t,4>{0,0,0,0},
                                std::array<std::uint32_t,4>{1,2,3,4},
                                std::array<std::uint32_t,4>{0xffffffffu,0,0x80000000u,17}}) {
            const auto original=digest(words);
            for(unsigned i=0;i<words.size();++i) for(unsigned j=i+1;j<words.size();++j)
                for(unsigned bit=0;bit<32;++bit) {
                    auto changed=words; changed[i]^=1u<<bit; changed[j]^=1u<<bit;
                    CHECK(digest(changed)!=original);
                }
        }
        GAGCore::WordChecksum empty, oneZero;
        oneZero.addUint32(0);
        CHECK(oneZero.value() == 0x2362f9deu);
        CHECK(empty.value() != oneZero.value());
        CHECK(oneZero.value() != digest(zeros));
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
