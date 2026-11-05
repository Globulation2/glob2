// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceClaims.h"
#include <random>

TEST_SUITE("ResourceClaims")
{
    TEST_CASE("reverse indexes survive swap removal retargeting and mutation during notifications")
    {
        ResourceClaims::Ledger ledger;ledger.reset(16);
        const auto changed=[](unsigned){};
        for (auto owner : {30u,10u,20u}) ledger.observe(owner,{3,4,0,0},changed);
        CHECK(ledger.ownersAtSource(3)==std::vector<ResourceClaims::Owner>{10,20,30});
        CHECK(ledger.ownersAtAccess(4)==std::vector<ResourceClaims::Owner>{10,20,30});
        ledger.release(10,changed); // Remove a middle packed element.
        ledger.observe(20,{3,5,1,7},changed); // Access-only and material retarget.
        ledger.observe(30,{6,4,0,0},changed); // Source-only retarget.
        CHECK(ledger.ownersAtSource(3)==std::vector<ResourceClaims::Owner>{20});
        CHECK(ledger.ownersAtSource(6)==std::vector<ResourceClaims::Owner>{30});
        CHECK(ledger.ownersAtAccess(4)==std::vector<ResourceClaims::Owner>{30});
        CHECK(ledger.ownersAtAccess(5)==std::vector<ResourceClaims::Owner>{20});
        CHECK_FALSE(ledger.acquire(30,{3,5,1,7},1,false,changed));
        CHECK(*ledger.find(30)==ResourceClaims::Claim{6,4,0,0});
        CHECK(ledger.ownersAtSource(6)==std::vector<ResourceClaims::Owner>{30});
        ledger.observe(10,{3,5,1,7},changed);
        for (const auto owner:ledger.ownersAtAccess(5)) ledger.release(owner,changed);
        CHECK(ledger.ownersAtSource(3).empty());CHECK(ledger.ownersAtAccess(5).empty());
        CHECK(ledger.ownersAtSource(6)==std::vector<ResourceClaims::Owner>{30});
        ledger.reset(16);
        CHECK(ledger.ownersAtSource(6).empty());CHECK(ledger.ownersAtAccess(4).empty());
    }
    TEST_CASE("reverse indexes match independent owner records through randomized mutations")
    {
        ResourceClaims::Ledger ledger;ledger.reset(8);
        const auto changed=[](unsigned){};
        std::map<ResourceClaims::Owner,ResourceClaims::Claim> expected;
        std::mt19937 random(192771);
        for (unsigned step=0;step<2000;++step) {
            const ResourceClaims::Owner owner=random()%80;
            if (random()%4==0) { ledger.release(owner,changed);expected.erase(owner); }
            else {
                const ResourceClaims::Claim claim{std::uint32_t(random()%8),std::uint32_t(random()%8),std::uint8_t(random()%3),std::uint16_t(random()%12),bool(random()%2)};
                if (random()%3==0) {
                    if (ledger.acquire(owner,claim,3,false,changed)) expected[owner]=claim;
                } else { ledger.observe(owner,claim,changed);expected[owner]=claim; }
            }
            for (unsigned tile=0;tile<8;++tile) {
                std::vector<ResourceClaims::Owner> sources,accesses;
                for (const auto& [id,claim]:expected) {
                    if (claim.source==tile)sources.push_back(id);
                    if (claim.access==tile)accesses.push_back(id);
                }
                CHECK(ledger.ownersAtSource(tile)==sources);
                CHECK(ledger.ownersAtAccess(tile)==accesses);
                CHECK(ledger.counts().at(tile)==sources.size());
            }
        }
    }

    TEST_CASE("mixed farm reserves retain the maximum floor across owner transfers")
    {
        ResourceClaims::Ledger ledger;ledger.reset(4);const auto changed=[](unsigned){};
        const ResourceClaims::Claim pooled{1,0,0,0,false,1},direct{1,1,0,0,false,0};
        REQUIRE(ledger.acquire(1,pooled,1,false,changed)); // Physical stock2, reserve1.
        CHECK_FALSE(ledger.acquire(2,direct,2,false,changed));
        CHECK(ledger.acquire(1,direct,2,false,changed)); // Own sole floor removed.
        REQUIRE(ledger.acquire(2,direct,2,false,changed));
        CHECK_FALSE(ledger.acquire(1,pooled,1,false,changed));
        ledger.release(2,changed);
        REQUIRE(ledger.acquire(1,pooled,1,false,changed));
        auto higher=pooled;higher.reserve=3;
        REQUIRE(ledger.acquire(2,higher,2,false,changed)); // Physical5, two reservations.
        CHECK_FALSE(ledger.acquire(3,direct,5,false,changed));
        ledger.release(2,changed); // Recompute remaining maximum1.
        REQUIRE(ledger.acquire(3,direct,3,false,changed));
        CHECK_FALSE(ledger.acquire(4,direct,3,false,changed));
        ledger.release(1,changed); // All positive floors gone.
        REQUIRE(ledger.acquire(4,direct,3,false,changed));
        REQUIRE(ledger.acquire(5,direct,3,false,changed));
        auto otherMaterial=pooled;otherMaterial.material=1;otherMaterial.reserve=100;
        REQUIRE(ledger.acquire(7,otherMaterial,1,false,changed));
        CHECK(ledger.available(3,direct,3,false)); // Other material's floor ignored.
        CHECK(ledger.available(8,direct,0,true)); // Infinite supply ignores floors.
    }
    TEST_CASE("reserve availability agrees with independent remaining-owner capacity oracle")
    {
        ResourceClaims::Ledger ledger;ledger.reset(4);const auto changed=[](unsigned){};
        std::map<ResourceClaims::Owner,ResourceClaims::Claim> expected;std::mt19937 random(88117);
        for(unsigned step=0;step<2500;++step) {
            const ResourceClaims::Owner owner=random()%32;
            if (random()%4==0) {ledger.release(owner,changed);expected.erase(owner);continue;}
            const ResourceClaims::Claim claim{std::uint32_t(random()%4),std::uint32_t(random()%4),std::uint8_t(random()%3),0,false,std::uint16_t(random()%5)};
            const unsigned physical=random()%12,capacity=physical>claim.reserve?physical-claim.reserve:0;
            unsigned count=0,floor=claim.reserve;
            for (const auto& [other,held]:expected)if(other!=owner && held.source==claim.source && held.material==claim.material) {
                ++count;floor=std::max(floor,unsigned(held.reserve));
            }
            const bool possible=physical>=floor && count+1<=physical-floor;
            CHECK(ledger.available(owner,claim,capacity)==possible);
            CHECK(ledger.acquire(owner,claim,capacity,false,changed)==possible);
            if(possible)expected[owner]=claim;
        }
    }

    TEST_CASE("owner keys distinguish equal generations in different slots and slot reuse")
    {
        const auto first=ResourceClaims::unitOwner(0,1);
        const auto neighbor=ResourceClaims::unitOwner(1,1);
        const auto replaced=ResourceClaims::unitOwner(0,2);
        CHECK(first!=neighbor);CHECK(first!=replaced);CHECK(neighbor!=replaced);
        CHECK(ResourceClaims::unitOwner(65535,UINT32_MAX)==0xffffffffffffull);
    }

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
