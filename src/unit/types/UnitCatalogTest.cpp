// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnitCatalog.h"
#include "GameHeader.h"
#include "EngineFixtures.h"
#include "Race.h"
#include <nlohmann/json.hpp>
#include "BinaryStream.h"
#include "TextStream.h"
#include "StreamBackend.h"
#include "FileFormatVersions.h"
#include "Team.h"
using Json = nlohmann::json;
TEST_SUITE("UnitCatalog")
{
    TEST_CASE("installed definitions reproduce immutable embedded defaults")
    {
        glob2test::HeadlessGlobals globals;
        auto catalog = UnitCatalog::loadFile((glob2test::sourceRoot() / "data/units/registry.json").string());
        auto defaults = UnitCatalog::builtins();
        REQUIRE(catalog->size() == BuiltinUnitCount);
        CHECK(catalog->serialize() == defaults->serialize());
        CHECK(catalog->serialize() == UnitCatalog::legacy()->serialize());
        for (unsigned i = 0; i < BuiltinUnitCount; ++i)
        {
            CHECK(catalog->find(catalog->definition(i).key) == i);
            CHECK(catalog->levels(i) == defaults->levels(i));
            CHECK(catalog->definition(i).cost[materialIndex(MaterialId::Food)] == 5);
        }
        CHECK(defaults->runtime(WORKER).has(UnitRuntimeTraits::Transport));
        CHECK(defaults->runtime(EXPLORER).has(UnitRuntimeTraits::ServiceRebound));
        CHECK(defaults->runtime(WARRIOR).has(UnitRuntimeTraits::Melee));
        CHECK(defaults->runtime(WORKER).has(UnitRuntimeTraits::CountsForSurvival));
        CHECK(defaults->runtime(WARRIOR).has(UnitRuntimeTraits::CountsForSurvival));
        CHECK_FALSE(defaults->runtime(EXPLORER).has(UnitRuntimeTraits::CountsForSurvival));
        for (unsigned id=0;id<BuiltinUnitCount;++id)
            CHECK_FALSE(defaults->runtime(id).has(UnitRuntimeTraits::ReleaseClearingClaims));
    }
    TEST_CASE("new combinations retain appearance and preserve other games")
    {
        glob2test::HeadlessGlobals globals;
        auto catalog = UnitCatalog::fromJson(
            R"({"schemaVersion":1,"units":[{"key":"fixture:armed-carrier","extends":"worker","mesh":"worker","behaviors":{"melee":true,"combatInterrupt":true,"cargoCapacity":8,"cargoKinds":3},"cost":{"food":7,"metal":2}}]})");
        REQUIRE(catalog->size() == 4);
        auto id = *catalog->find("fixture:armed-carrier");
        CHECK(catalog->runtime(id).has(UnitRuntimeTraits::Transport));
        CHECK(catalog->runtime(id).has(UnitRuntimeTraits::Melee));
        CHECK(catalog->runtime(id).has(UnitRuntimeTraits::ReleaseClearingClaims));
        CHECK(catalog->runtime(id).cargoCapacity == 8);
        CHECK(catalog->runtime(id).meshClass == WORKER);
        CHECK(UnitCatalog::builtins()->size() == 3);
        CHECK(UnitCatalog::deserialize(catalog->serialize())->serialize() == catalog->serialize());
        Race first, second;
        first.setCatalog(catalog);
        auto changed = Json::parse(catalog->serialize());
        changed["units"][WORKER]["levels"][0]["performance"][HP] = 400;
        first.setCatalog(UnitCatalog::deserialize(changed.dump()));
        CHECK(second.getUnitType(WORKER, 0)->performance[HP] == 200);
        CHECK(catalog->levels(WORKER)[0].performance[HP] == 200);
        CHECK(first.unitTypeCount() == 4);
    }
    TEST_CASE("embedded experiments contribute allowed keys without global registration")
    {
        glob2test::HeadlessGlobals globals;
        auto catalog = UnitCatalog::fromJson(
            R"({"schemaVersion":1,"experiments":[{"key":"fixture-unit","label":"Fixture unit","help":"Tests unit gating"}],"units":[{"key":"fixture:gated","extends":"warrior","requiredExperiment":"fixture-unit"}]})");
        GameHeader header;
        header.setUnitCatalog(catalog);
        auto keys = header.catalogExperimentKeys();
        CHECK(std::find(keys.begin(), keys.end(), "fixture-unit") != keys.end());
        CHECK(catalog->definition(3).requiredExperiment == "fixture-unit");
        CHECK(UnitCatalog::builtins()->experiments().empty());
    }
    TEST_CASE("counted race tables survive both text and binary saves")
    {
        glob2test::HeadlessGlobals globals;
        Race authored;
        auto changed = Json::parse(authored.getCatalog()->serialize());
        changed["units"][WORKER]["levels"][0]["performance"][HP] = 321;
        changed["units"][WARRIOR]["levels"][2]["performance"][HP] = 543;
        authored.setCatalog(UnitCatalog::deserialize(changed.dump()));
        const auto check = [&](bool text)
        {
            GAGCore::MemoryStreamBackend written;
            if (text)
            {
                auto* owned = new GAGCore::MemoryStreamBackend;
                GAGCore::TextOutputStream output(owned);
                authored.save(&output);
                written = *owned;
            }
            else
            {
                auto* owned = new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(owned);
                authored.save(&output);
                written = *owned;
            }
            GAGCore::MemoryStreamBackend copy(written);
            copy.seekFromStart(0);
            Race restored;
            restored.setCatalog(authored.getCatalog());
            const auto authoritative=restored.getCatalog();
            if (text)
            {
                GAGCore::TextInputStream input(new GAGCore::MemoryStreamBackend(copy));
                REQUIRE(restored.load(&input, FILE_FORMAT_VERSION_UNIT_CATALOG));
            }
            else
            {
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(copy));
                REQUIRE(restored.load(&input, FILE_FORMAT_VERSION_UNIT_CATALOG));
            }
            CHECK(restored.getCatalog() == authoritative);
            CHECK(restored.getCatalog()->serialize() == authored.getCatalog()->serialize());
            CHECK(UnitCatalog::legacy()->levels(WORKER)[0].performance[HP] == 200);
        };
        check(false);
        check(true);
    }
    TEST_CASE("current race records cannot override the embedded catalog")
    {
        glob2test::HeadlessGlobals globals;
        Race authored;
        auto changed=Json::parse(authored.getCatalog()->serialize());
        changed["units"][WORKER]["levels"][0]["performance"][HP]=321;
        authored.setCatalog(UnitCatalog::deserialize(changed.dump()));
        for(bool text:{false,true}) {
            GAGCore::MemoryStreamBackend written;
            if(text) {
                auto* owned=new GAGCore::MemoryStreamBackend;
                GAGCore::TextOutputStream output(owned); authored.save(&output); written=*owned;
            } else {
                auto* owned=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(owned); authored.save(&output); written=*owned;
            }
            GAGCore::MemoryStreamBackend copy(written); copy.seekFromStart(0);
            Race restored; const auto authoritative=restored.getCatalog();
            if(text) {
                GAGCore::TextInputStream input(new GAGCore::MemoryStreamBackend(copy));
                CHECK_THROWS(restored.load(&input,FILE_FORMAT_VERSION_UNIT_CATALOG));
            } else {
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(copy));
                CHECK_THROWS(restored.load(&input,FILE_FORMAT_VERSION_UNIT_CATALOG));
            }
            CHECK(restored.getCatalog()==authoritative);
            CHECK(restored.getUnitType(WORKER,0)->performance[HP]==200);
        }
    }
    TEST_CASE("both header forms retain embedded unit properties and experiment gates")
    {
        glob2test::HeadlessGlobals globals;
        GameHeader authored;
        authored.setUnitCatalog(UnitCatalog::fromJson(
            R"({"schemaVersion":1,"experiments":[{"key":"header-unit","label":"Header unit","help":"Tests header persistence"}],"units":[{"key":"fixture:header","extends":"worker","requiredExperiment":"header-unit","behaviors":{"hungerRate":0,"cargoCapacity":9,"cargoKinds":3}}]})"));
        authored.getExperiments().set("header-unit", true, authored.catalogExperimentKeys());
        for (bool text : {false, true})
            for (bool players : {false, true})
            {
                GAGCore::MemoryStreamBackend written;
                const auto save = [&](auto &output)
                {
                    if (players)
                        authored.save(&output);
                    else
                        authored.saveWithoutPlayerInfo(&output);
                };
                if (text)
                {
                    auto* owned = new GAGCore::MemoryStreamBackend;
                    GAGCore::TextOutputStream output(owned);
                    save(output);
                    written = *owned;
                }
                else
                {
                    auto* owned = new GAGCore::MemoryStreamBackend;
                    GAGCore::BinaryOutputStream output(owned);
                    save(output);
                    written = *owned;
                }
                GAGCore::MemoryStreamBackend copy(written);
                copy.seekFromStart(0);
                GameHeader restored;
                const auto load = [&](auto &input)
                {
                    return players ? restored.load(&input, FILE_FORMAT_VERSION_UNIT_CATALOG)
                                   : restored.loadWithoutPlayerInfo(&input, FILE_FORMAT_VERSION_UNIT_CATALOG);
                };
                if (text)
                {
                    GAGCore::TextInputStream input(new GAGCore::MemoryStreamBackend(copy));
                    REQUIRE(load(input));
                }
                else
                {
                    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(copy));
                    REQUIRE(load(input));
                }
                CHECK(restored.getUnitCatalogSnapshot() == authored.getUnitCatalogSnapshot());
                CHECK(restored.getExperiments().has("header-unit"));
            }
    }
    TEST_CASE("converted legacy flight tables retain their authoritative exception")
    {
        glob2test::HeadlessGlobals globals;
        auto old = UnitCatalog::legacy();
        std::vector<std::array<UnitType, NB_UNIT_LEVELS>> tables;
        for (unsigned type = 0; type < old->size(); ++type)
            tables.push_back(old->levels(type));
        tables[EXPLORER][0].performance[FLY] = 0;
        tables[EXPLORER][0].performance[WALK] = 16;
        auto migrated = old->withLegacyLevels(tables, 425);
        auto restored = UnitCatalog::deserialize(migrated->serialize());
        CHECK(restored->levels(EXPLORER) == tables[EXPLORER]);
        CHECK_THROWS(UnitCatalog::fromJson(migrated->serialize()));
        CHECK(UnitCatalog::legacy()->levels(EXPLORER)[0].performance[FLY] == 28);
    }
    TEST_CASE("legacy performance policies preserve tables and remain migration only")
    {
        glob2test::HeadlessGlobals globals;
        auto old = UnitCatalog::legacy();
        std::vector<std::array<UnitType, NB_UNIT_LEVELS>> tables;
        for (unsigned type = 0; type < old->size(); ++type)
            tables.push_back(old->levels(type));
        tables[WORKER][0].performance[ATTACK_SPEED] = 12;
        tables[WORKER][0].performance[ATTACK_STRENGTH] = 13;
        tables[EXPLORER][0].performance[FLY] = 0;
        auto migrated = old->withLegacyLevels(tables, 425);
        auto restored = UnitCatalog::deserialize(migrated->serialize());
        for (unsigned type = 0; type < old->size(); ++type)
        {
            CHECK(restored->levels(type) == tables[type]);
            CHECK(restored->runtime(type).hungerRate == 425);
            CHECK(restored->runtime(type).has(UnitRuntimeTraits::LegacyPerformancePolicies));
            CHECK(!old->runtime(type).has(UnitRuntimeTraits::LegacyPerformancePolicies));
        }
        CHECK(restored->runtime(WORKER).has(UnitRuntimeTraits::Melee));
        CHECK_THROWS(UnitCatalog::fromJson(migrated->serialize()));
        auto invalid = Json::parse(migrated->serialize());
        invalid["legacyPerformancePolicies"] = 1;
        CHECK_THROWS(UnitCatalog::deserialize(invalid.dump()));
    }
    TEST_CASE("legacy movement exception cannot relax additional definitions")
    {
        glob2test::HeadlessGlobals globals;
        auto extended = UnitCatalog::fromJson(
            R"({"schemaVersion":1,"units":[{"key":"fixture:legacy-adjacent","extends":"explorer"}]})");
        auto snapshot = Json::parse(extended->serialize());
        snapshot["legacyLevelMovement"] = true;
        snapshot["legacyPerformancePolicies"] = true;
        auto restored = UnitCatalog::deserialize(snapshot.dump());
        CHECK(!restored->runtime(3).has(UnitRuntimeTraits::LegacyPerformancePolicies));
        snapshot["units"][3]["levels"][0]["performance"][FLY] = 0;
        CHECK_THROWS(UnitCatalog::deserialize(snapshot.dump()));
    }
    TEST_CASE("pre-race legacy headers receive cached-performance policies")
    {
        glob2test::HeadlessGlobals globals;
        for (int version : {58, FILE_FORMAT_VERSION_RACE_FIELD - 1})
        {
            // Build the historical header representation directly so current
            // writers cannot accidentally supply the new migration marker.
            GAGCore::MemoryStreamBackend written;
            {
                auto* owned = new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(owned);
                output.writeSint32(0, "gameLatency");
                output.writeUint8(1, "orderRate");
                if (version >= FILE_FORMAT_VERSION_ALLIES_AND_WIN_CONDITIONS)
                {
                    for (int team = 0; team < Team::MAX_COUNT_ON_DISK; ++team)
                        output.writeUint8(0, "allyTeamNumber");
                    output.writeUint8(0, "allyTeamsFixed");
                    output.writeUint32(0, "winningConditionsCount");
                }
                if (version >= FILE_FORMAT_VERSION_UNIFIED_SEED)
                    output.writeUint32(123, "seed");
                if (version >= FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
                    output.writeUint8(0, "mapDiscovered");
                written = *owned;
            }
            GAGCore::MemoryStreamBackend copy(written);
            copy.seekFromStart(0);
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(copy));
            GameHeader restored;
            REQUIRE(restored.loadWithoutPlayerInfo(&input, version));
            for (unsigned type = 0; type < BuiltinUnitCount; ++type)
            {
                CHECK(restored.getUnitCatalog()->runtime(type).has(UnitRuntimeTraits::LegacyPerformancePolicies));
                CHECK(restored.getUnitCatalog()->levels(type) == UnitCatalog::legacy()->levels(type));
                CHECK(restored.getUnitCatalog()->runtime(type).hungerRate == 425);
            }
        }
    }
    TEST_CASE("current header forms reject omitted embedded definitions")
    {
        glob2test::HeadlessGlobals globals;
        GameHeader authored;
        const auto snapshot = authored.getUnitCatalogSnapshot();
        REQUIRE(snapshot.size() < 256 * 1024);
        for (bool players : {false, true})
        {
            GAGCore::MemoryStreamBackend written;
            {
                auto* owned = new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(owned);
                if (players) authored.save(&output);
                else authored.saveWithoutPlayerInfo(&output);
                written = *owned;
            }
            const std::string bytes(written.getBuffer(), written.getPosition());
            const auto offset = bytes.find(snapshot);
            REQUIRE(offset != std::string::npos);
            REQUIRE(offset >= 8);
            // One chunk encodes count/length/payload. Keep all later fields
            // intact while replacing it with an empty chunk list.
            const std::string malformed = bytes.substr(0, offset - 8) + std::string(4, '\0') +
                                          bytes.substr(offset + snapshot.size());
            GAGCore::MemoryStreamBackend copy(malformed.data(), malformed.size());
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(copy));
            GameHeader restored;
            if (players) CHECK_THROWS(restored.load(&input, FILE_FORMAT_VERSION_UNIT_CATALOG));
            else CHECK_THROWS(restored.loadWithoutPlayerInfo(&input, FILE_FORMAT_VERSION_UNIT_CATALOG));
        }
    }
    TEST_CASE("malformed definitions fail before publication")
    {
        glob2test::HeadlessGlobals globals;
        auto defaults = UnitCatalog::builtins();
        auto check = [&](Json entry)
        {
            CHECK_THROWS(
                UnitCatalog::fromJson(Json{{"schemaVersion", 1}, {"units", Json::array({entry})}}.dump()));
        };
        check({{"key", "worker"}, {"behaviors", {{"foodCapacity", 0}}}});
        check({{"key", "worker"}, {"behaviors", {{"hungerRate", -1}}}});
        check({{"key", "worker"}, {"behaviors", {{"hungerTriggerDenominator", 0}}}});
        check({{"key", "worker"}, {"behaviors", {{"cargoKinds", 13}, {"cargoCapacity", 20}}}});
        check({{"key", "fixture:x"}, {"extends", "missing"}});
        check({{"key", "fixture:x"}, {"extends", "worker"}, {"requiredExperiment", "missing"}});
        auto snapshot = Json::parse(defaults->serialize());
        snapshot["units"][0]["levels"][1]["performance"][FLY] = 20;
        snapshot["units"][0]["behaviors"]["fly"] = true;
        CHECK_THROWS(UnitCatalog::deserialize(snapshot.dump()));
        snapshot = Json::parse(defaults->serialize());
        snapshot["units"].erase(snapshot["units"].begin());
        CHECK_THROWS(UnitCatalog::deserialize(snapshot.dump()));
        snapshot = Json::parse(defaults->serialize());
        snapshot["units"][0]["behaviors"].erase("starvationDamage");
        CHECK_THROWS(UnitCatalog::deserialize(snapshot.dump()));
        snapshot = Json::parse(defaults->serialize());
        snapshot["units"][0]["levels"][0].erase("performance");
        CHECK_THROWS(UnitCatalog::deserialize(snapshot.dump()));
        CHECK(defaults->size() == 3);
    }
}
