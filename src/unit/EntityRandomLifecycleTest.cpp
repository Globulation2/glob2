// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "EntityRandomIO.h"
#include "Version.h"
#include "sim/snapshot/WorldSnapshot.h"
#include "AI.h"
#include "AIImplementation.h"
#include "ai/observation/AIWorldView.h"
#include <StreamBackend.h>
#include <TextStream.h>
#include <memory>
#include <fstream>

namespace
{
std::string save(Game& game, bool text = false, bool map = false)
{
    auto* backend = new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> output(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
    game.save(output.get(), map, "entity RNG continuation");
    output->flush();
    return backend->takeContents();
}
bool load(Game& game, const std::string& bytes, bool text = false)
{
    auto* backend = new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size());
    backend->seekFromStart(0);
    std::unique_ptr<GAGCore::InputStream> input(text
        ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend))
        : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend)));
    return game.load(input.get());
}
}

TEST_SUITE("EntityRandomLifecycle")
{
    TEST_CASE("world owners are isolated and their progress changes lockstep checksums")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.loadDefaultRace=true, .header=true});
        auto& game=world.game;
        game.privateRandom(RandomDomain::GrowthJobs);
        const auto before=game.map.worldRandom.streams;
        const auto oldWorld=game.syncRandom;
        const auto checksum=game.checkSum();
        const auto mapChecksum=game.map.checkSum(false);
        for (int n=0;n<37;++n) game.privateRandom(RandomDomain::ResourcePlacement).nextU32();
        CHECK(game.checkSum()!=checksum);
        CHECK(game.map.checkSum(false)!=mapChecksum);
        CHECK(game.syncRandom==oldWorld);
        for (unsigned i=0;i<WorldRandomStreams::Count;++i)
            if (i!=unsigned(RandomDomain::ResourcePlacement)-2)
                CHECK(game.map.worldRandom.streams[i]==before[i]);
        // Two privately generated maps never consume a global/test stream.
        const auto legacy=syncRandEngine();
        game.map.smoothResources(1);
        CHECK(syncRandEngine()==legacy);
    }

    TEST_CASE("world streams continue through binary and text saves without resume reseeding")
    {
        glob2test::HeadlessGlobals globals;
        for (bool text : {false,true}) {
            glob2test::HeadlessGame world({.loadDefaultRace=true, .header=true});
            auto& game=world.game;
            game.privateRandom(RandomDomain::GrowthJobs);
            for (unsigned i=0;i<WorldRandomStreams::Count;++i)
                for (unsigned n=0;n<7+i;++n) game.map.worldRandom.streams[i].nextU32();
            const auto bytes=save(game,text);
            glob2test::HeadlessGame restored({.loadDefaultRace=true});
            REQUIRE(load(restored.game,bytes,text));
            CHECK(restored.game.map.worldRandom.streams==game.map.worldRandom.streams);
            const auto progress=restored.game.map.worldRandom.streams;
            restored.game.setGameHeader(restored.game.gameHeader,false);
            CHECK(restored.game.map.worldRandom.streams==progress);
            for (unsigned i=0;i<WorldRandomStreams::Count;++i)
                for (unsigned n=0;n<100;++n)
                    CHECK(restored.game.map.worldRandom.streams[i].nextU32()==game.map.worldRandom.streams[i].nextU32());
        }
    }

    TEST_CASE("malformed new world RNG records fail rather than migrating")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
        auto& random=world.game.privateRandom(RandomDomain::GrowthJobs);
        random.nextU32();
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);
        saveEntityRandom(&output,random); output.flush();
        const auto record=backend->takeContents();
        const auto valid=save(world.game);
        const auto position=valid.find(record);
        REQUIRE(position!=std::string::npos);
        REQUIRE(valid.find(record,position+1)==std::string::npos);
        for (bool truncate : {false,true}) {
            auto bytes=valid;
            if (truncate) bytes.resize(position+record.size()-1);
            else bytes[position+record.size()-1]&=char(0xfe);
            glob2test::HeadlessGame restored({.loadDefaultRace=true});
            bool accepted=false;
            try { accepted=load(restored.game,bytes); } catch (const std::exception&) {}
            CHECK_FALSE(accepted);
        }
    }

    TEST_CASE("story identities isolate extra draws and fixed domains differ")
    {
        MapScriptSGSL script;
        Story first(&script), second(&script);
        first.random.initializeOwner(713,unsigned(RandomDomain::LegacyStory),0);
        second.random.initializeOwner(713,unsigned(RandomDomain::LegacyStory),1);
        auto expected=second.random;
        CHECK(first.random.exportState().increment!=second.random.exportState().increment);
        for (int n=0;n<50;++n) first.random.nextU32();
        for (int n=0;n<50;++n) CHECK(second.random.nextU32()==expected.nextU32());
        EntityRandom map;
        map.initializeOwner(713,unsigned(RandomDomain::GrowthJobs));
        CHECK(map.exportState().increment!=first.random.exportState().increment);
    }

    TEST_CASE("entity operations consume only their own streams outside a tick")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2, .clearImmobile=true, .header=true, .seed=731});
        world.game.privateRandom(RandomDomain::GrowthJobs);
        const auto activeWorldRandom = world.game.map.worldRandom.streams;
        const auto worldRandom = world.game.syncRandom;
        auto* worker = world.addUnit(WORKER, 8, 8);
        auto* flyer = world.addUnit(EXPLORER, 16, 16);
        auto* building = world.addBuilding("inn", 3, 3);
        auto* otherTeam = world.addUnit(WORKER, 24, 24, 1);
        REQUIRE(worker); REQUIRE(flyer); REQUIRE(building); REQUIRE(otherTeam);
        CHECK(world.game.map.worldRandom.streams == activeWorldRandom);
        CHECK(world.game.syncRandom == worldRandom); // creation does not draw from world
        AI controller(AI::NUMBI, world.game.players[0]);
        controller.getOrder(false);
        const auto aiRandom = controller.aiImplementation->snapshotRandom();
        const auto workerBefore = worker->entityRandom;
        const auto otherBefore = otherTeam->entityRandom;
        const auto buildingBefore = building->entityRandom;
        const auto flyerBefore = flyer->entityRandom;
        world.game.map.pathfindRandom(worker);
        CHECK(worker->entityRandom != workerBefore);
        CHECK(flyer->entityRandom == flyerBefore);
        // Force the random-fly action, without running ecology or another unit.
        flyer->dx = flyer->dy = 0;
        flyer->delta = 255;
        flyer->syncStep();
        CHECK(flyer->entityRandom != flyerBefore);
        CHECK(building->entityRandom == buildingBefore);
        auto expectedBuilding = buildingBefore;
        expectedBuilding.nextU32();
        CHECK(building->neededMaterial() != MATERIAL_TYPE_NONE);
        CHECK(building->entityRandom == expectedBuilding);
        CHECK(otherTeam->entityRandom == otherBefore);
        CHECK(world.game.map.worldRandom.streams == activeWorldRandom);
        CHECK(world.game.syncRandom == worldRandom);
        const auto checksum = worker->checkSum(nullptr);
        worker->entityRandom.nextU32();
        CHECK(worker->checkSum(nullptr) != checksum);
        const auto buildingChecksum = building->checkSum(nullptr);
        building->entityRandom.nextU32();
        CHECK(building->checkSum(nullptr) != buildingChecksum);
        CHECK(controller.aiImplementation->snapshotRandom() == aiRandom);
    }

    TEST_CASE("gradient sidesteps draw from the moving unit rather than the target building")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true, .header=true, .seed=731});
        auto* worker = world.addUnit(WORKER, 12, 12);
        auto* target = world.addBuilding("inn", 3, 3);
        REQUIRE(worker); REQUIRE(target);
        const auto targetRandom = target->entityRandom;
        world.game.privateRandom(RandomDomain::GrowthJobs);
        const auto activeWorldRandom = world.game.map.worldRandom.streams;
        const auto worldRandom = world.game.syncRandom;
        auto expected = worker->entityRandom;
        expected.nextU32();
        std::vector<Uint16> gradient(world.game.map.getW() * world.game.map.getH(), 100);
        int dx = 0, dy = 0;
        REQUIRE(world.game.map.directionByGradient(worker->entityRandom,
            worker->owner->me, worker->swimClass(), worker->posX, worker->posY,
            gradient.data(), &dx, &dy, false));
        CHECK(worker->entityRandom == expected);
        CHECK(target->entityRandom == targetRandom);
        CHECK(world.game.map.worldRandom.streams == activeWorldRandom);
        CHECK(world.game.syncRandom == worldRandom);
        CHECK((dx != 0 || dy != 0));
    }

    TEST_CASE("team steps consume entity streams without consuming world randomness")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2, .clearImmobile=true, .header=true, .seed=731});
        auto* worker = world.addUnit(WORKER, 12, 12);
        auto* other = world.addUnit(EXPLORER, 20, 20, 1);
        REQUIRE(worker); REQUIRE(other);
        world.game.privateRandom(RandomDomain::GrowthJobs);
        const auto activeWorldRandom = world.game.map.worldRandom.streams;
        const auto worldRandom = world.game.syncRandom;
        const auto otherRandom = other->entityRandom;
        for (int tick = 0; tick < 64; ++tick) world.game.teams[0]->syncStep();
        CHECK(world.game.map.worldRandom.streams == activeWorldRandom);
        CHECK(world.game.syncRandom == worldRandom);
        CHECK(other->entityRandom == otherRandom);
    }

    TEST_CASE("upgrades preserve streams and reused slots receive a fresh salt")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true, .header=true, .seed=19});
        auto* worker = world.addUnit(WORKER, 12, 12);
        REQUIRE(worker);
        const auto gid = worker->gid;
        const auto generation = worker->scriptIdentity;
        worker->entityRandom.nextU32();
        const auto before = worker->entityRandom;
        worker->resetAtLevel(1);
        CHECK(worker->entityRandom == before);
        CHECK(worker->scriptIdentity == generation);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(12, 12, Game::DEL_UNIT));
        auto* replacement = world.addUnit(WORKER, 12, 12);
        REQUIRE(replacement);
        CHECK(replacement->gid == gid);
        CHECK(replacement->scriptIdentity == generation + 1);
        CHECK(replacement->entityRandom.exportState().increment != before.exportState().increment);
        auto* building = world.addBuilding("inn", 3, 3);
        REQUIRE(building);
        const auto buildingGid = building->gid;
        const auto buildingGeneration = building->scriptIdentity;
        const auto buildingRandom = building->entityRandom;
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(3, 3, Game::DEL_BUILDING));
        auto* newBuilding = world.addBuilding("inn", 3, 3);
        REQUIRE(newBuilding);
        CHECK(newBuilding->gid == buildingGid);
        CHECK(newBuilding->scriptIdentity == buildingGeneration + 1);
        CHECK(newBuilding->entityRandom.exportState().increment != buildingRandom.exportState().increment);
    }

    TEST_CASE("fresh header reseeds starting entities but resume header preserves progress")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.header=true, .seed=19});
        auto* unit = world.addUnit(WORKER, 12, 12);
        auto* building = world.addBuilding("inn", 3, 3);
        REQUIRE(unit); REQUIRE(building);
        unit->entityRandom.nextU32(); building->entityRandom.nextU32();
        auto header = world.game.gameHeader;
        header.setRandomSeed(731);
        world.game.setGameHeader(header, true);
        EntityRandom expectedUnit, expectedBuilding;
        expectedUnit.initialize(731, EntityRandom::Kind::Unit, unit->gid, unit->scriptIdentity);
        expectedBuilding.initialize(731, EntityRandom::Kind::Building, building->gid, building->scriptIdentity);
        CHECK(unit->entityRandom == expectedUnit);
        CHECK(building->entityRandom == expectedBuilding);
        world.step(7);
        const auto bytes = save(world.game);
        glob2test::HeadlessGame restored({.header=true});
        REQUIRE(load(restored.game, bytes));
        auto* loadedUnit = restored.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)];
        auto* loadedBuilding = restored.game.teams[0]->myBuildings[Building::GIDtoID(building->gid)];
        REQUIRE(loadedUnit); REQUIRE(loadedBuilding);
        CHECK(loadedUnit->entityRandom == unit->entityRandom);
        CHECK(loadedBuilding->entityRandom == building->entityRandom);
        restored.game.setGameHeader(restored.game.gameHeader, true);
        restored.game.setGameHeader(restored.game.gameHeader, true);
        CHECK(loadedUnit->entityRandom == unit->entityRandom);
        CHECK(loadedBuilding->entityRandom == building->entityRandom);
    }

    TEST_CASE("snapshots freeze both unit and building RNG state")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.header=true});
        auto* unit = world.addUnit(WORKER, 12, 12);
        auto* building = world.addBuilding("inn", 3, 3);
        REQUIRE(unit); REQUIRE(building);
        unit->entityRandom.nextU32(); building->entityRandom.nextU32();
        const auto captured = SimulationSnapshot::capture(world.game, SimulationSnapshot::captureCatalog(world.game));
        AIEngine::AIWorldView frozen(captured);
        REQUIRE(frozen.unit(Game::refOf(unit)));
        REQUIRE(frozen.building(Game::refOf(building)));
        const auto unitBefore = unit->entityRandom;
        const auto buildingBefore = building->entityRandom;
        unit->entityRandom.nextU32(); building->entityRandom.nextU32();
        CHECK(frozen.unit(Game::refOf(unit))->entityRandom == unitBefore);
        CHECK(frozen.building(Game::refOf(building))->entityRandom == buildingBefore);
    }

    TEST_CASE("binary and text saves preserve exact private state and subsequent draws")
    {
        glob2test::HeadlessGlobals globals;
        for (bool text : {false, true}) {
            glob2test::HeadlessGame world({.header=true, .seed=731});
            auto* unit = world.addUnit(WORKER, 12, 12);
            auto* building = world.addBuilding("inn", 3, 3);
            REQUIRE(unit); REQUIRE(building);
            for (int i = 0; i < 51; ++i) unit->entityRandom.nextU32();
            for (int i = 0; i < 17; ++i) building->entityRandom.nextU32();
            const auto bytes = save(world.game, text);
            glob2test::HeadlessGame restored({.header=true});
            REQUIRE(load(restored.game, bytes, text));
            auto* u = restored.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)];
            auto* b = restored.game.teams[0]->myBuildings[Building::GIDtoID(building->gid)];
            REQUIRE(u); REQUIRE(b);
            CHECK(u->entityRandom == unit->entityRandom);
            CHECK(b->entityRandom == building->entityRandom);
            for (int i = 0; i < 100; ++i) {
                CHECK(u->entityRandom.nextU32() == unit->entityRandom.nextU32());
                CHECK(b->entityRandom.nextU32() == building->entityRandom.nextU32());
            }
        }
    }

    TEST_CASE("format 150 area state survives one-time RNG migration and continuation [save-format][artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.header=true, .seed=731});
        auto* unit = world.addUnit(WORKER, 12, 12);
        auto* building = world.addBuilding("inn", 3, 3);
        REQUIRE(unit); REQUIRE(building);
        unit->areaServiceRemainders = {17, 83, 241};
        unit->areaLastPulseTick = 0;
        building->areaFunded = true;
        building->areaFundingTeam = 0;
        building->areaFundingType = building->typeNum;
        building->areaFundingTick = 0;
        unit->entityRandom.nextU32(); building->entityRandom.nextU32();
        auto bytes = save(world.game, true);
        // Reconstruct the released format-150 text layout: keep its area fields,
        // but remove every later RNG section, rather than just patching a header.
        auto eraseSections = [&](const std::string& name) {
            for (auto start = bytes.find(name + "\n"); start != std::string::npos;
                 start = bytes.find(name + "\n")) {
                const auto open = bytes.find('{', start);
                REQUIRE(open != std::string::npos);
                std::size_t end = open;
                unsigned depth = 0;
                do {
                    REQUIRE(end < bytes.size());
                    if (bytes[end] == '{') ++depth;
                    if (bytes[end] == '}') --depth;
                    ++end;
                } while (depth);
                bytes.erase(start, end - start);
            }
        };
        eraseSections("worldRandom");
        eraseSections("entityRandom");
        const auto version = bytes.find("versionMinor = " + std::to_string(VERSION_MINOR));
        REQUIRE(version != std::string::npos);
        bytes.replace(version, std::string("versionMinor = " + std::to_string(VERSION_MINOR)).size(),
                      "versionMinor = 150");
        std::ofstream(glob2test::artifactDir() / "format-150.game.txt", std::ios::binary) << bytes;
        glob2test::HeadlessGame migrated({.header=true});
        REQUIRE(load(migrated.game, bytes, true));
        auto* u = migrated.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)];
        auto* b = migrated.game.teams[0]->myBuildings[Building::GIDtoID(building->gid)];
        REQUIRE(u); REQUIRE(b);
        CHECK(u->areaServiceRemainders == unit->areaServiceRemainders);
        CHECK(u->areaLastPulseTick == unit->areaLastPulseTick);
        CHECK(b->areaFunded == building->areaFunded);
        CHECK(b->areaFundingType == building->areaFundingType);
        CHECK(b->areaFundingTeam == building->areaFundingTeam);
        CHECK(b->areaFundingTick == building->areaFundingTick);
        EntityRandom expectedUnit, expectedBuilding;
        expectedUnit.initialize(731, EntityRandom::Kind::Unit, u->gid, u->scriptIdentity);
        expectedBuilding.initialize(731, EntityRandom::Kind::Building, b->gid, b->scriptIdentity);
        CHECK(u->entityRandom == expectedUnit);
        CHECK(b->entityRandom == expectedBuilding);
        auto components = [](Game& game) {
            std::vector<Uint32> state, buildings, units;
            game.checkSum(&state, &buildings, &units, true);
            // The loaded header retains 150 while the resave writes 152. Its
            // format checksum differs intentionally; authoritative state must not.
            state.erase(state.begin());
            state.insert(state.end(), buildings.begin(), buildings.end());
            state.insert(state.end(), units.begin(), units.end());
            return state;
        };
        for (bool text : {false, true}) {
            glob2test::HeadlessGame restored({.header=true});
            REQUIRE(load(restored.game, save(migrated.game, text), text));
            CHECK(components(restored.game) == components(migrated.game));
            for (int tick = 0; tick < 64; ++tick) {
                migrated.step(); restored.step();
                CHECK(components(restored.game) == components(migrated.game));
            }
        }
    }

    TEST_CASE("truncated binary RNG state and even increments are rejected")
    {
        EntityRandom random;
        random.seed(42, 54);
        auto* backend = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);
        saveEntityRandom(&output, random);
        const auto valid = backend->takeContents();
        REQUIRE(valid.size() == 16);
        for (std::size_t length = 0; length < valid.size(); ++length) {
            auto* inputBackend = new GAGCore::MemoryStreamBackend(valid.data(), length);
            inputBackend->seekFromStart(0);
            GAGCore::BinaryInputStream input(inputBackend);
            const auto before = random;
            CHECK_THROWS(loadEntityRandom(&input, random));
            CHECK(random == before);
        }
        auto even = valid;
        even.back() &= char(0xfe); // BinaryStream uses network byte order.
        auto* inputBackend = new GAGCore::MemoryStreamBackend(even.data(), even.size());
        inputBackend->seekFromStart(0);
        GAGCore::BinaryInputStream input(inputBackend);
        CHECK_THROWS(loadEntityRandom(&input, random));
    }
    TEST_CASE("missing malformed and even text RNG fields are rejected")
    {
        EntityRandom random;
        random.seed(42, 54);
        const auto before = random;
        for (const char* field : {"stateHigh", "stateLow", "incrementHigh", "incrementLow"}) {
            for (const char* invalid : {"missing", "-1", "4294967296", "garbage", "12x"}) {
                std::string text = "entityRandom { ";
                for (const char* name : {"stateHigh", "stateLow", "incrementHigh", "incrementLow"}) {
                    if (std::string(name) == field && std::string(invalid) == "missing") continue;
                    text += std::string(name) + " = " + (std::string(name) == field ? invalid : "1") + "; ";
                }
                text += "}";
                auto backend = std::make_unique<GAGCore::MemoryStreamBackend>(text.data(), text.size());
                backend->seekFromStart(0);
                GAGCore::TextInputStream input(backend.get());
                CHECK_THROWS(loadEntityRandom(&input, random));
                CHECK(random == before);
            }
        }
        const std::string text = "entityRandom { stateHigh = 1; stateLow = 1; incrementHigh = 1; incrementLow = 2; }";
        auto backend = std::make_unique<GAGCore::MemoryStreamBackend>(text.data(), text.size());
        backend->seekFromStart(0);
        GAGCore::TextInputStream input(backend.get());
        CHECK_THROWS(loadEntityRandom(&input, random));
        CHECK(random == before);
    }

}
