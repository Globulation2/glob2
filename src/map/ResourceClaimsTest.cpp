// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceClaims.h"

TEST_SUITE("ResourceClaims")
{
    TEST_CASE("finite packets transfer atomically and release idempotently")
    {
        ResourceClaims::Ledger claims; claims.reset(16);
        std::vector<unsigned> dirty;
        const auto mark = [&](unsigned tile) { dirty.push_back(tile); };
        const ResourceClaims::Claim a{3, 2, 0, 0}, b{8, 7, 0, 0};
        REQUIRE(claims.acquire(10, a, 1, false, mark));
        CHECK_FALSE(claims.acquire(20, a, 1, false, mark));
        CHECK(claims.counts().at(3) == 1);
        REQUIRE(claims.acquire(20, b, 1, false, mark));
        CHECK_FALSE(claims.acquire(10, b, 1, false, mark));
        CHECK(*claims.find(10) == a); // A failed transfer keeps the old claim.
        REQUIRE(claims.release(20, mark));
        REQUIRE(claims.acquire(10, b, 1, false, mark));
        CHECK(claims.counts().at(3) == 0); CHECK(claims.counts().at(8) == 1);
        const auto marks = dirty.size();
        REQUIRE(claims.acquire(10, b, 1, false, mark));
        CHECK(dirty.size() == marks);
        CHECK(claims.release(10, mark)); CHECK_FALSE(claims.release(10, mark));
        CHECK(claims.size() == 0); CHECK(claims.counts().at(8) == 0);
    }
    TEST_CASE("materials share a tile but destructive harvests are exclusive")
    {
        ResourceClaims::Ledger claims; claims.reset(4);
        const auto mark = [](unsigned) {};
        const ResourceClaims::Claim food{1, 1, 0, 0}, wood{1, 1, 1, 0}, destroy{1, 1, 1, 0, true};
        REQUIRE(claims.acquire(1, food, 1, false, mark));
        REQUIRE(claims.acquire(2, wood, 1, false, mark));
        CHECK_FALSE(claims.acquire(2, destroy, 1, false, mark));
        CHECK(claims.materialCount(1, 0) == 1); CHECK(claims.materialCount(1, 1) == 1);
        claims.release(1, mark);
        REQUIRE(claims.acquire(2, destroy, 1, false, mark));
        CHECK_FALSE(claims.acquire(3, food, 100, true, mark));
    }
    TEST_CASE("byte overflow remains exact through arbitrary release order")
    {
        ResourceClaims::Ledger claims; claims.reset(4);
        const auto mark = [](unsigned) {};
        const ResourceClaims::Claim claim{1, 0, 0, 0};
        for (unsigned owner = 0; owner < 600; ++owner)
            REQUIRE(claims.acquire(owner, claim, 1, true, mark));
        CHECK(claims.counts().cells[1] == 255); CHECK(claims.counts().at(1) == 600);
        for (unsigned owner = 0; owner < 600; owner += 2) claims.release(owner, mark);
        CHECK(claims.counts().at(1) == 300);
        for (unsigned owner = 1; owner < 101; owner += 2) claims.release(owner, mark);
        CHECK(claims.counts().at(1) == 250); CHECK(claims.counts().overflow.empty());
        for (unsigned owner = 101; owner < 600; owner += 2) claims.release(owner, mark);
        CHECK(claims.counts().at(1) == 0); CHECK(claims.size() == 0);
    }
    TEST_CASE("shadow claims preserve overcommitment and reject invalid identities")
    {
        ResourceClaims::Ledger claims; claims.reset(4);
        const auto mark = [](unsigned) {};
        const ResourceClaims::Claim claim{1, 1, 0, 1024};
        claims.observe(50, claim, mark); claims.observe(2, claim, mark);
        CHECK(claims.counts().at(1) == 2);
        CHECK_FALSE(claims.available(50, claim, 1));
        CHECK(claims.entries()[0].first == 2);
        CHECK(claims.entries()[0].second.resource == 1024);
        CHECK_THROWS_AS(claims.observe(3, {4, 0, 0, 0}, mark), std::out_of_range);
        CHECK_THROWS_AS(claims.observe(3, {0, 0, 255, 0}, mark), std::out_of_range);
        CHECK(claims.size() == 2);
        claims.reset(2); CHECK(claims.size() == 0); CHECK(claims.counts().at(1) == 0);
    }
}
