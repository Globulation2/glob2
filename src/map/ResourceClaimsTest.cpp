// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "Property.h"
#include "ResourceClaims.h"
#include <map>
#include <random>

TEST_SUITE("ResourceClaims")
{
    TEST_CASE("reverse indexes survive swap removal retargeting and mutation during notifications")
    {
        ResourceClaims::Ledger ledger;ledger.reset(16);
        for (auto owner : {30u,10u,20u}) ledger.record(owner,{3,4,0,0});
        CHECK(ledger.ownersAtSource(3)==std::vector<ResourceClaims::Owner>{10,20,30});
        CHECK(ledger.ownersAtAccess(4)==std::vector<ResourceClaims::Owner>{10,20,30});
        ledger.release(10); // Remove a middle packed element.
        ledger.record(20,{3,5,1,7}); // Access-only and material retarget.
        ledger.record(30,{6,4,0,0}); // Source-only retarget.
        CHECK(ledger.ownersAtSource(3)==std::vector<ResourceClaims::Owner>{20});
        CHECK(ledger.ownersAtSource(6)==std::vector<ResourceClaims::Owner>{30});
        CHECK(ledger.ownersAtAccess(4)==std::vector<ResourceClaims::Owner>{30});
        CHECK(ledger.ownersAtAccess(5)==std::vector<ResourceClaims::Owner>{20});
        CHECK_FALSE(ledger.acquire(30,{3,5,1,7},1,false));
        CHECK(*ledger.find(30)==ResourceClaims::Claim{6,4,0,0});
        CHECK(ledger.ownersAtSource(6)==std::vector<ResourceClaims::Owner>{30});
        ledger.record(10,{3,5,1,7});
        for (const auto owner:ledger.ownersAtAccess(5)) ledger.release(owner);
        CHECK(ledger.ownersAtSource(3).empty());CHECK(ledger.ownersAtAccess(5).empty());
        CHECK(ledger.ownersAtSource(6)==std::vector<ResourceClaims::Owner>{30});
        ledger.reset(16);
        CHECK(ledger.ownersAtSource(6).empty());CHECK(ledger.ownersAtAccess(4).empty());
    }
    TEST_CASE("reverse indexes match independent owner records through randomized mutations")
    {
        glob2test::forEachSeed(8, [](std::uint32_t seed) {
            ResourceClaims::Ledger ledger;ledger.reset(8);
            std::map<ResourceClaims::Owner,ResourceClaims::Claim> expected;
            std::mt19937 random(seed);
            for (unsigned step=0;step<2000;++step) {
                const ResourceClaims::Owner owner=random()%80;
                if (random()%4==0) { ledger.release(owner);expected.erase(owner); }
                else {
                    const ResourceClaims::Claim claim{std::uint32_t(random()%8),std::uint32_t(random()%8),std::uint8_t(random()%3),std::uint16_t(random()%12),bool(random()%2)};
                    if (random()%3==0) {
                        if (ledger.acquire(owner,claim,3,false)) expected[owner]=claim;
                    } else { ledger.record(owner,claim);expected[owner]=claim; }
                }
                for (unsigned tile=0;tile<8;++tile) {
                    std::vector<ResourceClaims::Owner> sources,accesses;
                    for (const auto& [id,claim]:expected) {
                        if (claim.source==tile)sources.push_back(id);
                        if (claim.access==tile)accesses.push_back(id);
                    }
                    CHECK(ledger.ownersAtSource(tile)==sources);
                    CHECK(ledger.ownersAtAccess(tile)==accesses);
                    CHECK(ledger.ownersAtSource(tile).size()==sources.size());
                }
            }
        }, 192771);
    }

    TEST_CASE("mixed farm reserves retain the maximum floor across owner transfers")
    {
        ResourceClaims::Ledger ledger;ledger.reset(4);
        const ResourceClaims::Claim pooled{1,0,0,0,false,1},direct{1,1,0,0,false,0};
        REQUIRE(ledger.acquire(1,pooled,1,false)); // Physical stock2, reserve1.
        CHECK_FALSE(ledger.acquire(2,direct,2,false));
        CHECK(ledger.acquire(1,direct,2,false)); // Own sole floor removed.
        REQUIRE(ledger.acquire(2,direct,2,false));
        CHECK_FALSE(ledger.acquire(1,pooled,1,false));
        ledger.release(2);
        REQUIRE(ledger.acquire(1,pooled,1,false));
        auto higher=pooled;higher.reserve=3;
        REQUIRE(ledger.acquire(2,higher,2,false)); // Physical5, two reservations.
        CHECK_FALSE(ledger.acquire(3,direct,5,false));
        ledger.release(2); // Recompute remaining maximum1.
        REQUIRE(ledger.acquire(3,direct,3,false));
        CHECK_FALSE(ledger.acquire(4,direct,3,false));
        ledger.release(1); // All positive floors gone.
        REQUIRE(ledger.acquire(4,direct,3,false));
        REQUIRE(ledger.acquire(5,direct,3,false));
        auto otherMaterial=pooled;otherMaterial.material=1;otherMaterial.reserve=100;
        REQUIRE(ledger.acquire(7,otherMaterial,1,false));
        CHECK(ledger.available(3,direct,3,false)); // Other material's floor ignored.
        CHECK(ledger.available(8,direct,0,true)); // Infinite supply ignores floors.
    }
    TEST_CASE("reserve availability agrees with independent remaining-owner capacity oracle")
    {
        glob2test::forEachSeed(8, [](std::uint32_t seed) {
            ResourceClaims::Ledger ledger;ledger.reset(4);
            std::map<ResourceClaims::Owner,ResourceClaims::Claim> expected;std::mt19937 random(seed);
            for(unsigned step=0;step<2500;++step) {
                const ResourceClaims::Owner owner=random()%32;
                if (random()%4==0) {ledger.release(owner);expected.erase(owner);continue;}
                const ResourceClaims::Claim claim{std::uint32_t(random()%4),std::uint32_t(random()%4),std::uint8_t(random()%3),0,false,std::uint16_t(random()%5)};
                const unsigned physical=random()%12,capacity=physical>claim.reserve?physical-claim.reserve:0;
                unsigned count=0,floor=claim.reserve;
                for (const auto& [other,held]:expected)if(other!=owner && held.source==claim.source && held.material==claim.material) {
                    ++count;floor=std::max(floor,unsigned(held.reserve));
                }
                const bool possible=physical>=floor && count+1<=physical-floor;
                CHECK(ledger.available(owner,claim,capacity)==possible);
                CHECK(ledger.acquire(owner,claim,capacity,false)==possible);
                if(possible)expected[owner]=claim;
            }
        }, 88117);
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
        const ResourceClaims::Claim a{3, 2, 0, 0}, b{8, 7, 0, 0};
        REQUIRE(claims.acquire(10, a, 1, false));
        CHECK_FALSE(claims.acquire(20, a, 1, false));
        CHECK(claims.materialCount(3, 0) == 1);
        REQUIRE(claims.acquire(20, b, 1, false));
        CHECK_FALSE(claims.acquire(10, b, 1, false));
        CHECK(*claims.find(10) == a); // A failed transfer keeps the old claim.
        REQUIRE(claims.release(20));
        REQUIRE(claims.acquire(10, b, 1, false));
        CHECK(claims.materialCount(3, 0) == 0); CHECK(claims.materialCount(8, 0) == 1);
        // Re-acquiring the claim already held succeeds without counting twice.
        REQUIRE(claims.acquire(10, b, 1, false));
        CHECK(claims.materialCount(8, 0) == 1);
        CHECK(claims.release(10)); CHECK_FALSE(claims.release(10));
        CHECK(claims.size() == 0); CHECK(claims.materialCount(8, 0) == 0);
    }
    TEST_CASE("materials share a tile but destructive harvests are exclusive")
    {
        ResourceClaims::Ledger claims; claims.reset(4);
        const ResourceClaims::Claim food{1, 1, 0, 0}, wood{1, 1, 1, 0}, destroy{1, 1, 1, 0, true};
        REQUIRE(claims.acquire(1, food, 1, false));
        REQUIRE(claims.acquire(2, wood, 1, false));
        CHECK_FALSE(claims.acquire(2, destroy, 1, false));
        CHECK(claims.materialCount(1, 0) == 1); CHECK(claims.materialCount(1, 1) == 1);
        claims.release(1);
        REQUIRE(claims.acquire(2, destroy, 1, false));
        CHECK_FALSE(claims.acquire(3, food, 100, true));
    }
    TEST_CASE("large claimant counts remain exact through arbitrary release order")
    {
        ResourceClaims::Ledger claims; claims.reset(4);
        const ResourceClaims::Claim claim{1, 0, 0, 0};
        for (unsigned owner = 0; owner < 600; ++owner)
            REQUIRE(claims.acquire(owner, claim, 1, true));
        CHECK(claims.materialCount(1, 0) == 600); CHECK(claims.ownersAtSource(1).size() == 600);
        for (unsigned owner = 0; owner < 600; owner += 2) claims.release(owner);
        CHECK(claims.materialCount(1, 0) == 300);
        for (unsigned owner = 1; owner < 101; owner += 2) claims.release(owner);
        CHECK(claims.materialCount(1, 0) == 250);
        for (unsigned owner = 101; owner < 600; owner += 2) claims.release(owner);
        CHECK(claims.materialCount(1, 0) == 0); CHECK(claims.size() == 0);
        CHECK(claims.ownersAtSource(1).empty());
    }
    TEST_CASE("recorded claims preserve overcommitment and reject invalid identities")
    {
        ResourceClaims::Ledger claims; claims.reset(4);
        const ResourceClaims::Claim claim{1, 1, 0, 1024};
        claims.record(50, claim); claims.record(2, claim);
        CHECK(claims.materialCount(1, 0) == 2);
        CHECK_FALSE(claims.available(50, claim, 1));
        CHECK(claims.entries()[0].first == 2);
        CHECK(claims.entries()[0].second.resource == 1024);
        CHECK_THROWS_AS(claims.record(3, {4, 0, 0, 0}), std::out_of_range);
        CHECK_THROWS_AS(claims.record(3, {0, 0, 255, 0}), std::out_of_range);
        CHECK(claims.size() == 2);
        claims.reset(2); CHECK(claims.size() == 0); CHECK(claims.materialCount(1, 0) == 0);
    }
}

