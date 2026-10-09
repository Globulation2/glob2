// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "EntityRandom.h"
#include <array>
#include <type_traits>

TEST_SUITE("EntityRandom")
{
    TEST_CASE("owner domain seed vectors and namespace separation")
    {
        struct Vector { unsigned domain, identity; std::array<std::uint32_t,4> expected; };
        // Independent fixed-width integer reference, separate from C++ implementation.
        const Vector vectors[]={
            {2,0,{0x588cc6bbu,0xa8555e47u,0x860b4af3u,0x6b5ffb84u}},
            {7,1,{0x803a37c4u,0xe8703b25u,0x664f568au,0x91ba6560u}},
            {11,257,{0x3d03d840u,0x60261fc1u,0x4e5fc054u,0x1b1d8232u}},
            {9,20,{0x126cf669u,0x329497d8u,0x0acdf97fu,0xd43ef4aeu}}
        };
        for (const auto& vector:vectors) {
            EntityRandom random;
            random.initializeOwner(713,vector.domain,vector.identity);
            for (auto expected:vector.expected) CHECK(random.nextU32()==expected);
        }
        EntityRandom random;
        CHECK_THROWS(random.initializeOwner(1,0));
        CHECK_THROWS(random.initializeOwner(1,1));
        CHECK_THROWS(random.initializeOwner(1,0x8000));
    }

    TEST_CASE("PCG32 reference seed 42 stream 54")
    {
        EntityRandom random;
        random.seed(42, 54);
        // Published PCG demo output, independent of our seed mixer.
        for (auto expected : {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                              0x83d2f293u, 0xbfa4784bu, 0xcbed606eu})
            CHECK(random.nextU32() == expected);
        static_assert(sizeof(EntityRandom) == 16);
        static_assert(std::is_trivially_copyable_v<EntityRandom>);
        static_assert(std::is_standard_layout_v<EntityRandom>);
    }

    TEST_CASE("salted initialization vectors lock seed kind gid and generation")
    {
        struct Vector {
            std::uint32_t seed;
            EntityRandom::Kind kind;
            std::uint16_t gid;
            std::uint32_t generation;
            EntityRandom::State state;
            std::array<std::uint32_t, 4> draws;
        };
        // Independently calculated with fixed-width Python integer arithmetic.
        const Vector vectors[] = {
            {1, EntityRandom::Kind::Unit, 0, 1,
             {0xb9aea995ddd964b1ULL, 0x20001ULL},
             {0xa1ff9c6b, 0x51e63840, 0xb3f6eeb1, 0xb80d133a}},
            {731, EntityRandom::Kind::Unit, 16385, 2,
             {0xbeee3db0b872468cULL, 0x48003ULL},
             {0x8482cdbb, 0xb89b8f90, 0xdc91bf3c, 0x2a10c640}},
            {731, EntityRandom::Kind::Building, 16385, 2,
             {0x0a0d7003ce5c1b17ULL, 0x2000000048003ULL},
             {0x20d72809, 0x1bcb4a59, 0x786e1ba9, 0x931da277}},
            {0xffffffff, EntityRandom::Kind::Building, 32767, 0xffffffff,
             {0xc0448e7e52c72d02ULL, 0x3fffffffeffffULL},
             {0x97cdee08, 0x9f82832f, 0x3a49cda9, 0x44007891}},
        };
        for (const auto& v : vectors) {
            EntityRandom random;
            random.initialize(v.seed, v.kind, v.gid, v.generation);
            CHECK(random.exportState() == v.state);
            for (auto expected : v.draws) CHECK(random.nextU32() == expected);
        }
    }

    TEST_CASE("interleaving and extra draws cannot change another stream")
    {
        std::array<EntityRandom, 16> sequential, interleaved;
        for (unsigned i = 0; i < sequential.size(); ++i) {
            sequential[i].initialize(19, EntityRandom::Kind::Unit, i, 1);
            interleaved[i] = sequential[i];
        }
        std::array<std::array<std::uint32_t, 100>, 16> expected;
        for (unsigned entity = 0; entity < sequential.size(); ++entity)
            for (auto& draw : expected[entity]) draw = sequential[entity].nextU32();
        for (unsigned tick = 0; tick < 100; ++tick)
            for (int entity = 15; entity >= 0; --entity)
                CHECK(interleaved[entity].nextU32() == expected[entity][tick]);
        CHECK(interleaved == sequential);
        auto first = sequential[0];
        for (int i = 0; i < 1000; ++i) first.nextU32();
        for (unsigned i = 1; i < sequential.size(); ++i) CHECK(sequential[i] == interleaved[i]);
    }

    TEST_CASE("state import resumes exactly and rejects invalid increments atomically")
    {
        EntityRandom random;
        random.seed(42, 54);
        for (int i = 0; i < 37; ++i) random.nextU32();
        EntityRandom restored;
        restored.importState(random.exportState());
        CHECK(restored == random);
        CHECK(restored.checksum() == random.checksum());
        auto before = restored;
        auto bad = restored.exportState(); bad.increment &= ~1ULL;
        CHECK_THROWS_AS(restored.importState(bad), std::runtime_error);
        CHECK(restored == before);
        CHECK_THROWS_AS(restored.initialize(1, EntityRandom::Kind::Unit, 0, 0), std::runtime_error);
        CHECK(restored == before);
        for (int i = 0; i < 1000; ++i) CHECK(restored.nextU32() == random.nextU32());
        auto altered = restored.exportState(); altered.value ^= 1;
        before.importState(altered);
        CHECK(before.checksum() != restored.checksum());
        altered = restored.exportState(); altered.increment ^= 2;
        before.importState(altered);
        CHECK(before.checksum() != restored.checksum());
    }
}
