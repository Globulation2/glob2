// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ActiveMaterialClaims.h"
#include "Version.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>

namespace
{
std::string saveRoutes(Map& map, bool text)
{
    auto* backend = new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> output(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
    map.saveActiveClaimRoutes(output.get());
    output->flush();
    return backend->takeContents();
}
void restoreRoutes(Map& map, const std::string& bytes, bool text, int version = VERSION_MINOR)
{
    auto backend = std::make_unique<GAGCore::MemoryStreamBackend>();
    backend->write(bytes.data(), bytes.size()); backend->seekFromStart(0);
    if (text) { GAGCore::TextInputStream input(backend.get()); map.loadActiveClaimRoutes(&input, version); }
    else { GAGCore::BinaryInputStream input(backend.release()); map.loadActiveClaimRoutes(&input, version); }
}
void enable(Map& map)
{
    map.setMaterialDestinationsEnabled(true);
    map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
}
struct Assignment
{
    Unit& unit;
    ~Assignment() { unit.releaseActiveMaterialClaim(); unit.attachedBuilding = nullptr; }
};
void assign(Unit& unit, Building& building, Map& map, int x, int y)
{
    unit.releaseActiveMaterialClaim();
    unit.attachedBuilding = &building; unit.activity = Unit::ACT_FILLING;
    unit.displacement = Unit::DIS_GOING_TO_RESOURCE; unit.destinationPurpose = WHEAT; unit.medical = Unit::MED_FREE;
    const auto target = Uint32(map.coordToIndex(x, y));
    map.setResourceByIndex(x, y, WHEAT, 1);
    map.setMaterialAmountSlot(target, WHEAT, 3);
    REQUIRE(unit.acquireMaterialClaimAt(target));
    REQUIRE(unit.activeMaterialClaimValid());
}
}

TEST_SUITE("ActiveClaimRoutes")
{
    TEST_CASE("pending and published routes preserve publication and bytes across binary and text saves")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned threads : {1u, 3u}) for (bool text : {false, true}) {
            CAPTURE(threads); CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
            auto& game = fixture.game; auto& map = game.map;
            map.computeExecutor().configure(threads); enable(map);
            auto* building = fixture.addBuilding("inn", 2, 2);
            auto* unit = fixture.addUnit(WORKER, 8, 8); Assignment cleanup{*unit}; assign(*unit, *building, map, 12, 8);
            game.observeCompletedWorld();
            int dx = 99, dy = 99;
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();
            const auto before = map.activeClaimRouteChecksum(true);
            const auto pending = saveRoutes(map, text);
            restoreRoutes(map, pending, text);
            CHECK(map.activeClaimRouteChecksum(true) == before);
            CHECK(saveRoutes(map, text) == pending);
            map.publishClaimRoutes();
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            ++game.stepCounter; map.publishClaimRoutes();
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
            CHECK(dx == 1); CHECK(std::abs(dy) <= 1);
            const auto published = saveRoutes(map, text);
            const auto publishedHash = map.activeClaimRouteChecksum(true);
            restoreRoutes(map, published, text);
            CHECK(map.activeClaimRouteChecksum(true) == publishedHash);
            CHECK(saveRoutes(map, text) == published);
            // Merely querying an unexecuted direction must not advance cursor.
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
            CHECK(saveRoutes(map, text) == published);
        }
    }
    TEST_CASE("changed tasks and blocked cached steps cannot reuse an old route")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
        auto& game = fixture.game; auto& map = game.map; enable(map);
        auto* building = fixture.addBuilding("inn", 2, 2);
        auto* unit = fixture.addUnit(WORKER, 8, 8); Assignment cleanup{*unit}; assign(*unit, *building, map, 12, 8);
        game.observeCompletedWorld();
        int dx = 0, dy = 0;
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes();
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
        const auto beforeTraffic = saveRoutes(map, false);
        fixture.addUnit(WORKER, unit->posX + dx, unit->posY + dy);
        map.markImmobileUnit(unit->posX + dx, unit->posY + dy, unit->owner->teamNumber);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        CHECK(unit->activeMaterialClaim.has_value());
        CHECK(saveRoutes(map, false) == beforeTraffic);
        map.addForbidden(unit->posX + dx, unit->posY + dy, unit->owner->teamNumber);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Failed);
        // Complete the next immutable observation before admitting another tick.
        game.invalidateObservation(); game.observeCompletedWorld();
        assign(*unit, *building, map, 8, 12);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        map.cancelClaimRoute(ResourceClaims::unitOwner(unit->gid, unit->scriptIdentity));
        map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes();
        game.invalidateObservation(); game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
    }
    TEST_CASE("disabled and historical route state has no implicit active policy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        auto& map = fixture.game.map;
        const auto before = map.activeClaimRouteChecksum(true);
        const auto disabled = saveRoutes(map, false); restoreRoutes(map, disabled, false);
        CHECK_FALSE(map.activeClaimsEnabled()); CHECK(map.activeClaimRouteChecksum(true) == before);
        enable(map);
        restoreRoutes(map, {}, false, 194);
        CHECK_FALSE(map.activeClaimsEnabled());
    }
}