TEST_CASE("claimants are limited to one per spendable unit and leave exclusivity and floors intact" * doctest::test_suite("ResourceClaims"))
{
    using namespace ResourceClaims;
    for(const std::uint32_t stock:{0u,1u,2u,4u}) {
        CAPTURE(stock);
        Ledger ledger;ledger.reset(16);
        const Claim claim{1,1,0,0,false,0};
        unsigned admitted=0;
        for(Owner owner=1;owner<=32;++owner)admitted+=ledger.acquire(owner,claim,stock,false);
        CHECK(admitted==stock);
        // A holder re-checking its own claim never counts itself.
        if(admitted)CHECK(ledger.available(1,claim,stock,false));
    }
    // Exclusive claims admit a single holder and block every other claim on the tile.
    Ledger exclusive;exclusive.reset(16);
    const Claim sole{2,2,0,0,true,0};
    CHECK(exclusive.acquire(1,sole,1,false));
    CHECK_FALSE(exclusive.available(2,sole,1,false));
    CHECK_FALSE(exclusive.available(2,{2,2,0,0,false,0},4,false));
    // The shared seed floor is held back: physical stock 6 above floor 5
    // leaves one spendable unit, already taken by the first holder.
    Ledger floor;floor.reset(16);
    CHECK(floor.acquire(1,{3,3,0,0,false,5},1,false));
    CHECK_FALSE(floor.available(2,{3,3,0,0,false,0},6,false));
    CHECK_FALSE(floor.available(2,{3,3,0,0,false,6},0,false));
}
