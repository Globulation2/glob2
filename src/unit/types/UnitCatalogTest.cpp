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
#include "FileManager.h"
using Json = nlohmann::json;
namespace {
// The released pre-73 layout places flat Race records inside BaseTeam and
// has no second Race record in Team. Write that layout directly so these
// fixtures exercise both actual Team binding points, rather than patching a
// current header while retaining its newer record representation.
void writePre73Team(GAGCore::OutputStream& out, int version, const Unit& cached,
                   const std::vector<std::array<UnitType,NB_UNIT_LEVELS>>& tables)
{
    out.writeEnterSection("BaseTeam");
    out.writeUint32(BaseTeam::T_HUMAN,"type");
    out.writeSint32(0,"teamNumber"); out.writeSint32(1,"numberOfPlayer");
    const Uint8 color=255;
    for(const char* key:{"colorR","colorG","colorB","colorPAD"}) out.write(&color,1,key);
    out.writeUint32(1,"playersMask");
    for(const auto& table:tables) for(auto level:table) level.save(&out);
    out.writeSint32(700,"hungryness");
    out.writeLeaveSection();
    out.writeEnterSection("Team");
    out.writeEnterSection("myUnits");
    for(int slot=0;slot<Unit::MAX_COUNT;++slot) {
        out.writeEnterSection(slot); out.writeUint32(slot==0,"isUsed");
        if(slot==0) {
            out.writeEnterSection("Unit");
            out.writeSint32(WORKER,"typeNum"); out.writeText("worker","skinName");
            out.writeUint16(cached.gid,"gid"); out.writeSint32(0,"isDead");
            for(const auto& [key,value]:std::initializer_list<std::pair<const char*,int>>{
                {"posX",cached.posX},{"posY",cached.posY},{"delta",cached.delta},
                {"dx",cached.dx},{"dy",cached.dy},{"direction",cached.direction},
                {"insideTimeout",cached.insideTimeout},{"speed",cached.speed}}) out.writeSint32(value,key);
            out.writeUint32(1,"needToRecheckMedical"); out.writeUint32(Unit::MED_FREE,"medical");
            out.writeUint32(Unit::ACT_RANDOM,"activity"); out.writeUint32(Unit::DIS_RANDOM,"displacement");
            out.writeUint32(Unit::MOV_RANDOM_GROUND,"movement"); out.writeUint32(WALK,"action");
            out.writeSint32(0,"targetX"); out.writeSint32(0,"targetY"); out.writeSint32(0,"validTarget");
            out.writeSint32(0,"magicActionTimeout");
            if(version>=FILE_FORMAT_VERSION_UNDER_ATTACK_TIMER) out.writeUint8(0,"underAttackTimer");
            out.writeSint32(177,"hp"); out.writeSint32(37,"trigHP");
            out.writeSint32(123456,"hungry"); out.writeSint32(611,"hungryness");
            out.writeSint32(31000,"trigHungry"); out.writeUint32(0,"fruitMask"); out.writeUint32(0,"fruitCount");
            out.writeEnterSection("abilities");
            for(int ability=0;ability<NB_ABILITY;++ability) {
                out.writeEnterSection(ability);
                out.writeSint32(ability==WALK?17:cached.performance[ability],"performance");
                out.writeSint32(cached.level[ability],"level"); out.writeUint32(cached.canLearn[ability],"canLearn");
                out.writeLeaveSection();
            }
            out.writeLeaveSection();
            out.writeSint32(0,"experience"); out.writeSint32(0,"experienceLevel");
            out.writeSint32(-1,"destinationPurpose"); out.writeSint32(-1,"carriedRessource");
            out.writeSint32(27,"jobTimer"); out.writeLeaveSection();
        }
        out.writeLeaveSection();
    }
    out.writeLeaveSection();
    out.writeEnterSection("myBuildings");
    for(int slot=0;slot<Building::MAX_COUNT;++slot) {
        out.writeEnterSection(slot); out.writeUint32(0,"isUsed"); out.writeLeaveSection();
    }
    out.writeLeaveSection();
    out.writeEnterSection("myUnits"); out.writeEnterSection(0); out.writeEnterSection("Unit");
    for(const char* key:{"attachedBuilding","targetBuilding","ownExchangeBuilding"}) out.writeUint16(NOGBID,key);
    out.writeLeaveSection(); out.writeLeaveSection(); out.writeLeaveSection();
    out.writeEnterSection("myBuildings"); out.writeLeaveSection();
    for(const char* key:{"allies","enemies","sharedVisionExchange","sharedVisionFood","sharedVisionOther","me"})
        out.writeUint32(std::string_view(key)=="enemies"?0:1,key);
    for(const char* key:{"startPosX","startPosY","startPosSet","unitConversionLost","unitConversionGained"}) out.writeSint32(0,key);
    out.writeEnterSection("teamRessources");
    for(unsigned material=0;material<MaterialSlotCount;++material) {
        out.writeEnterSection(material); out.writeUint32(0,"teamRessources"); out.writeLeaveSection();
    }
    out.writeLeaveSection();
    out.writeEnterSection("TeamStats"); out.writeUint32(0,"size"); out.writeLeaveSection();
    out.writeLeaveSection();
}
}
TEST_SUITE("UnitCatalog")
{
    TEST_CASE("pre73 team race tables survive cached units training production and current resaves [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        for(int version:{58,72}) for(bool text:{false,true}) {
            CAPTURE(version); CAPTURE(text);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=731});
            // A pre-existing authoring catalog may have more than three types;
            // it must not change the historical Race record's width or policies.
            world.game.gameHeader.setUnitCatalog(UnitCatalog::fromJson(
                R"({"schemaVersion":1,"units":[{"key":"fixture:pre73-extra","extends":"worker"}]})"));
            world.game.configureBuildingCatalog();
            REQUIRE(world.team->race.unitTypeCount()==4);
            auto* source=world.addUnit(WORKER,20,20); REQUIRE(source);
            std::vector<std::array<UnitType,NB_UNIT_LEVELS>> tables;
            for(unsigned type=0;type<BuiltinUnitCount;++type) tables.push_back(UnitCatalog::legacy()->levels(type));
            tables[WORKER][0].performance[WALK]=23;
            tables[WORKER][0].performance[HP]=333;
            tables[WORKER][1].performance[BUILD]=19;
            tables[WORKER][1].performance[HARVEST]=13;
            if(text) {
                // Historical Race text records reuse flat keys for all twelve
                // tables. TextInputStream retains the last value of each key,
                // including the global hunger value. Use a representable
                // uniform modified table here; binary58/72 above retain the
                // distinct type and level tables of the original wire format.
                auto uniform=tables[WORKER][0];
                uniform.performance[BUILD]=19; uniform.performance[HARVEST]=13;
                uniform.hungriness=700;
                for(auto& table:tables) table.fill(uniform);
            }
            const auto expected=UnitCatalog::legacyMigration()->withLegacyLevels(tables,700);
            auto* written=new GAGCore::MemoryStreamBackend;
            std::string bytes;
            if(text) {
                GAGCore::TextOutputStream output(written); writePre73Team(output,version,*source,tables); output.flush(); bytes=written->takeContents();
            } else {
                GAGCore::BinaryOutputStream output(written); writePre73Team(output,version,*source,tables); bytes=written->takeContents();
            }
            if(text) {
                GAGCore::MemoryStreamBackend backend(bytes.data(),bytes.size()); backend.seekFromStart(0);
                GAGCore::TextInputStream input(&backend); REQUIRE(world.team->load(&input,&world.game.buildingsTypes,version));
            } else {
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
                REQUIRE(world.team->load(&input,&world.game.buildingsTypes,version)); REQUIRE(input.getPosition()==bytes.size());
            }
            REQUIRE(world.team->race.getCatalog()->serialize()==expected->serialize());
            auto* loaded=world.team->myUnits[0]; REQUIRE(loaded);
            REQUIRE(loaded->runtimeTraits().hungerRate==700);
            CHECK(loaded->performance[WALK]==17); CHECK(loaded->performance[HP]==200);
            CHECK(loaded->hp==177); CHECK(loaded->hungriness==611); CHECK(loaded->trigHP==37); CHECK(loaded->trigHungry==31000);
            // Model Game's final-team adoption after the actual old Team load.
            // This must not re-create effective fields loaded from the old unit.
            world.game.gameHeader.setUnitCatalog(world.team->race.getCatalog());
            world.game.configureBuildingCatalog();
            CHECK(loaded->performance[WALK]==17); CHECK(loaded->hungriness==611);
            loaded->setWorkerLevel(1);
            CHECK(loaded->performance[BUILD]==19); CHECK(loaded->performance[HARVEST]==13);
            auto* producer=world.addBuilding("swarm",8,8); REQUIRE(producer);
            world.team->addToStaticAbilitiesLists(producer);
            producer->ratio[WORKER]=1; producer->ratio[EXPLORER]=0; producer->ratio[WARRIOR]=0;
            producer->productionTimeout=0;
            for(unsigned material=0;material<MaterialCount;++material)
                producer->materials[material]=producer->type->semantics.production.recipes[WORKER].cost[material];
            producer->swarmStep();
            auto* born=world.team->myUnits[1]; REQUIRE(born);
            CHECK(born->performance[WALK]==23); CHECK(born->performance[HP]==333); CHECK(born->hungriness==700);
            const auto vector=[&](Game& game) {
                std::vector<Uint32> fields,buildings,units; game.checkSum(&fields,&buildings,&units,true);
                fields.insert(fields.end(),buildings.begin(),buildings.end()); fields.insert(fields.end(),units.begin(),units.end()); return fields;
            };
            auto* current=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(current); world.game.save(&output,false,"pre73 current continuation");
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(*current)); input.seekFromStart(0);
            GameGUI resumed; REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
            REQUIRE(resumed.game.unitCatalog().serialize()==expected->serialize());
            CHECK(vector(resumed.game)==vector(world.game));
            for(int tick=0;tick<64;++tick) {
                world.game.syncStep(0); resumed.game.syncStep(0);
                REQUIRE(vector(resumed.game)==vector(world.game)); REQUIRE(resumed.game.syncRandom==world.game.syncRandom);
            }
            CHECK(UnitCatalog::legacyMigration()->runtime(WORKER).hungerRate==425);
            CHECK(UnitCatalog::legacyMigration()->levels(WORKER)[0].performance[HP]==200);
        }
    }
    TEST_CASE("retained format64 FourSquares map keeps its historical warrior tables and current continuation [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        GameGUI world;
        GAGCore::BinaryInputStream historical(globals.globals.fileManager->openInflatingInputStreamBackend(
            (glob2test::sourceRoot()/"maps/FourSquares1.map.gz").string()));
        REQUIRE(world.game.load(&historical));
        REQUIRE(world.game.mapHeader.getVersionMinor()==64);
        REQUIRE(world.game.mapHeader.getNumberOfTeams()==4);
        // This shipped map was written before Race moved out of BaseTeam.
        // Frozen master loads attack8; installed current defaults use attack13.
        for(int team=0;team<4;++team) {
            const auto& race=world.game.teams[team]->race;
            REQUIRE(race.getCatalog()->levels(WARRIOR)[0].performance[ATTACK_STRENGTH]==8);
            CHECK(race.getCatalog()->serialize()==world.game.unitCatalog().serialize());
        }
        CHECK(UnitCatalog::availableDefaults()->levels(WARRIOR)[0].performance[ATTACK_STRENGTH]==13);
        world.game.setWaitingOnMask(0);
        const auto components=[](Game& game,bool normalizeHistoricalHeader) {
            std::vector<Uint32> state,buildings,units;
            game.checkSum(&state,&buildings,&units,true);
            // Serialize only the header through its current-version writer,
            // then checksum that canonical header. Keep team count and all
            // experiment fields in the comparison across the old->new resave.
            if(normalizeHistoricalHeader) {
                auto* bytes=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(bytes); game.mapHeader.save(&output);
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(*bytes)); input.seekFromStart(0);
                MapHeader canonical; REQUIRE(canonical.load(&input));
                state.front()=canonical.checkSum();
            }
            state.insert(state.end(),buildings.begin(),buildings.end());
            state.insert(state.end(),units.begin(),units.end()); return state;
        };
        const auto save=[](Game& game) {
            auto* bytes=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(bytes); game.save(&output,false,"retained format64 continuation");
            return bytes->takeContents();
        };
        const auto load=[](Game& game,const std::string& bytes) {
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0); return game.load(&input);
        };
        GameGUI resumed,repeated;
        const auto bytes=save(world.game); REQUIRE(load(resumed.game,bytes)); resumed.game.setWaitingOnMask(0);
        REQUIRE(resumed.game.unitCatalog().serialize()==world.game.unitCatalog().serialize());
        REQUIRE(components(resumed.game,true)==components(world.game,true));
        REQUIRE(load(repeated.game,save(resumed.game))); repeated.game.setWaitingOnMask(0);
        REQUIRE(components(repeated.game,false)==components(resumed.game,false));
        for(int tick=0;tick<64;++tick) {
            world.game.syncStep(0); resumed.game.syncStep(0); repeated.game.syncStep(0);
            REQUIRE(components(resumed.game,true)==components(world.game,true));
            REQUIRE(components(repeated.game,false)==components(resumed.game,false));
            REQUIRE(world.game.syncRandom==resumed.game.syncRandom);
            REQUIRE(repeated.game.syncRandom==resumed.game.syncRandom);
        }
    }
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
                GAGCore::TextInputStream input(&copy);
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
                GAGCore::TextInputStream input(&copy);
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
                    GAGCore::TextInputStream input(&copy);
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
