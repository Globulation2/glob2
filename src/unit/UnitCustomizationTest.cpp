// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "UnitCatalog.h"
#include "gradient/GradientRuntime.h"
#include "FileFormatVersions.h"
#include "ai/maxima/AIMaximaWorldHelpers.h"
#include "UnitTiming.h"
#include "Bullet.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <limits>
#include <sstream>
#include <climits>
#include <cstddef>

namespace {
Uint32 trace(Game& game)
{
    std::vector<Uint32> gameFields,buildings,units;
    game.checkSum(&gameFields,&buildings,&units,true);
    if (!gameFields.empty()) gameFields.erase(gameFields.begin()); // format of the saved map header
    Uint32 result=2166136261u;
    for (const auto& values:{gameFields,buildings,units}) for (Uint32 value:values) result=(result^value)*16777619u;
    return result;
}
void deposit(Map& map,int x,int y,int type,int amount)
{
    map.setResourceByIndex(x,y,type,0);
    REQUIRE(map.getResource(x,y).type==type);
    map.setResourceAmount(map.coordToIndex(x,y),amount);
    REQUIRE(map.getResource(x,y).amount==amount);
}
void configure(glob2test::HeadlessGame& world)
{
    auto catalog=UnitCatalog::loadFile(glob2test::fixture("unit-catalog/combinations.json").string());
    world.game.gameHeader.setUnitCatalog(catalog);
    world.game.configureBuildingCatalog();
    for (int t=0;t<world.game.teamsCount();++t) world.game.teams[t]->race.setCatalog(catalog);
}

// Compare the complete checksum vectors, serialized cached unit state, ordered
// relationships and reservations. A digest alone would hide which continuation
// contract failed, and catalog-derived values must not replace saved caches.
using ContinuationAudit = std::pair<std::vector<Uint32>,std::vector<std::string>>;
ContinuationAudit continuationAudit(Game& game)
{
    ContinuationAudit result;
    std::vector<Uint32> buildings,units;
    game.checkSum(&result.first,&buildings,&units,true);
    result.first.insert(result.first.end(),buildings.begin(),buildings.end());
    result.first.insert(result.first.end(),units.begin(),units.end());
    const auto appendList=[&](const auto& list) {
        result.first.push_back(Uint32(list.size()));
        for(const auto* entry:list) result.first.push_back(entry->gid);
    };
    for(int t=0;t<game.teamsCount();++t) {
        const auto* team=game.teams[t];
        appendList(team->canFeedUnit); appendList(team->canHealUnit);
        for(const auto& list:team->canUpgrade) appendList(list);
        for(unsigned material=0;material<MaterialCount;++material)
            result.first.push_back(team->reservedTeamMaterials[material]);
        for(int id=0;id<Building::MAX_COUNT;++id) if(const auto* building=team->myBuildings[id]) {
            appendList(building->unitsWorking); appendList(building->unitsInside);
            for(unsigned material=0;material<MaterialCount;++material) {
                result.first.push_back(building->materials[material]);
                result.first.push_back(building->reservedMaterials[material]);
            }
        }
        for(int id=0;id<Unit::MAX_COUNT;++id) if(auto* unit=team->myUnits[id]) {
            auto* storage=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(storage); unit->save(&output);
            result.second.emplace_back(storage->getBuffer(),storage->getPosition());
            result.first.push_back(unit->capabilityFlags);
            result.first.push_back(unit->configuredFoodCapacity);
            result.first.push_back(unit->trigHungryCarrying);
        }
    }
    return result;
}
void checkPhaseContinuation(Game& game,int ticks)
{
    const auto before=continuationAudit(game);
    const auto random=game.syncRandom;
    auto* storage=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);
    game.save(&output,false,"custom unit phase continuation");
    const std::string bytes(storage->getBuffer(),storage->getPosition());
    GameGUI resumed;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0); REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
    REQUIRE(resumed.game.unitCatalog().digest()==game.unitCatalog().digest());
    REQUIRE(continuationAudit(resumed.game)==before);
    REQUIRE(resumed.game.syncRandom==random);
    REQUIRE(resumed.game.unitCargo.entries()==game.unitCargo.entries());
    for(int tick=0;tick<ticks;++tick) {
        CAPTURE(tick);
        game.syncStep(0); resumed.game.syncStep(0);
        REQUIRE(continuationAudit(resumed.game)==continuationAudit(game));
        REQUIRE(resumed.game.syncRandom==game.syncRandom);
        REQUIRE(resumed.game.unitCargo.entries()==game.unitCargo.entries());
    }
}
}

TEST_SUITE("UnitCustomization")
{
    TEST_CASE("hybrid flag recruitment policies are independent of abilities and idle area policies [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        constexpr const char *policyNames[] = {"recruitClear", "recruitExplore", "recruitDefend"};
        for (unsigned role = 0; role < 3; ++role) for (bool allowed : {false, true})
            for (bool explicitSelection : {false, true}) {
            CAPTURE(role); CAPTURE(allowed); CAPTURE(explicitSelection);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
            auto definitions = nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto hybrid = definitions["units"][WORKER]; hybrid["key"] = "fixture:recruitment-hybrid";
            auto &traits = hybrid["behaviors"];
            traits["melee"] = true; traits["explore"] = true;
            // These idle policies concern painted areas/fog, not flag work.
            traits["guardIdle"] = false; traits["clearIdle"] = false; traits["exploreIdle"] = false;
            for (unsigned candidate = 0; candidate < 3; ++candidate)
                traits[policyNames[candidate]] = allowed && candidate == role;
            for (unsigned level = 0; level < NB_UNIT_LEVELS; ++level) {
                hybrid["levels"][level]["performance"][ATTACK_SPEED] = definitions["units"][WARRIOR]["levels"][level]["performance"][ATTACK_SPEED];
                hybrid["levels"][level]["performance"][ATTACK_STRENGTH] = definitions["units"][WARRIOR]["levels"][level]["performance"][ATTACK_STRENGTH];
            }
            definitions["units"].push_back(hybrid);
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
            auto buildings = nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            const auto innType = world.game.buildingsTypes.getFinishedTypeNum("inn");
            buildings["variants"][innType]["properties"]["zonable"] = {1,1,1};
            if (explicitSelection)
                for (const char *job : {"clear", "explore", "defend"})
                    buildings["variants"][innType]["semantics"]["attractionUnits"][job] = {"fixture:recruitment-hybrid"};
            world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
            world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
            auto *flag = world.addBuilding("inn",10,8);
            auto *unit = world.addUnit(3,6,8);
            REQUIRE(flag); REQUIRE(unit);
            flag->unitStayRange = 6; flag->dirtyGradients();
            deposit(world.game.map,15,9,WOOD,3);
            CHECK(unit->hasCapability(UnitRuntimeTraits::Clear)); CHECK(unit->hasCapability(UnitRuntimeTraits::Melee));
            CHECK(unit->hasCapability(UnitRuntimeTraits::Explore));
            CHECK(unit->runtimeTraits().recruits(role) == allowed);
            // A refusal changes hiring, not the building's role or ability
            // to host a job that was already assigned.
            CHECK(flag->canUnitWorkHere(unit,true,int(role)));
            CHECK(flag->runtime->attractsRole(role));
            CHECK(flag->runtime->interaction(unit->typeNum).recruits(role) == allowed);
            unit->handleDisplacement(); CHECK(unit->displacement == Unit::DIS_RANDOM);
            flag->maxUnitWorking = flag->desiredMaxUnitWorking = 1;
            flag->subscriptionWorkingTimer = 32;
            CHECK(flag->subscribeForFlagingStep() == allowed);
            if (allowed) {
                CHECK(unit->attachedBuilding == flag); CHECK(unit->activity == Unit::ACT_FLAG);
                CHECK(unit->jobPurpose == static_cast<UnitJobPurpose>(int(UnitJobPurpose::Clear)+role));
            } else {
                CHECK(unit->attachedBuilding == nullptr); CHECK(unit->activity == Unit::ACT_RANDOM);
                CHECK(flag->unitsWorking.empty());
            }
            checkPhaseContinuation(world.game,64);
        }
    }

    TEST_CASE("flag recruitment refusal does not disable painted clearing or defense areas")
    {
        glob2test::HeadlessGlobals globals;
        for (bool clearing : {false,true}) {
            CAPTURE(clearing);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
            const auto catalog = UnitCatalog::fromJson(clearing
                ? R"({"schemaVersion":1,"units":[{"key":"worker","behaviors":{"recruitClear":false}}]})"
                : R"({"schemaVersion":1,"units":[{"key":"warrior","behaviors":{"recruitDefend":false}}]})");
            world.game.gameHeader.setUnitCatalog(catalog); world.game.configureBuildingCatalog();
            world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
            auto *unit = world.addUnit(clearing ? WORKER : WARRIOR,6,8); REQUIRE(unit);
            CHECK_FALSE(unit->runtimeTraits().recruits(clearing ? 0 : 2));
            if (clearing) {
                deposit(world.game.map,8,8,WOOD,2);
                world.game.map.addClearArea(8,8,world.team->teamNumber);
                for (int tick=0;tick<2048 && world.game.map.getResource(8,8).amount;++tick) world.game.syncStep(0);
                CHECK(world.game.map.getResource(8,8).amount == 0);
                CHECK(world.team->stats.measurements.cleared[WOOD] > 0);
            } else {
                world.game.map.addGuardArea(9,8,world.team->teamNumber);
                unit->handleDisplacement(); CHECK(unit->displacement == Unit::DIS_ATTACKING_AROUND);
                for (int tick=0;tick<2048 && !world.game.map.isGuardArea(unit->posX,unit->posY,world.team->me);++tick)
                    world.game.syncStep(0);
                CHECK(world.game.map.isGuardArea(unit->posX,unit->posY,world.team->me));
            }
            CHECK(unit->attachedBuilding == nullptr);
            checkPhaseContinuation(world.game,64);
        }
    }

    TEST_CASE("disabling future flag recruitment preserves all existing job roles and memberships [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        constexpr const char *policyNames[] = {"recruitClear", "recruitExplore", "recruitDefend"};
        constexpr const char *flagNames[] = {"clearingflag", "explorationflag", "warflag"};
        constexpr int unitTypes[] = {WORKER, EXPLORER, WARRIOR};
        for (unsigned role=0;role<3;++role) {
            CAPTURE(role);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
            world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
            auto definitions = nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto *flag = world.addBuilding(flagNames[role],16,16);
            auto *unit = world.addUnit(unitTypes[role],10,16);
            REQUIRE(flag); REQUIRE(unit);
            world.team->addToStaticAbilitiesLists(flag);
            flag->unitStayRange=6; flag->dirtyGradients();
            deposit(world.game.map,20,16,WOOD,3);
            flag->maxUnitWorking=flag->desiredMaxUnitWorking=1; flag->subscriptionWorkingTimer=32;
            REQUIRE(flag->subscribeForFlagingStep());
            const auto purpose=static_cast<UnitJobPurpose>(int(UnitJobPurpose::Clear)+role);
            REQUIRE(unit->jobPurpose==purpose); REQUIRE(world.team->integrity());
            const auto clearingMembership=world.team->clearingFlags;
            const auto combatMembership=world.team->combatFlags;
            const auto semanticRoles=flag->runtime->attractionRoles;
            const auto buildingCatalog=world.game.buildingsTypes.snapshotJson();
            definitions["units"][unitTypes[role]]["behaviors"][policyNames[role]]=false;
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
            // Recompile interaction rows without replacing live descriptors.
            world.game.configureBuildingCatalog();
            CHECK_FALSE(unit->runtimeTraits().recruits(role));
            CHECK_FALSE(flag->runtime->interaction(unit->typeNum).recruits(role));
            CHECK(flag->runtime->attractionRoles==semanticRoles);
            CHECK(world.game.buildingsTypes.snapshotJson()==buildingCatalog);
            CHECK(world.team->clearingFlags==clearingMembership);
            CHECK(world.team->combatFlags==combatMembership);
            CHECK(unit->activity==Unit::ACT_FLAG); CHECK(unit->attachedBuilding==flag);
            CHECK(unit->jobPurpose==purpose); CHECK(flag->unitsWorking.size()==1);
            CHECK(flag->canUnitWorkHere(unit,true,int(role))); REQUIRE(world.team->integrity());
            auto *idle=world.addUnit(unitTypes[role],10,20); REQUIRE(idle);
            flag->maxUnitWorking=flag->desiredMaxUnitWorking=2; flag->subscriptionWorkingTimer=32;
            CHECK_FALSE(flag->subscribeForFlagingStep());
            CHECK(idle->activity==Unit::ACT_RANDOM); CHECK(idle->attachedBuilding==nullptr);
            REQUIRE(world.team->integrity());
            checkPhaseContinuation(world.game,64);
            const auto gid=flag->gid;
            flag->kill(); world.game.syncStep(0);
            CHECK(world.team->myBuildings[Building::GIDtoID(gid)]==nullptr);
            CHECK(std::find(world.team->clearingFlags.begin(),world.team->clearingFlags.end(),flag)==world.team->clearingFlags.end());
            CHECK(std::find(world.team->combatFlags.begin(),world.team->combatFlags.end(),flag)==world.team->combatFlags.end());
            CHECK(unit->attachedBuilding==nullptr); CHECK(idle->attachedBuilding==nullptr);
            REQUIRE(world.team->integrity());
            checkPhaseContinuation(world.game,64);
        }
    }


    TEST_CASE("stock capacity-one wide primary cargo survives unit and game continuation [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        for(Uint64 denominator:{1000001ull,1000000000039ull}) for(bool text:{false,true}) {
            CAPTURE(denominator); CAPTURE(text);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            world.game.gameHeader.setHungerDisabled(true);
            auto* unit=world.addUnit(WORKER,20,20); REQUIRE(unit);
            REQUIRE(unit->runtimeTraits().cargoCapacity==1);
            REQUIRE(unit->hasCapability(UnitRuntimeTraits::SpillRejectedCargo));
            REQUIRE_FALSE(unit->hasCapability(UnitRuntimeTraits::ExtendedCargo));
            unit->receiveCargoPacket(WOOD,{1,denominator});
            REQUIRE(unit->widePrimaryCargo); REQUIRE(unit->carriedPacketCount()==1);
            CHECK_FALSE(unit->canCarryMaterial(WOOD));
            const auto original=world.game.unitCargo.entries();
            world.team->stats.beginMeasurementSnapshot(world.team);
            world.team->stats.observeMeasurementUnit(unit);
            CHECK(world.team->stats.measurements.carried[WOOD]==1);
            const auto saveUnit=[&](Unit& value,bool textual) {
                auto* backend=new GAGCore::MemoryStreamBackend;
                std::string bytes;
                if(textual) { GAGCore::TextOutputStream output(backend); value.save(&output); output.flush(); bytes=backend->takeContents(); }
                else { GAGCore::BinaryOutputStream output(backend); value.save(&output); bytes=backend->takeContents(); }
                return bytes;
            };
            const auto bytes=saveUnit(*unit,text);
            glob2test::HeadlessGame restored({.clearImmobile=true,.header=true,.seed=4921});
            Unit* loaded=nullptr;
            if(text) {
                GAGCore::MemoryStreamBackend backend(bytes.data(),bytes.size()); backend.seekFromStart(0);
                GAGCore::TextInputStream input(&backend); loaded=new Unit(&input,restored.team,FILE_FORMAT_VERSION_UNIT_CATALOG);
            } else {
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
                loaded=new Unit(&input,restored.team,FILE_FORMAT_VERSION_UNIT_CATALOG);
                REQUIRE(input.getPosition()==bytes.size());
            }
            restored.team->myUnits[0]=loaded; restored.team->rebuildLiveLists();
            restored.game.map.setGroundUnit(loaded->posX,loaded->posY,loaded->gid);
            REQUIRE(loaded->widePrimaryCargo); CHECK(loaded->carriedPacketCount()==1);
            CHECK(saveUnit(*loaded,text)==bytes); CHECK(restored.game.unitCargo.entries()==original);
            restored.team->stats.beginMeasurementSnapshot(restored.team); restored.team->stats.observeMeasurementUnit(loaded);
            CHECK(restored.team->stats.measurements.carried[WOOD]==1);
            CHECK(restored.team->stats.measurements.materialSpillageEvents==0);
            checkPhaseContinuation(world.game,64);
            CHECK(world.game.unitCargo.entries()==original);
            CHECK(world.team->stats.measurements.materialSpillageEvents==0);
            // Admission of the primary alias must not admit a different first
            // material or a second packet into this capacity-one inventory.
            if(text) for(bool wrongMaterial:{false,true}) {
                CAPTURE(wrongMaterial);
                auto malformed=bytes;
                const std::string before=wrongMaterial?"material = "+std::to_string(WOOD)+";":"cargoOverflowCount = 1;";
                const std::string after=wrongMaterial?"material = "+std::to_string(STONE)+";":"cargoOverflowCount = 2;";
                const auto at=malformed.find(before); REQUIRE(at!=std::string::npos); malformed.replace(at,before.size(),after);
                glob2test::HeadlessGame invalid({.clearImmobile=true,.header=true,.seed=4921});
                GAGCore::MemoryStreamBackend backend(malformed.data(),malformed.size()); backend.seekFromStart(0);
                GAGCore::TextInputStream input(&backend);
                CHECK_THROWS_AS(Unit(&input,invalid.team,FILE_FORMAT_VERSION_UNIT_CATALOG),std::runtime_error);
                CHECK(invalid.game.unitCargo.empty());
            }
        }
    }

    TEST_CASE("catalog resizing rejects in-flight projectile rows without publishing state")
    {
        glob2test::HeadlessGlobals globals;
        for (bool shrink:{false,true}) {
            CAPTURE(shrink);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto extra=definitions["units"][WORKER]; extra["key"]="fixture:projectile-target";
            if (shrink) {
                definitions["units"].push_back(extra);
                world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
                world.game.configureBuildingCatalog();
            }
            REQUIRE(world.addUnit(WORKER,8,8));
            auto* bullet=new Bullet(0,0,0,0,128,7,20,20,0,0,1,1);
            bullet->unitDamage.resize(world.game.unitTypeCount()); bullet->unitDamage.fill(7);
            world.game.map.getSector(0)->bullets.push_back(bullet);
            const auto oldCatalog=world.team->race.getCatalog();
            const auto before=continuationAudit(world.game);
            const auto random=world.game.syncRandom;
            const auto availability=world.game.unitAvailability;
            const auto waterOnly=world.game.hasWaterOnlyUnits();
            const auto snapshot=world.team->stats.frozenDisplay();
            if (shrink) definitions["units"].erase(definitions["units"].end()-1);
            else definitions["units"].push_back(extra);
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
            CHECK_THROWS_WITH(world.game.configureBuildingCatalog(),
                "Unit catalog cannot resize while projectiles are in flight");
            CHECK(world.team->race.getCatalog()==oldCatalog);
            CHECK(world.game.unitAvailability==availability);
            CHECK(world.game.hasWaterOnlyUnits()==waterOnly);
            CHECK(world.team->stats.frozenDisplay()==snapshot);
            CHECK(world.game.syncRandom==random);
            world.game.gameHeader.setUnitCatalog(oldCatalog);
            CHECK(continuationAudit(world.game)==before);
            checkPhaseContinuation(world.game,32);
            // Loading replaces old map sectors after catalog binding. Existing
            // projectiles from the discarded game must not veto that binding.
            auto* memory=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(memory);
            world.game.save(&output,false,"projectile replacement load");
            const std::string bytes(memory->getBuffer(),memory->getPosition());
            const auto saved=continuationAudit(world.game);
            bullet->unitDamage.resize(shrink?3:4);
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0); REQUIRE(world.game.load(&input));
            CHECK(continuationAudit(world.game)==saved);
        }
    }

    TEST_CASE("setup scaling preserves lethal extremes and zero-kind inventories refuse packets")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
        definitions["units"][WORKER]["behaviors"]["foodCapacity"]=1;
        definitions["units"][WORKER]["behaviors"]["hungerRate"]=1;
        for(auto& level:definitions["units"][WORKER]["levels"]) level["performance"][HP]=1;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
        world.game.configureBuildingCatalog();
        auto* unit=world.addUnit(WORKER,8,8); REQUIRE(unit);
        unit->hp=INT_MIN; unit->hungry=INT_MIN;
        definitions["units"][WORKER]["behaviors"]["foodCapacity"]=1000000;
        for(auto& level:definitions["units"][WORKER]["levels"]) level["performance"][HP]=1000000;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
        world.game.configureBuildingCatalog();
        unit->rebindDefinitionForSetup();
        CHECK(unit->hp==INT_MIN); CHECK(unit->hungry==INT_MIN);
        auto restricted=definitions["units"][WORKER];
        restricted["key"]="fixture:zero-kind";
        restricted["behaviors"]["transport"]=false;
        restricted["behaviors"]["cargoCapacity"]=1;
        restricted["behaviors"]["cargoKinds"]=0;
        definitions["units"].push_back(restricted);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
        world.game.configureBuildingCatalog();
        auto* storage=world.addUnit(3,12,12); REQUIRE(storage);
        CHECK_FALSE(storage->canCarryMaterial(WOOD));
        CHECK_THROWS_AS(storage->receiveCargoPacket(WOOD,{1,1}),std::runtime_error);
        CHECK(storage->carriedPacketCount()==0);
        CHECK(world.game.unitCargo.empty());
    }

    TEST_CASE("current saves reject invalid cached bounds and retain pending lethal state")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto* unit=world.addUnit(WORKER,8,8); REQUIRE(unit);
        const UnitState original=*unit;
        const auto serialized=[&] {
            auto* memory=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(memory); unit->save(&output);
            return memory->takeContents();
        };
        for(int invalid=0;invalid<8;++invalid) {
            static_cast<UnitState&>(*unit)=original;
            switch(invalid) {
                case 0: unit->performance[WALK]=-1; break;
                case 1: unit->performance[HP]=0; break;
                case 2: unit->performance[ARMOR]=1000001; break;
                case 3: unit->fruitCount=4; break;
                case 4: unit->experience=-1; break;
                case 5: unit->experienceLevel=-1; break;
                case 6: unit->speed=-1; break;
                case 7: unit->delta=-1; break;
            }
            const auto bytes=serialized(); static_cast<UnitState&>(*unit)=original;
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
            CHECK_THROWS_AS(Unit(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG),std::runtime_error);
        }
        unit->hp=INT_MIN; unit->hungry=INT_MIN; unit->delta=INT_MAX; unit->speed=INT_MAX;
        unit->experienceLevel=INT_MAX;
        const auto bytes=serialized(); static_cast<UnitState&>(*unit)=original;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
        Unit restored(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG);
        CHECK(restored.hp==INT_MIN); CHECK(restored.hungry==INT_MIN);
        CHECK(restored.delta==INT_MAX); CHECK(restored.speed==INT_MAX); CHECK(restored.experienceLevel==INT_MAX);
    }

    TEST_CASE("custom combat extremes preserve observer estimates and clamp lethal overkill")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
        auto catalog=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto custom=catalog["units"][WARRIOR]; custom["key"]="fixture:numeric-fighter";
        custom["behaviors"]["healingSpeedQ8"]=65536;
        custom["behaviors"]["magicGround"]=true;
        for(auto& level:custom["levels"]) {
            level["performance"][HP]=1000000;
            level["performance"][ATTACK_SPEED]=255;
            level["performance"][ATTACK_STRENGTH]=1000000;
            level["performance"][MAGIC_ATTACK_GROUND]=1000000;
            level["performance"][ARMOR]=0;
            level["armorReductionPerHappyness"]=1000000;
        }
        catalog["units"].push_back(custom);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(catalog.dump()));
        world.game.configureBuildingCatalog(); world.game.gameHeader.setHungerDisabled(true);
        auto* attacker=world.addUnit(3,8,8); auto* victim=world.addUnit(3,9,8,1);
        REQUIRE(attacker); REQUIRE(victim);
        attacker->hp=500000;
        auto snapshot=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
        const auto* observed=snapshot->unitSlots(0)[Unit::GIDtoID(attacker->gid)]; REQUIRE(observed);
        CHECK(AIMaxima::WorldHelpers::warrior_power(*snapshot,observed)==127500000);
        attacker->experienceLevel=INT_MAX;
        snapshot=AIEngine::AIWorldView::capture(world.game,snapshot->catalog);
        observed=snapshot->unitSlots(0)[Unit::GIDtoID(attacker->gid)]; REQUIRE(observed);
        CHECK(AIEngine::ObservationQueries::realAttackStrength(*snapshot,*observed)==INT_MAX);
        CHECK(AIMaxima::WorldHelpers::warrior_power(*snapshot,observed)==INT_MAX);
        CHECK(attacker->getRealAttackStrength()==INT_MAX);
        victim->fruitCount=3;
        for(int repeat=0;repeat<2;++repeat) {
            attacker->action=ATTACK_SPEED; attacker->speed=12; attacker->delta=128;
            attacker->dx=1; attacker->dy=0;
            attacker->syncStep();
        }
        CHECK(victim->hp==INT_MIN);
        CHECK(victim->diagnosticDeathCause==GameplayMeasurements::COMBAT);
        CHECK(world.team->stats.measurements.damageDealt[GameplayMeasurements::MELEE][GameplayMeasurements::UNIT]==1000000);
        victim->delta=255; victim->syncStep(); CHECK(victim->isDead);
        auto* magicVictim=world.addUnit(3,10,8,1); REQUIRE(magicVictim);
        magicVictim->fruitCount=3;
        for(int repeat=0;repeat<2;++repeat) { attacker->magicActionTimeout=0; attacker->handleMagic(); }
        CHECK(magicVictim->hp==INT_MIN);
        CHECK(magicVictim->diagnosticDeathCause==GameplayMeasurements::COMBAT);
        CHECK(world.team->stats.measurements.damageDealt[GameplayMeasurements::MAGIC][GameplayMeasurements::UNIT]==1000000);
    }

    TEST_CASE("maximum healing multiplier and health complete a diagonal service visit")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto catalog=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto custom=catalog["units"][WORKER]; custom["key"]="fixture:numeric-patient";
        custom["behaviors"]["healingSpeedQ8"]=65536;
        for(auto& level:custom["levels"]) level["performance"][HP]=1000000;
        catalog["units"].push_back(custom);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(catalog.dump()));
        world.game.configureBuildingCatalog(); world.game.gameHeader.setHungerDisabled(true);
        auto* hospital=world.addBuilding("hospital",9,9); auto* patient=world.addUnit(3,8,8);
        REQUIRE(hospital); REQUIRE(patient);
        patient->hp=999999;
        patient->subscriptionSuccess(hospital,true,false);
        hospital->unitsInside.push_back(patient);
        patient->activity=Unit::ACT_UPGRADING; patient->destinationPurpose=HEAL;
        patient->displacement=Unit::DIS_ENTERING_BUILDING;
        patient->posX=9; patient->posY=9; patient->dx=1; patient->dy=1;
        patient->action=WALK;
        patient->handleDisplacement();
        CHECK(patient->speed==INT_MAX);
        CHECK(unitActionStepSpeed(patient->speed,WALK,1,1,true)==UNIT_DELTA_QUANTUM);
        const int duration=hospital->type->semantics.healing.duration;
        for(int tick=0;tick<=duration+2;++tick) patient->syncStep();
        CHECK(patient->hp==1000000);
        CHECK(patient->displacement!=Unit::DIS_INSIDE);
    }
}

TEST_SUITE("UnitCustomization")
{
    TEST_CASE("mixed melee and transport conserve multiple material kinds and rejected packets")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-1"),6,8);
        REQUIRE(unit);
        CHECK(unit->hasCapability(UnitRuntimeTraits::Melee));
        CHECK(unit->hasCapability(UnitRuntimeTraits::Transport));
        unit->receiveCarriedMaterial(WOOD,{});
        unit->receiveCarriedMaterial(STONE,{1,2});
        unit->receiveCarriedMaterial(WOOD,{});
        CHECK(unit->carriedPacketCount()==3);
        auto& stats=world.game.teams[0]->stats;
        stats.beginMeasurementSnapshot(world.game.teams[0]);
        stats.observeMeasurementUnit(unit);
        CHECK(stats.measurements.carried[WOOD]==2);
        CHECK(stats.measurements.carried[STONE]==1);
        CHECK_FALSE(unit->canCarryMaterial(WOOD));
        CHECK(unit->hasCarriedMaterial(STONE));
        auto* inn=world.addBuilding("inn",8,8);
        REQUIRE(inn);
        // An inn does not need either carried construction material.
        CHECK_FALSE(unit->deliverCargo(*inn));
        CHECK(unit->carriedPacketCount()==3);
        auto* enemy=world.addUnit(WORKER,7,8,1);
        REQUIRE(enemy);
        unit->action=ATTACK_SPEED; unit->speed=12; unit->delta=128;
        unit->dx=1; unit->dy=0;
        const int hp=enemy->hp;
        unit->syncStep();
        CHECK(enemy->hp<hp);
        CHECK(unit->carriedPacketCount()==3);
        unit->clearCargo();
        CHECK(world.game.unitCargo.empty());
    }

    TEST_CASE("all locked capability ablations match fixed tick and RNG traces [golden][artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        std::vector<Uint32> expected;
        std::ostringstream golden; golden << "seed=4921 ticks=96 ablations=8\n";
        for (int repeat=0;repeat<2;++repeat) {
            glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
            configure(world);
            world.game.gameHeader.setHungerDisabled(true);
            for (int i=0;i<8;++i) {
                auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-"+std::to_string(i)),5+3*(i%4),5+3*(i/4));
                REQUIRE(unit);
                unit->receiveCarriedMaterial(WOOD,{});
                unit->receiveCarriedMaterial(STONE,{});
            }
            for (int tick=0;tick<96;++tick) {
                world.game.syncStep(0);
                const auto checksum=trace(world.game);
                if (!repeat) { expected.push_back(checksum); golden << tick << " " << checksum << "\n"; }
                else CHECK(checksum==expected[tick]);
            }
        }
        glob2test::expectGolden("unit-catalog/ablation-checksums.txt",golden.str());
        glob2test::writeFile(glob2test::artifactDir()/"unit-ablations.trace",golden.str());
    }

    TEST_CASE("extended cargo delivers one usable packet per completed work action")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
        definitions["units"][WORKER]["behaviors"]["cargoCapacity"]=4;
        definitions["units"][WORKER]["behaviors"]["cargoKinds"]=2;
        definitions["units"][WORKER]["behaviors"]["spillRejectedCargo"]=false;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump())); world.game.configureBuildingCatalog();
        world.game.gameHeader.setHungerDisabled(true);
        auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(WORKER,9,8);
        REQUIRE(inn); REQUIRE(unit);
        unit->receiveCarriedMaterial(WOOD,{});
        for (int i=0;i<3;++i) unit->receiveCarriedMaterial(WHEAT,{});
        unit->destinationPurpose=WHEAT;
        unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
        unit->setTargetBuilding(inn);
        inn->unitsWorking.push_back(unit); inn->desiredMaxUnitWorking=inn->maxUnitWorking=1;
        unit->destinationPurpose=WHEAT; unit->displacement=Unit::DIS_FILLING_BUILDING;
        unit->movement=Unit::MOV_FILLING; unit->action=BUILD; unit->speed=unit->performance[BUILD];
        unit->dx=1; unit->dy=0; unit->delta=255;
        unit->syncStep(); CHECK(inn->materials[WHEAT]==1); CHECK(unit->carriedPacketCount()==3);
        for (int tick=0;tick<31;++tick) unit->syncStep();
        CHECK(inn->materials[WHEAT]==1);
        unit->syncStep(); CHECK(inn->materials[WHEAT]==2); CHECK(unit->carriedPacketCount()==2);
        for (int tick=0;tick<32;++tick) unit->syncStep();
        CHECK(inn->materials[WHEAT]==3); CHECK(unit->carriedPacketCount()==1);
        CHECK(unit->hasCarriedMaterial(WOOD));
        CHECK(world.team->stats.measurements.materialSpillageEvents==0);
        unit->clearCargo();
        unit->receiveCargoPacket(STONE,{1,1000001});
        unit->receiveCarriedMaterial(WOOD,{});
        REQUIRE(unit->widePrimaryCargo);
        world.team->stats.beginMeasurementSnapshot(world.team); world.team->stats.observeMeasurementUnit(unit);
        CHECK(world.team->stats.measurements.carried[STONE]==1);
        CHECK(world.team->stats.measurements.carried[WOOD]==1);
    }

    TEST_CASE("tiling anchors and starting positions recognize producers of additional units")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        world.game.gameHeader.setUnitCatalog(UnitCatalog::fromJson(R"({"schemaVersion":1,"units":[{"key":"fixture:producer-unit","extends":"worker"}]})"));
        const int variant=world.game.buildingsTypes.getFinishedTypeNum("inn");
        auto definitions=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& production=definitions["variants"][variant]["semantics"]["production"];
        production["scheduling"]="weighted_committed_job";
        production["recipes"]={{"fixture:producer-unit",{{"enabled",true},{"duration",20},{"cost",nlohmann::json::object()}}}};
        production["initialRatios"]={{"fixture:producer-unit",7}};
        world.game.buildingsTypes.loadSnapshotJson(definitions.dump()); world.game.configureBuildingCatalog();
        auto* hospital=world.addBuilding("hospital",4,4); REQUIRE(hospital);
        auto* producer=world.game.addBuilding(12,12,variant,0,1,1); REQUIRE(producer);
        REQUIRE(producer->type->semantics.production.enabledUnitMask==0);
        REQUIRE(world.game.tileForPlay(2,1,2,1));
        for (int team=0;team<2;++team) {
            auto* colony=world.game.teams[team]; REQUIRE(colony);
            CHECK(colony->startPosSet);
            REQUIRE(colony->swarms.size()==1);
            const auto* copied=colony->swarms.front();
            CHECK(colony->startPosX==copied->posX); CHECK(colony->startPosY==copied->posY);
            CHECK(copied->productionRatio(3)==7);
        }
    }

    TEST_CASE("labor and defense statistics require usable clocks and recruitment mobility")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
        const auto append=[&](int base,const char* key,auto edit) {
            auto definition=definitions["units"][base]; definition["key"]=key;
            edit(definition); definitions["units"].push_back(definition);
        };
        append(WORKER,"fixture:no-build-clock",[](auto& d) { for(auto& l:d["levels"]) l["performance"][BUILD]=0; });
        append(WORKER,"fixture:no-harvest-clock",[](auto& d) { for(auto& l:d["levels"]) l["performance"][HARVEST]=0; });
        append(WORKER,"fixture:transportless-builder",[](auto& d) {
            d["behaviors"]["transport"]=false; d["behaviors"]["cargoCapacity"]=0; d["behaviors"]["cargoKinds"]=0;
        });
        append(WORKER,"fixture:stationary-carrier",[](auto& d) { d["behaviors"]["walk"]=false; d["behaviors"]["swim"]=false; });
        append(WARRIOR,"fixture:stationary-fighter",[](auto& d) { d["behaviors"]["walk"]=false; d["behaviors"]["swim"]=false; });
        append(WARRIOR,"fixture:no-melee-clock",[](auto& d) { for(auto& l:d["levels"]) l["performance"][ATTACK_SPEED]=0; });
        append(WARRIOR,"fixture:no-melee-damage",[](auto& d) { for(auto& l:d["levels"]) l["performance"][ATTACK_STRENGTH]=0; });
        append(EXPLORER,"fixture:stationary-scout",[](auto& d) {
            d["behaviors"]["fly"]=false; d["behaviors"]["walk"]=false; d["behaviors"]["swim"]=false;
        });
        append(WORKER,"fixture:service-carrier",[](auto& d) { d["behaviors"]["construct"]=false; });
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump())); world.game.configureBuildingCatalog();
        Unit* courier=nullptr;
        for(unsigned id=0;id<world.game.unitTypeCount();++id) {
            auto* unit=world.addUnit(id,4+2*(id%6),4+3*(id/6)); REQUIRE(unit);
            if(id+1==world.game.unitTypeCount()) courier=unit;
        }
        for(int sample=0;sample<TeamStats::STATS_SMOOTH_SIZE;++sample) world.team->stats.step(world.team);
        const auto* stats=world.team->stats.getLatestStat();
        CHECK(stats->carriers==2); CHECK(stats->builders==1); CHECK(stats->workersByConstructionLevel[0]==1);
        CHECK(stats->meleeUnits==2); CHECK(stats->scouts==1);
        CHECK(stats->idleCarriers==2); CHECK(stats->idleDefenders==1);
        REQUIRE(courier); courier->performance[BUILD]=0;
        for(int sample=0;sample<TeamStats::STATS_SMOOTH_SIZE;++sample) world.team->stats.step(world.team);
        stats=world.team->stats.getLatestStat();
        CHECK(stats->carriers==1); CHECK(stats->idleCarriers==1); CHECK(stats->idleDefenders==1);
    }

    TEST_CASE("mobility statistics respect swimming only and stationary definitions")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        world.game.map.paintCell(6,8,WATER);
        auto* swimmer=world.addUnit(*world.game.unitCatalog().find("swimmer"),6,8);
        auto* stationary=world.addUnit(*world.game.unitCatalog().find("stationary"),20,20);
        auto* walker=world.addUnit(WORKER,24,24);
        REQUIRE(swimmer); REQUIRE(stationary); REQUIRE(walker);
        world.team->stats.sampleTraps(world.team);
        for (int kind=0;kind<2;++kind) {
            CHECK(world.team->stats.measurements.trappedUnits[kind][swimmer->typeNum]==1);
            CHECK(world.team->stats.measurements.trappedUnits[kind][stationary->typeNum]==1);
            CHECK(world.team->stats.measurements.trappedUnits[kind][WORKER]==0);
        }
        world.game.map.paintCell(7,8,WATER);
        world.team->stats.sampleTraps(world.team);
        for (int kind=0;kind<2;++kind)
            CHECK(world.team->stats.measurements.trappedUnits[kind][swimmer->typeNum]==0);
    }

    TEST_CASE("survival eligibility is independent of movement and work capabilities")
    {
        glob2test::HeadlessGlobals globals;
        for (bool survives:{false,true}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto custom=definitions["units"][WORKER]; custom["key"]="fixture:survivor";
            auto& traits=custom["behaviors"];
            for (const auto* flag:{"transport","construct","clear","clearIdle","walk","swim","adjacentClearInterrupt"}) traits[flag]=false;
            traits["countsForSurvival"]=survives; traits["hungerRate"]=0;
            definitions["units"].push_back(custom);
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
            world.game.configureBuildingCatalog();
            auto* unit=world.addUnit(3,6,8); REQUIRE(unit);
            world.team->playersMask=1;
            world.team->syncStep();
            CHECK(world.team->isAlive==survives);
        }
    }

    TEST_CASE("conversion claim release preserves the stock policy and is configurable")
    {
        glob2test::HeadlessGlobals globals;
        for (bool releases:{false,true}) {
            glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
            if (releases) {
                auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
                definitions["units"][WORKER]["behaviors"]["releaseClearingClaims"]=true;
                world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
                world.game.configureBuildingCatalog();
            }
            auto* inn=world.addBuilding("inn",8,8,0,1); REQUIRE(inn);
            inn->materials[WHEAT]=10; inn->materials[CHERRY]=10; inn->updateCallLists();
            world.addUnit(WORKER,24,20,0); world.addUnit(WORKER,24,22,1);
            for (int tick=0;tick<160;++tick) world.game.syncStep(0);
            auto* unit=world.addUnit(WORKER,6,8); REQUIRE(unit); REQUIRE(inn->canConvertUnit());
            world.game.teams[1]->sharedVisionFood|=world.team->me;
            world.game.teams[1]->allies&=~world.team->me;
            const auto oldGid=unit->gid;
            unit->previousClearingArea=Unit::ClearingAreaClaim{7,7}; unit->previousClearingAreaDistance=1;
            world.game.map.setClearingAreaClaimed(7,7,0,oldGid);
            unit->hungry=unit->trigHungryCarrying; unit->medical=Unit::MED_HUNGRY; unit->needToRecheckMedical=true;
            REQUIRE(world.team->findNearestFood(unit)==inn);
            unit->handleActivity();
            REQUIRE(unit->owner==world.game.teams[1]);
            CHECK(bool(unit->previousClearingArea)==!releases);
            CHECK(world.game.map.isClearingAreaClaimed(7,7,0)==(releases?NOGUID:oldGid));
        }
    }

    TEST_CASE("swim only fields avoid land and stationary zero hunger units keep making medical progress")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        for (int y=10;y<=12;++y) for (int x=4;x<=14;++x) world.game.map.paintCell(x,y,WATER);
        auto* swimmer=world.addUnit(*world.game.unitCatalog().find("swimmer"),5,11);
        REQUIRE(swimmer);
        CHECK(swimmer->swimClass()==WATER_ONLY_CLASS);
        int dx=0,dy=0;
        CHECK(world.game.map.pathfindPointToPoint(5,11,13,11,&dx,&dy,swimmer->swimClass(),world.team->me,20));
        CHECK(world.game.map.terrainPropertiesAt(5+dx,11+dy).swimmable);
        CHECK_FALSE(world.game.map.pathfindPointToPoint(5,11,13,9,&dx,&dy,swimmer->swimClass(),world.team->me,20));
        auto* stationary=world.addUnit(*world.game.unitCatalog().find("stationary"),20,20);
        REQUIRE(stationary);
        const int food=stationary->hungry;
        for (int tick=0;tick<96;++tick) { stationary->syncStep(); swimmer->syncStep(); }
        CHECK(stationary->posX==20); CHECK(stationary->posY==20);
        CHECK(stationary->hungry==food); CHECK_FALSE(stationary->isUnitHungry());
        CHECK(world.game.map.terrainPropertiesAt(swimmer->posX,swimmer->posY).swimmable);
    }

    TEST_CASE("batch planning releases its own harvest obstacle for cold and warm fields without advancing clocks")
    {
        glob2test::HeadlessGlobals globals;
        for (int mode:{0,1,2}) for (bool warm:{false,true}) {
            const bool batched=mode==1;
            CAPTURE(mode); CAPTURE(warm);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            configure(world);
            if (mode==2) {
                auto catalog=nlohmann::json::parse(world.game.unitCatalog().serialize());
                catalog["units"][WORKER]["behaviors"]["spillRejectedCargo"]=false;
                world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(catalog.dump()));
                world.game.configureUnitCatalog();
            }
            auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            for(auto& variant:buildings["variants"]) if(variant["semantics"]["feeding"]["enabled"].get<bool>()) {
                variant["properties"]["maxMaterial"][WOOD]=1;
                variant["properties"]["maxMaterial"][WHEAT]=6;
                variant["semantics"]["replenishMaterials"].push_back("wood");
            }
            world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
            auto* inn=world.addBuilding("inn",10,8);
            auto* unit=world.addUnit(batched?*world.game.unitCatalog().find("ablation-1"):WORKER,6,8);
            REQUIRE(inn); REQUIRE(unit);
            deposit(world.game.map,7,9,WHEAT,3); world.game.map.setMapDiscovered();
            unit->receiveCarriedMaterial(WOOD,{}); unit->destinationPurpose=WOOD;
            unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
            inn->unitsWorking.push_back(unit);
            if(warm) REQUIRE(world.game.map.materialAvailableSlot(world.team->teamNumber,WHEAT,unit->swimClass(),6,8,false,inn));
            world.game.map.markImmobileUnit(6,8,world.team->teamNumber);
            REQUIRE(world.game.map.isImmobileUnit(6,8));
            const auto entityRandom=unit->entityRandom;
            const auto gameRandom=world.game.syncRandom;
            const int action=unit->action, delta=unit->delta, speed=unit->speed;
            const auto epochs=world.game.map.snapshotGenerations();
            CHECK(unit->continueCargoCollection()==batched);
            CHECK(unit->entityRandom==entityRandom); CHECK(world.game.syncRandom==gameRandom);
            CHECK(unit->action==action); CHECK(unit->delta==delta); CHECK(unit->speed==speed);
            CHECK(unit->posX==6); CHECK(unit->posY==8); CHECK(unit->carriedPacketCount()==1);
            CHECK(unit->jobPurpose==UnitJobPurpose::Transport);
            if(batched) {
                CHECK_FALSE(world.game.map.isImmobileUnit(6,8));
                CHECK(unit->destinationPurpose==WHEAT); CHECK(unit->targetX==7); CHECK(unit->targetY==9);
                CHECK(unit->displacement==Unit::DIS_GOING_TO_RESOURCE); CHECK(unit->validTarget);
                const auto cleared=world.game.map.snapshotGenerations();
                world.game.map.clearImmobileUnit(6,8);
                CHECK(world.game.map.snapshotGenerations()==cleared);
            } else {
                CHECK(world.game.map.isImmobileUnit(6,8)); CHECK(world.game.map.snapshotGenerations()==epochs);
                CHECK(unit->destinationPurpose==WOOD);
            }
        }
        // Zero-capacity non-carriers still enter the extended helper's
        // early return, preserving all authoritative state and map epochs.
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto catalog=nlohmann::json::parse(world.game.unitCatalog().serialize());
        catalog["units"][WORKER]["behaviors"]["transport"]=false;
        catalog["units"][WORKER]["behaviors"]["cargoCapacity"]=0;
        catalog["units"][WORKER]["behaviors"]["cargoKinds"]=0;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(catalog.dump()));
        world.game.configureUnitCatalog();
        auto* unit=world.addUnit(WORKER,6,8); REQUIRE(unit);
        world.game.map.markImmobileUnit(6,8,world.team->teamNumber);
        const auto before=continuationAudit(world.game);
        const auto epochs=world.game.map.snapshotGenerations();
        const auto random=unit->entityRandom;
        CHECK_FALSE(unit->continueCargoCollection());
        CHECK(continuationAudit(world.game)==before);
        CHECK(world.game.map.snapshotGenerations()==epochs);
        CHECK(unit->entityRandom==random);
    }

    TEST_CASE("ground and airborne couriers collect mixed kinds and deliver through the engine")
    {
        glob2test::HeadlessGlobals globals;
        for (const char* key:{"ablation-1","ablation-5"}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            configure(world);
            world.game.gameHeader.setHungerDisabled(true);
            world.game.gameHeader.setResourceGrowthDisabled(true);
            auto buildingJson=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            for (auto& variant:buildingJson["variants"])
                if (variant["semantics"]["feeding"]["enabled"].get<bool>()) {
                    variant["properties"]["maxMaterial"][WOOD]=1;
                    variant["properties"]["maxMaterial"][WHEAT]=6;
                    variant["semantics"]["replenishMaterials"].push_back("wood");
                }
            world.game.buildingsTypes.loadSnapshotJson(buildingJson.dump());
            world.game.configureBuildingCatalog();
            auto* inn=world.addBuilding("inn",10,8);
            auto* unit=world.addUnit(*world.game.unitCatalog().find(key),6,8);
            REQUIRE(inn); REQUIRE(unit);
            // Trees consume the entire deposit for one transport packet. Three
            // separate trees therefore provide three packets, unlike wheat.
            // One wood packet exhausts the building's wood demand. Remaining
            // cargo capacity must then collect wheat to form a mixed load.
            for (const auto& position:std::array<std::array<int,2>,3>{{{7,7},{6,18},{8,18}}})
                deposit(world.game.map,position[0],position[1],WOOD,1);
            deposit(world.game.map,7,9,WHEAT,3);
            world.game.map.setMapDiscovered();
            unit->destinationPurpose=WOOD;
            unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
            inn->unitsWorking.push_back(unit);
            inn->updateCallLists();
            bool sawMixed=false;
            for (int tick=0;tick<4096 && (inn->materials[WOOD]<1 || inn->materials[WHEAT]<3);++tick) {
                world.game.syncStep(0);
                sawMixed |= unit->hasCarriedMaterial(WOOD) && unit->hasCarriedMaterial(WHEAT);
            }
            CAPTURE(key);
            CHECK(sawMixed);
            CHECK(inn->materials[WOOD]==1); CHECK(inn->materials[WHEAT]==3);
            CHECK(world.team->stats.measurements.materialSpillageEvents==0);
            for (int material:{WOOD,WHEAT}) {
                int held=unit->carriedMaterial==material ? 1 : 0;
                if (const auto* overflow=world.game.unitCargo.find(unit->gid))
                    for (const auto& packet:*overflow) if (packet.material==material) ++held;
                int remaining=world.game.map.getResource(7,9).amount;
                if (material==WOOD) {
                    remaining=0;
                    for (const auto& position:std::array<std::array<int,2>,3>{{{7,7},{6,18},{8,18}}})
                        remaining+=world.game.map.getResource(position[0],position[1]).amount;
                }
                CHECK(remaining+inn->materials[material]+held==3);
                CHECK(world.team->stats.measurements.harvested[material]==Uint64(inn->materials[material]+held));
            }
        }
    }

    TEST_CASE("game save resumes mixed cargo and per entity RNG at every tick")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        world.game.gameHeader.setHungerDisabled(true);
        auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-3"),6,8);
        REQUIRE(unit);
        unit->receiveCarriedMaterial(WOOD,{1,2}); unit->receiveCarriedMaterial(STONE,{});
        for (int tick=0;tick<17;++tick) world.game.syncStep(0);
        auto* storage=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(storage);
        world.game.save(&output,false,"mixed unit continuation");
        const std::string bytes(storage->getBuffer(),storage->getPosition());
        std::vector<Uint32> expected;
        for (int tick=0;tick<96;++tick) { world.game.syncStep(0); expected.push_back(trace(world.game)); }
        GameGUI resumed;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        REQUIRE(resumed.game.load(&input));
        resumed.game.setWaitingOnMask(0);
        auto* copy=resumed.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)];
        REQUIRE(copy); CHECK(copy->carriedPacketCount()==2);
        for (int tick=0;tick<96;++tick) { resumed.game.syncStep(0); CHECK(trace(resumed.game)==expected[tick]); }
    }

    TEST_CASE("passive healing and starvation damage are independent configured rates")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        auto* healer=world.addUnit(*world.game.unitCatalog().find("healer"),6,8);
        auto* starver=world.addUnit(*world.game.unitCatalog().find("starver"),20,20);
        REQUIRE(healer); REQUIRE(starver);
        healer->hp=100;
        starver->hungry=0; starver->delta=255;
        world.game.syncStep(0);
        CHECK(healer->hp==101);
        CHECK(starver->hp==197);
        CHECK(starver->hungry==-1000);
        CHECK_FALSE(healer->isUnitHungry());
        for (int tick=0;tick<31;++tick) world.game.syncStep(0);
        CHECK(healer->hp==132);
        CHECK(starver->hp<197);
    }

    TEST_CASE("configured service multipliers shorten complete meals and healing visits")
    {
        glob2test::HeadlessGlobals globals;
        for (int purpose:{int(FEED),int(HEAL)}) {
            int durations[2]={0,0};
            for (int fast=0;fast<2;++fast) {
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
                configure(world);
                auto* building=world.addBuilding(purpose==FEED?"inn":"hospital",10,8);
                auto* unit=world.addUnit(fast?*world.game.unitCatalog().find("fast-service"):WORKER,6,8);
                REQUIRE(building); REQUIRE(unit);
                building->materials[WHEAT]=building->type->maxMaterial[WHEAT]; building->updateCallLists();
                unit->hungry=purpose==FEED?10000:unit->foodCapacity();
                unit->hp=purpose==HEAL?50:unit->performance[HP];
                unit->medical=purpose==FEED?Unit::MED_HUNGRY:Unit::MED_DAMAGED;
                unit->needToRecheckMedical=true;
                bool entered=false,completed=false;
                for (int tick=0;tick<10000 && !completed;++tick) {
                    world.game.syncStep(0);
                    entered |= unit->displacement==Unit::DIS_INSIDE;
                    if (entered) ++durations[fast];
                    completed=entered && unit->displacement==Unit::DIS_EXITING_BUILDING;
                }
                CHECK(entered); CHECK(completed);
                if (purpose==FEED) CHECK(unit->hungry>unit->trigHungry);
                else CHECK(unit->hp==unit->performance[HP]);
            }
            CHECK(durations[1]<durations[0]);
        }
    }

    TEST_CASE("airborne clearing and magic coexist with held cargo")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        world.game.gameHeader.setHungerDisabled(true);
        auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-7"),6,8);
        auto* enemy=world.addUnit(WORKER,9,8,1);
        REQUIRE(unit); REQUIRE(enemy);
        unit->receiveCarriedMaterial(STONE,{});
        const int initialHp=enemy->hp;
        unit->delta=255;
        world.game.syncStep(0);
        CHECK(enemy->hp<initialHp);
        world.game.gameHeader.setResourceGrowthDisabled(true);
        auto* clearer=world.addUnit(*world.game.unitCatalog().find("ablation-6"),12,15);
        REQUIRE(clearer); clearer->receiveCarriedMaterial(STONE,{});
        deposit(world.game.map,15,15,WOOD,3);
        world.game.map.addClearArea(15,15,world.team->teamNumber);
        REQUIRE(world.game.map.isClearingTarget(world.game.map.coordToIndex(15,15),world.team->me,false));
        REQUIRE_FALSE(world.game.map.isClearingTarget(world.game.map.coordToIndex(15,15),world.game.teams[1]->me,false));
        world.game.map.setMapDiscovered();
        for (int tick=0;tick<4096 && world.game.map.getResource(15,15).amount;++tick) world.game.syncStep(0);
        CHECK(world.game.map.getResource(15,15).amount==0);
        CHECK(unit->hasCarriedMaterial(STONE));
        CHECK(clearer->hasCarriedMaterial(STONE));
        CHECK(world.team->stats.measurements.cleared[WOOD]>0);
    }

    TEST_CASE("conversion cancels pending transport and transfers the complete cargo identity")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        auto* ownInn=world.addBuilding("inn",18,8,0,0);
        auto* enemyInn=world.addBuilding("inn",8,8,0,1);
        world.addUnit(WORKER,24,20,0); world.addUnit(WORKER,24,22,1);
        REQUIRE(ownInn); REQUIRE(enemyInn);
        enemyInn->materials[WHEAT]=10; enemyInn->materials[CHERRY]=10;
        enemyInn->updateCallLists();
        for (int tick=0;tick<160;++tick) world.game.syncStep(0);
        auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-0"),6,8);
        REQUIRE(unit); REQUIRE(enemyInn->canConvertUnit());
        world.game.teams[1]->sharedVisionFood |= world.team->me;
        world.game.teams[1]->allies &= ~world.team->me;
        unit->receiveCarriedMaterial(WOOD,{}); unit->receiveCarriedMaterial(STONE,{1,2});
        unit->destinationPurpose=WHEAT;
        deposit(world.game.map,19,7,WHEAT,3);
        unit->subscriptionSuccess(ownInn,false,false,UnitJobPurpose::Transport);
        ownInn->unitsWorking.push_back(unit);
        const Uint16 oldGid=unit->gid;
        unit->previousClearingArea=Unit::ClearingAreaClaim{7,7}; unit->previousClearingAreaDistance=1;
        world.game.map.setClearingAreaClaimed(7,7,world.team->teamNumber,oldGid);
        unit->hungry=unit->trigHungryCarrying; unit->medical=Unit::MED_HUNGRY;
        unit->needToRecheckMedical=true; unit->delta=255;
        REQUIRE(world.team->findNearestFood(unit)==enemyInn);
        world.game.syncStep(0);
        CHECK(unit->owner==world.game.teams[1]);
        CHECK(unit->gid!=oldGid);
        CHECK(world.game.map.isClearingAreaClaimed(7,7,world.team->teamNumber)==NOGUID);
        CHECK_FALSE(unit->previousClearingArea);
        CHECK(unit->jobPurpose==UnitJobPurpose::None);
        CHECK(unit->carriedPacketCount()==2);
        CHECK_FALSE(world.game.unitCargo.find(oldGid));
        REQUIRE(world.game.unitCargo.find(unit->gid));
        CHECK(world.game.unitCargo.find(unit->gid)->front().material==STONE);
        CHECK(std::find(ownInn->unitsWorking.begin(),ownInn->unitsWorking.end(),unit)==ownInn->unitsWorking.end());
    }

    TEST_CASE("retained single packets and kind limits do not withdraw incompatible resources")
    {
        glob2test::HeadlessGlobals globals;
        for (int capacity:{1,3}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            const std::string json=R"({"schemaVersion":1,"units":[{"key":"limited","extends":"worker","behaviors":{"spillRejectedCargo":false,"cargoKinds":1,"cargoCapacity":)"+std::to_string(capacity)+"}}]}";
            world.game.gameHeader.setUnitCatalog(UnitCatalog::fromJson(json)); world.game.configureBuildingCatalog();
            world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
            auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(3,8,8);
            REQUIRE(inn); REQUIRE(unit);
            deposit(world.game.map,8,7,WHEAT,3); world.game.map.setMapDiscovered();
            unit->receiveCarriedMaterial(STONE,{});
            unit->destinationPurpose=WHEAT;
            unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport); inn->unitsWorking.push_back(unit);
            unit->delta=255;
            for (int tick=0;tick<256;++tick) world.game.syncStep(0);
            CHECK(unit->hasCarriedMaterial(STONE)); CHECK_FALSE(unit->hasCarriedMaterial(WHEAT));
            CHECK(world.game.map.getResource(8,7).amount==3); CHECK(inn->materials[WHEAT]==0);
            CHECK(world.team->stats.measurements.materialSpillageEvents==0);
        }
    }

    TEST_CASE("extreme hunger and experience saturate without changing valid stock arithmetic")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto extreme=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto definition=extreme["units"][WARRIOR]; definition["key"]="immortal";
        definition["behaviors"]["foodCapacity"]=1000000; definition["behaviors"]["hungerRate"]=1000000;
        definition["behaviors"]["starvationDamage"]=0;
        for (auto& level:definition["levels"]) { level["performance"][ATTACK_STRENGTH]=1000000; level["experiencePerLevel"]=1000000; }
        extreme["units"].push_back(definition);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(extreme.dump()));
        world.game.configureBuildingCatalog();
        auto* unit=world.addUnit(3,8,8); REQUIRE(unit);
        unit->hungry=INT_MIN+500000;
        for (int tick=0;tick<16;++tick) world.game.syncStep(0);
        CHECK(unit->hungry==INT_MIN); CHECK(unit->hp==unit->performance[HP]);
        CHECK(unit->foodStepsLeft(INT_MAX)<0); CHECK(unit->numberOfStepsLeftUntilHungry()<0);
        unit->experienceLevel=46; unit->experience=INT_MAX-1;
        CHECK(unit->getNextLevelThreshold()==INT_MAX);
        unit->incrementExperience(1000000); CHECK(unit->experience==INT_MAX); CHECK(unit->experienceLevel==46);
        for (int food:{INT_MIN,-1,0,150000,INT_MAX})
        for (int threshold:{INT_MIN,-1,0,37500,INT_MAX})
        for (int rate:{-1,0,1,425,INT_MAX}) {
            CAPTURE(food); CAPTURE(threshold); CAPTURE(rate);
            unit->hungry=food; unit->trigHungry=threshold; unit->hungriness=rate;
            const Sint64 remaining=Sint64(food)-threshold;
            const int quotient=rate ? int(std::clamp<Sint64>(remaining/rate,INT_MIN,INT_MAX)) : INT_MAX;
            CHECK(unit->numberOfStepsLeftUntilHungry()==quotient);
            CHECK(unit->stepsLeftUntilHungry==quotient);
            CHECK(unit->foodStepsLeft(threshold)==(rate>0 ? quotient : INT_MAX/4));
        }
    }

    TEST_CASE("unit experiment gates control spawning and reject disabled live definitions")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        world.game.gameHeader.setUnitCatalog(UnitCatalog::fromJson(R"({"schemaVersion":1,"experiments":[{"key":"fixture-unit","label":"Fixture unit","help":"Tests unit gating"}],"units":[{"key":"fixture:gated","extends":"worker","requiredExperiment":"fixture-unit"}]})"));
        world.game.configureBuildingCatalog(); CHECK_FALSE(world.game.addUnit(8,8,0,3,0,0,0,0));
        auto& header=world.game.gameHeader;
        header.getExperiments().set("fixture-unit",true,header.catalogExperimentKeys()); world.game.configureBuildingCatalog();
        auto* unit=world.addUnit(3,8,8); REQUIRE(unit);
        header.getExperiments().set("fixture-unit",false,header.catalogExperimentKeys());
        CHECK_THROWS(world.game.configureBuildingCatalog());
        header.getExperiments().set("fixture-unit",true,header.catalogExperimentKeys()); world.game.configureBuildingCatalog();
        auto* storage=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream output(storage);
        world.game.save(&output,false,"enabled unit experiment");
        const std::string bytes(storage->getBuffer(),storage->getPosition());
        GameGUI resumed; GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
        REQUIRE(resumed.game.load(&input)); CHECK(resumed.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)]->typeNum==3);
        header.getExperiments().clear();
        auto* malformed=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream badOutput(malformed);
        world.game.save(&badOutput,false,"disabled live unit experiment");
        const std::string badBytes(malformed->getBuffer(),malformed->getPosition());
        GameGUI rejected; GAGCore::BinaryInputStream badInput(new GAGCore::MemoryStreamBackend(badBytes.data(),badBytes.size())); badInput.seekFromStart(0);
        CHECK_FALSE(rejected.game.load(&badInput));
    }

    TEST_CASE("fractional deliveries retain exact residuals across multipliers and reject overflow atomically")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        for (auto& variant:catalog["variants"]) if (variant["semantics"]["feeding"]["enabled"].get<bool>()) {
            variant["properties"]["maxMaterial"][WOOD]=1;
            variant["properties"]["materialMultiplier"][WOOD]=3;
            variant["semantics"]["replenishMaterials"].push_back("wood");
        }
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("inn",10,8);
        auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-3"),6,8);
        REQUIRE(building); REQUIRE(unit);
        unit->receiveCarriedMaterial(WOOD,{1,2}); unit->receiveCarriedMaterial(STONE,{});
        CHECK(unit->deliverCargo(*building)); CHECK(building->materials[WOOD]==1);
        CHECK(unit->carriedPacket.numerator==1); CHECK(unit->carriedPacket.denominator==6);
        CHECK(unit->carriedPacketCount()==2); CHECK(unit->hasCarriedMaterial(STONE));
        CHECK(world.team->stats.measurements.materialSpillageEvents==0);
        building->materials[WOOD]=0;
        const auto delivered=world.team->stats.measurements.delivered[WOOD];
        const int health=building->hp;
        constexpr Uint64 maximum=std::numeric_limits<Uint64>::max();
        const WideMaterialPacket incoming{maximum/2,maximum-2};
        const auto result=building->deliverCargoPacket(WOOD,incoming);
        CHECK(result.acceptedStock==0); CHECK(result.residual==incoming);
        CHECK(building->materials[WOOD]==0); CHECK(building->hp==health);
        CHECK(world.team->stats.measurements.delivered[WOOD]==delivered);
        CHECK(world.team->stats.measurements.materialSpillageEvents==0);
    }

    TEST_CASE("legacy setup sentinel retains resolved map definitions and cached unit values")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        const auto base=UnitCatalog::builtins();
        std::vector<std::array<UnitType,NB_UNIT_LEVELS>> levels;
        for (unsigned type=0;type<base->size();++type) levels.push_back(base->levels(type));
        const auto resolved=base->withLegacyLevels(levels,700);
        world.game.gameHeader.setUnitCatalog(resolved); world.game.configureBuildingCatalog();
        auto* unit=world.addUnit(WORKER,8,8); REQUIRE(unit);
        unit->hp=147; unit->hungry=48123; unit->performance[WALK]=17; unit->hungriness=701;
        const auto random=unit->entityRandom.exportState();
        GameHeader setup=world.game.gameHeader;
        setup.setUnitCatalog(UnitCatalog::legacyMigration());
        world.game.setGameHeader(setup,true);
        CHECK(world.game.unitCatalog().digest()==resolved->digest());
        CHECK(world.team->race.getCatalog()->digest()==resolved->digest());
        CHECK(unit->hp==147); CHECK(unit->hungry==48123);
        CHECK(unit->performance[WALK]==17); CHECK(unit->hungriness==701);
        CHECK(unit->entityRandom.exportState()==random);
        setup.setUnitCatalog(UnitCatalog::fromJson(R"({"schemaVersion":1,"units":[{"key":"worker","behaviors":{"hungerRate":900}}]})"));
        CHECK_THROWS_AS(world.game.setGameHeader(setup,true),std::runtime_error);
        CHECK(world.game.unitCatalog().digest()==resolved->digest());
        CHECK(unit->performance[WALK]==17); CHECK(unit->hungriness==701);
        CHECK(unit->entityRandom.exportState()==random);
    }

    TEST_CASE("migrated tables retain cached performance idle and contact policies")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
        auto base=UnitCatalog::builtins();
        std::vector<std::array<UnitType,NB_UNIT_LEVELS>> levels;
        for (unsigned type=0;type<base->size();++type) levels.push_back(base->levels(type));
        levels[WORKER][0].performance[ATTACK_SPEED]=12;
        levels[WORKER][0].performance[ATTACK_STRENGTH]=13;
        levels[EXPLORER][0].performance[FLY]=0;
        levels[EXPLORER][0].performance[WALK]=16;
        auto migrated=base->withLegacyLevels(levels,425);
        world.game.gameHeader.setUnitCatalog(migrated); world.game.configureBuildingCatalog();
        auto* combatWorker=world.addUnit(WORKER,6,8);
        auto* enemy=world.addUnit(WORKER,7,8,1);
        auto* groundedExplorer=world.addUnit(EXPLORER,18,18);
        REQUIRE(combatWorker); REQUIRE(enemy); REQUIRE(groundedExplorer);
        combatWorker->delta=255; groundedExplorer->delta=255;
        CHECK(combatWorker->trigHungry==30000);
        world.game.syncStep(0);
        CHECK(combatWorker->displacement==Unit::DIS_ATTACKING_AROUND);
        CHECK(combatWorker->movement==Unit::MOV_ATTACKING_TARGET);
        CHECK(groundedExplorer->displacement==Unit::DIS_RANDOM);
        CHECK(groundedExplorer->movement==Unit::MOV_RANDOM_GROUND);
        CHECK(world.game.map.getAirUnit(groundedExplorer->posX,groundedExplorer->posY)==NOGUID);
    }

    TEST_CASE("an explicit producer creates stationary units on a legal birth cell")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        const int type=*world.game.unitCatalog().find("stationary");
        auto definitions=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        for (auto& variant:definitions["variants"]) if (variant["key"]=="swarm.0.finished") {
            auto& production=variant["semantics"]["production"];
            production["scheduling"]="weighted_committed_job";
            production["recipes"]={{"stationary",{{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}}}};
            production["initialRatios"]={{"stationary",1}};
        }
        world.game.buildingsTypes.loadSnapshotJson(definitions.dump()); world.game.configureBuildingCatalog();
        auto* producer=world.addBuilding("swarm",10,10); REQUIRE(producer);
        world.team->addToStaticAbilitiesLists(producer);
        REQUIRE(world.team->isAlive);
        REQUIRE(std::find(world.team->swarms.begin(),world.team->swarms.end(),producer)!=world.team->swarms.end());
        REQUIRE(producer->runtime->produces(type)); REQUIRE(producer->productionRatio(type)==1);
        REQUIRE(producer->canAffordProduction(type));
        REQUIRE(producer->selectProductionRecipe()==type);
        int exitX,exitY,exitDx,exitDy;
        REQUIRE(producer->findGroundExit(&exitX,&exitY,&exitDx,&exitDy,false,true));
        Unit* stationary=nullptr;
        for (int tick=0;tick<32 && !stationary;++tick) {
            world.game.syncStep(0);
            for (Unit* unit:world.team->liveUnits.entries()) if (unit->typeNum==type) stationary=unit;
        }
        REQUIRE(stationary);
        producer->productionRatio(type)=0;
        const int x=stationary->posX,y=stationary->posY;
        CHECK(world.game.map.terrainPropertiesAt(x,y).walkable);
        CHECK(stationary->performance[WALK]==0); CHECK(stationary->performance[SWIM]==0);
        CHECK(world.team->stats.measurements.births[type]==1);
        for (int tick=0;tick<96;++tick) world.game.syncStep(0);
        CHECK(stationary->posX==x); CHECK(stationary->posY==y);
    }

    TEST_CASE("high unit IDs spawn work and resume all indexed state")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        nlohmann::json definitions={{"schemaVersion",1},{"units",nlohmann::json::array()}};
        for (int id=3;id<1024;++id) definitions["units"].push_back({{"key","fixture-"+std::to_string(id)},
            {"extends","worker"},{"behaviors",{{"cargoCapacity",3},{"cargoKinds",3}}}});
        auto catalog=UnitCatalog::fromJson(definitions.dump());
        world.game.gameHeader.setUnitCatalog(catalog); world.game.configureBuildingCatalog();
        world.game.gameHeader.setHungerDisabled(true);
        auto* inn=world.addBuilding("inn",10,10);
        auto* turret=world.addBuilding("defencetower",20,20);
        REQUIRE(inn); REQUIRE(turret);
        std::vector<Uint16> gids;
        for (int id:{35,275,1023}) {
            auto* unit=world.addUnit(id,6,8+int(gids.size())*4);
            REQUIRE(unit);
            CHECK(unit->typeNum==id); CHECK(unit->hasCapability(UnitRuntimeTraits::Transport));
            unit->receiveCarriedMaterial(WHEAT,{}); unit->receiveCarriedMaterial(STONE,{1,2});
            unit->destinationPurpose=WHEAT;
            unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport); inn->unitsWorking.push_back(unit);
            gids.push_back(unit->gid);
            CHECK(world.team->stats.measurements.births[id]==0);
            CHECK(turret->runtime->damage(id)==turret->runtime->damage(WORKER));
        }
        inn->updateCallLists();
        for (int tick=0;tick<32;++tick) world.game.syncStep(0);
        for (int id:{35,275,1023}) CHECK(world.team->stats.getLatestStat()->numberUnitPerType[id]==1);
        auto* storage=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream output(storage);
        world.game.save(&output,false,"high unit identity continuation");
        const std::string bytes(storage->getBuffer(),storage->getPosition());
        std::vector<Uint32> expected;
        for (int tick=0;tick<96;++tick) { world.game.syncStep(0); expected.push_back(trace(world.game)); }
        GameGUI resumed;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
        REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
        CHECK(resumed.game.unitTypeCount()==1024);
        for (Uint16 gid:gids) {
            auto* unit=resumed.game.teams[0]->myUnits[Unit::GIDtoID(gid)]; REQUIRE(unit);
            CHECK(unit->typeNum>31); CHECK(unit->jobPurpose==UnitJobPurpose::Transport);
            CHECK(unit->carriedPacketCount()==2); CHECK(unit->hasCarriedMaterial(STONE));
            CHECK(resumed.game.teams[0]->stats.getLatestStat()->numberUnitPerType[unit->typeNum]==1);
        }
        for (int tick=0;tick<96;++tick) { resumed.game.syncStep(0); CHECK(trace(resumed.game)==expected[tick]); }
        CHECK(inn->materials[WHEAT]>0);
        auto& stats=world.team->stats;
        const auto* maxima=&stats.samplingMaxima.isFree[3];
        const auto* eligibility=&stats.samplingEligibility[3];
        const auto* qualifications=&stats.samplingQualifications[3];
        const auto scratchBytes=stats.samplingMaxima.isFree.extraCapacityBytes()
            +stats.samplingEligibility.extraCapacityBytes()+stats.samplingQualifications.extraCapacityBytes();
        REQUIRE(scratchBytes==std::size_t(1021)*(sizeof(int)+sizeof(std::array<int,4>)+sizeof(std::array<Uint8,2>)));
        const auto random=world.game.syncRandom;
        for(int sample=0;sample<96;++sample) stats.step(world.team);
        CHECK(&stats.samplingMaxima.isFree[3]==maxima);
        CHECK(&stats.samplingEligibility[3]==eligibility);
        CHECK(&stats.samplingQualifications[3]==qualifications);
        CHECK(stats.samplingCatalog==catalog);
        CHECK(stats.samplingQualifications[275][0]==1);
        CHECK(world.game.syncRandom==random);
        // Count alone cannot identify an immutable catalog: replacement tables
        // with the same IDs must invalidate the qualification cache.
        auto replacement=nlohmann::json::parse(catalog->serialize());
        replacement["units"][275]["levels"][3]["performance"][BUILD]=0;
        auto changed=UnitCatalog::deserialize(replacement.dump());
        world.game.gameHeader.setUnitCatalog(changed); world.game.configureBuildingCatalog();
        for(int sample=0;sample<32;++sample) stats.step(world.team);
        CHECK(stats.samplingCatalog==changed);
        CHECK(stats.samplingQualifications[275][0]==0);
        CHECK(&stats.samplingEligibility[3]==eligibility);
        CHECK(world.game.syncRandom==random);
    }

    TEST_CASE("concurrent catalogs keep independent live performance and continuation")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
        glob2test::HeadlessGame first({.clearImmobile=true,.header=true,.seed=4921});
        glob2test::HeadlessGame second({.clearImmobile=true,.header=true,.seed=4921});
        auto altered=nlohmann::json::parse(second.game.unitCatalog().serialize());
        altered["units"][0]["behaviors"]["foodCapacity"]=90000;
        altered["units"][0]["levels"][0]["performance"][WALK]=24;
        auto catalog=UnitCatalog::deserialize(altered.dump());
        second.game.gameHeader.setUnitCatalog(catalog); second.game.configureBuildingCatalog();
        auto* oldUnit=first.addUnit(WORKER,6,8); auto* changedUnit=second.addUnit(WORKER,6,8);
        REQUIRE(oldUnit); REQUIRE(changedUnit);
        CHECK(oldUnit->performance[WALK]==16); CHECK(changedUnit->performance[WALK]==24);
        CHECK(oldUnit->foodCapacity()==150000); CHECK(changedUnit->foodCapacity()==90000);
        for (int tick=0;tick<32;++tick) { first.game.syncStep(0); second.game.syncStep(0); }
        auto* storage=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream output(storage);
        first.game.save(&output,false,"independent catalog");
        const std::string bytes(storage->getBuffer(),storage->getPosition());
        std::vector<Uint32> expected;
        for (int tick=0;tick<96;++tick) { first.game.syncStep(0); second.game.syncStep(0); expected.push_back(trace(first.game)); }
        GameGUI resumed;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
        REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
        for (int tick=0;tick<96;++tick) { resumed.game.syncStep(0); second.game.syncStep(0); CHECK(trace(resumed.game)==expected[tick]); }
        CHECK(oldUnit->performance[WALK]==16); CHECK(changedUnit->performance[WALK]==24);
    }

    TEST_CASE("deleted units release wide cargo before a GID is reused")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        const int type=*world.game.unitCatalog().find("ablation-3");
        auto* unit=world.addUnit(type,6,8);
        REQUIRE(unit);
        const Uint16 gid=unit->gid;
        unit->receiveCargoPacket(WOOD,{1,1000000000039ull});
        unit->receiveCarriedMaterial(STONE,{});
        REQUIRE(world.game.unitCargo.find(gid));
        unit->previousClearingArea=Unit::ClearingAreaClaim{7,7}; unit->previousClearingAreaDistance=1;
        world.game.map.setClearingAreaClaimed(7,7,world.team->teamNumber,gid);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(6,8,Game::DEL_UNIT));
        CHECK_FALSE(world.game.unitCargo.find(gid));
        CHECK(world.game.map.isClearingAreaClaimed(7,7,world.team->teamNumber)==NOGUID);
        auto* replacement=world.addUnit(type,6,8);
        REQUIRE(replacement);
        CHECK(replacement->gid==gid);
        CHECK(replacement->carriedPacketCount()==0);
        CHECK_FALSE(replacement->widePrimaryCargo);
    }

    TEST_CASE("mixed inventory and assigned purpose survive unit serialization")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        configure(world);
        auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-3"),6,8);
        REQUIRE(unit);
        unit->receiveCargoPacket(WOOD,{1,1000000000039ull}); unit->receiveCarriedMaterial(STONE,{});
        unit->activity=Unit::ACT_FLAG;
        unit->jobPurpose=UnitJobPurpose::Defend;
        auto* storage=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(storage);
        unit->save(&output);
        const std::string bytes(storage->getBuffer(),storage->getPosition());
        unit->clearCargo();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        unit->load(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG);
        CHECK(unit->jobPurpose==UnitJobPurpose::Defend);
        CHECK(unit->carriedPacketCount()==2);
        CHECK(unit->hasCarriedMaterial(WOOD)); CHECK(unit->hasCarriedMaterial(STONE));
        CHECK(unit->widePrimaryCargo);
        REQUIRE(world.game.unitCargo.find(unit->gid));
        CHECK(world.game.unitCargo.find(unit->gid)->front().packet.denominator==1000000000039ull);
    }

    TEST_CASE("setup cancels obsolete live work clocks and claims without losing cargo or RNG")
    {
        glob2test::HeadlessGlobals globals;
        for (int disabled:{HARVEST,BUILD}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto* unit=world.addUnit(WORKER,6,8);
            auto* inn=world.addBuilding("inn",10,8);
            REQUIRE(unit); REQUIRE(inn);
            unit->destinationPurpose=WHEAT;
            unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
            inn->unitsWorking.push_back(unit);
            unit->receiveCarriedMaterial(WOOD,{});
            unit->displacement=disabled==HARVEST?Unit::DIS_HARVESTING:Unit::DIS_FILLING_BUILDING;
            unit->movement=disabled==HARVEST?Unit::MOV_HARVESTING:Unit::MOV_FILLING;
            unit->action=Abilities(disabled); unit->speed=unit->performance[disabled];
            unit->previousClearingArea=Unit::ClearingAreaClaim{7,8};
            world.game.map.setClearingAreaClaimed(7,8,world.team->teamNumber,unit->gid);
            const auto random=unit->entityRandom.exportState();
            auto altered=nlohmann::json::parse(world.game.unitCatalog().serialize());
            for (auto& level:altered["units"][WORKER]["levels"]) level["performance"][disabled]=0;
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(altered.dump()));
            world.game.configureBuildingCatalog();
            CHECK(unit->activity==Unit::ACT_RANDOM); CHECK(unit->jobPurpose==UnitJobPurpose::None);
            CHECK_FALSE(unit->attachedBuilding); CHECK(inn->unitsWorking.empty());
            CHECK_FALSE(unit->previousClearingArea);
            CHECK(world.game.map.isClearingAreaClaimed(7,8,world.team->teamNumber)==NOGUID);
            CHECK(unit->hasCarriedMaterial(WOOD)); CHECK(unit->carriedPacketCount()==1);
            CHECK(unit->entityRandom.exportState()==random); CHECK(unit->speed>0);
            for (int tick=0;tick<96;++tick) world.game.syncStep(0);
            CHECK(unit->integrity());
        }
    }

    TEST_CASE("setup releases original parallel course reservations before disabling learnability")
    {
        glob2test::HeadlessGlobals globals;
        for (bool entered:{false,true}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            auto& semantics=buildings["variants"][3]["semantics"];
            semantics["trainingInParallel"]=true;
            semantics["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",3},{"cost",{{"food",2}}}};
            semantics["training"]["build"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",4},{"cost",{{"food",3}}}};
            world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
            auto* unit=world.addUnit(WORKER,6,8); auto* school=world.addBuilding("inn",10,8);
            REQUIRE(unit); REQUIRE(school);
            school->materials[WHEAT]=5;
            unit->destinationPurpose=WALK; school->subscribeUnitForInside(unit);
            REQUIRE(unit->serviceResourcesReserved); REQUIRE(school->reservedMaterials[WHEAT]==5);
            if (entered) unit->displacement=Unit::DIS_ENTERING_BUILDING;
            const auto random=unit->entityRandom.exportState();
            const auto oldCatalog=world.game.gameHeader.getUnitCatalog();
            auto units=nlohmann::json::parse(oldCatalog->serialize());
            units["units"][WORKER]["behaviors"]["learnableMask"]=Uint32(world.game.unitCatalog().runtime(WORKER).learnableMask&~(1u<<BUILD));
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
            if (entered) {
                CHECK_THROWS(world.game.configureBuildingCatalog());
                CHECK(unit->serviceResourcesReserved); CHECK(school->reservedMaterials[WHEAT]==5);
                CHECK(unit->attachedBuilding==school); CHECK(world.team->race.getCatalog()==oldCatalog);
                world.game.gameHeader.setUnitCatalog(oldCatalog);
            } else {
                world.game.configureBuildingCatalog();
                CHECK_FALSE(unit->serviceResourcesReserved); CHECK(school->reservedMaterials[WHEAT]==0);
                CHECK(school->unitsInside.empty()); CHECK(unit->activity==Unit::ACT_RANDOM);
            }
            CHECK(school->materials[WHEAT]==5); CHECK(unit->entityRandom.exportState()==random);
        }
    }

    TEST_CASE("transport capability with a zero delivery clock cannot be hired")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        for (auto& level:units["units"][WORKER]["levels"]) level["performance"][BUILD]=0;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
        world.game.configureBuildingCatalog();
        world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
        auto* empty=world.addUnit(WORKER,6,8); auto* loaded=world.addUnit(WORKER,6,9);
        auto* inn=world.addBuilding("inn",10,8);
        REQUIRE(empty); REQUIRE(loaded); REQUIRE(inn);
        loaded->receiveCarriedMaterial(WHEAT,{});
        deposit(world.game.map,7,8,WHEAT,3); world.game.map.setMapDiscovered();
        inn->desiredMaxUnitWorking=2; inn->maxUnitWorking=2;
        CHECK_FALSE(inn->canUnitWorkHere(empty)); CHECK_FALSE(inn->canUnitWorkHere(loaded));
        CHECK_FALSE(inn->subscribeToBringMaterialsStep());
        for (int tick=0;tick<96;++tick) {
            world.game.syncStep(0);
            CHECK(empty->activity!=Unit::ACT_FILLING); CHECK(loaded->activity!=Unit::ACT_FILLING);
        }
        CHECK(empty->speed>0); CHECK(loaded->speed>0);
        CHECK(loaded->hasCarriedMaterial(WHEAT)); CHECK(inn->unitsWorking.empty());
    }

    TEST_CASE("initial hiring respects retained packet capacity and material kind limits")
    {
        glob2test::HeadlessGlobals globals;
        for (int capacity:{1,3}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto& traits=units["units"][WORKER]["behaviors"];
            traits["cargoCapacity"]=capacity; traits["cargoKinds"]=1; traits["spillRejectedCargo"]=false;
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump())); world.game.configureBuildingCatalog();
            world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
            auto* unit=world.addUnit(WORKER,6,8); auto* inn=world.addBuilding("inn",10,8);
            REQUIRE(unit); REQUIRE(inn);
            for (int packet=0;packet<capacity;++packet) unit->receiveCarriedMaterial(WOOD,{1,2});
            deposit(world.game.map,7,8,WHEAT,3); world.game.map.setMapDiscovered();
            inn->desiredMaxUnitWorking=1; inn->maxUnitWorking=1;
            CHECK_FALSE(inn->subscribeToBringMaterialsStep());
            for (int tick=0;tick<96;++tick) world.game.syncStep(0);
            CHECK(unit->activity!=Unit::ACT_FILLING); CHECK(inn->unitsWorking.empty());
            CHECK(unit->carriedPacketCount()==unsigned(capacity)); CHECK_FALSE(unit->hasCarriedMaterial(WHEAT));
            CHECK(world.game.map.getResource(7,8).amount==3);
            CHECK(world.team->stats.measurements.materialSpillageEvents==0);
        }
    }

    TEST_CASE("initial hiring finds a wanted material in overflow cargo and completes delivery")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto& traits=units["units"][WORKER]["behaviors"];
        traits["cargoCapacity"]=3; traits["cargoKinds"]=2; traits["spillRejectedCargo"]=false;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump())); world.game.configureBuildingCatalog();
        world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
        auto* unit=world.addUnit(WORKER,6,8); auto* inn=world.addBuilding("inn",10,8);
        REQUIRE(unit); REQUIRE(inn);
        unit->receiveCarriedMaterial(WOOD,{}); unit->receiveCarriedMaterial(WHEAT,{});
        inn->desiredMaxUnitWorking=1; inn->maxUnitWorking=1;
        REQUIRE(inn->subscribeToBringMaterialsStep()); CHECK(unit->destinationPurpose==WHEAT);
        CHECK(unit->jobPurpose==UnitJobPurpose::Transport);
        for (int tick=0;tick<4096 && inn->materials[WHEAT]==0;++tick) world.game.syncStep(0);
        CHECK(inn->materials[WHEAT]==1); CHECK(unit->hasCarriedMaterial(WOOD));
        CHECK_FALSE(unit->hasCarriedMaterial(WHEAT)); CHECK(unit->carriedPacketCount()==1);
        CHECK(world.team->stats.measurements.materialSpillageEvents==0);
    }

    TEST_CASE("unit loading rejects mismatched assigned purposes and over-capacity primary packets")
    {
        glob2test::HeadlessGlobals globals;
        for (int malformed=0;malformed<4;++malformed) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            if (malformed>=2) {
                auto catalog=nlohmann::json::parse(world.game.unitCatalog().serialize());
                auto& traits=catalog["units"][WORKER]["behaviors"];
                traits["transport"]=false; traits["cargoCapacity"]=malformed==2?0:1; traits["cargoKinds"]=0;
                world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(catalog.dump())); world.game.configureBuildingCatalog();
            }
            auto* unit=world.addUnit(WORKER,6,8); REQUIRE(unit);
            if (malformed==0) { unit->activity=Unit::ACT_FLAG; unit->jobPurpose=UnitJobPurpose::None; }
            else if (malformed==1) { unit->activity=Unit::ACT_FILLING; unit->destinationPurpose=WHEAT; unit->jobPurpose=UnitJobPurpose::Defend; }
            else { unit->carriedMaterial=WHEAT; unit->carriedPacket={}; }
            auto* storage=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream output(storage);
            unit->save(&output);
            const std::string bytes(storage->getBuffer(),storage->getPosition());
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
            CHECK_THROWS(unit->load(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG));
        }
    }

    TEST_CASE("unusable retained fractional cargo releases its job and continues exactly after saving")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto catalog=nlohmann::json::parse(world.game.unitCatalog().serialize());
        catalog["units"][WORKER]["behaviors"]["spillRejectedCargo"]=false;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(catalog.dump())); world.game.configureBuildingCatalog();
        world.game.gameHeader.setHungerDisabled(true); world.game.gameHeader.setResourceGrowthDisabled(true);
        auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(WORKER,9,8);
        REQUIRE(inn); REQUIRE(unit);
        const Uint16 gid=unit->gid;
        unit->receiveCarriedMaterial(WHEAT,{1,2});
        CHECK_FALSE(unit->hasDeliverableCargo(*inn,WHEAT));
        unit->destinationPurpose=WHEAT; unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
        inn->unitsWorking.push_back(unit); inn->updateCallLists();
        inn->desiredMaxUnitWorking=inn->maxUnitWorking=inn->maxUnitWorkingPreferred=1;
        for (int tick=0;tick<3;++tick) world.game.syncStep(0);
        auto* storage=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream output(storage);
        world.game.save(&output,false,"fractional delivery continuation");
        const std::string bytes(storage->getBuffer(),storage->getPosition());
        std::vector<Uint32> expected;
        for (int tick=0;tick<192;++tick) { world.game.syncStep(0); expected.push_back(trace(world.game)); }
        CHECK(unit->activity==Unit::ACT_RANDOM); CHECK_FALSE(unit->attachedBuilding);
        CHECK(inn->unitsWorking.empty()); CHECK(inn->materials[WHEAT]==0);
        CHECK(unit->carriedPacketCount()==1); CHECK(unit->carriedPacket==MaterialPacket{1,2});
        CHECK(world.team->stats.measurements.materialSpillageEvents==0);
        GameGUI resumed;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size())); input.seekFromStart(0);
        REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
        for (int tick=0;tick<192;++tick) { resumed.game.syncStep(0); CHECK(trace(resumed.game)==expected[tick]); }
        auto* copy=resumed.game.teams[0]->myUnits[Unit::GIDtoID(gid)]; REQUIRE(copy);
        CHECK(copy->activity==Unit::ACT_RANDOM); CHECK(copy->carriedPacket==MaterialPacket{1,2});
    }

    TEST_CASE("target search handles a defender whose effective melee strength saturates")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        world.game.gameHeader.setHungerDisabled(true);
        auto* seeker=world.addUnit(WARRIOR,8,8); auto* defender=world.addUnit(WARRIOR,12,8,1);
        REQUIRE(seeker); REQUIRE(defender);
        defender->experienceLevel=INT_MAX;
        REQUIRE(defender->getRealAttackStrength()==INT_MAX);
        // Permanent discovery does not grant current sight used by targeting.
        world.game.map.setMapDiscovered(12,8,seeker->owner->sharedVisionOther);
        // Keep the target's first movement after acquisition and the seeker's
        // initial strike; its strength still participates in target ranking.
        defender->speed=1; defender->delta=0;
        seeker->handleMovementAttackingAround();
        CHECK(seeker->movement==Unit::MOV_GOING_TARGET);
        CHECK(seeker->validTarget); CHECK(seeker->targetX==12); CHECK(seeker->targetY==8);
        CHECK((seeker->dx!=0 || seeker->dy!=0));
        for(int tick=0;tick<256 && world.team->stats.measurements.shots[GameplayMeasurements::MELEE]==0;++tick) {
            world.game.map.setMapDiscovered(12,8,seeker->owner->sharedVisionOther);
            world.game.syncStep(0);
        }
        CHECK(world.team->stats.measurements.shots[GameplayMeasurements::MELEE]>0);
    }

    TEST_CASE("pending lethal scouts have bounded attraction ranking and resolve death")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto scout=units["units"][WORKER]; scout["key"]="fixture:fragile-scout";
        scout["behaviors"]["explore"]=true; scout["behaviors"]["flagRankingHealth"]=1;
        units["units"].push_back(scout);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
        auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int innType=world.game.buildingsTypes.getFinishedTypeNum("inn");
        buildings["variants"][innType]["properties"]["zonable"]={0,1,0};
        world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
        world.game.gameHeader.setHungerDisabled(true);
        auto* victim=world.addUnit(3,8,8); auto* attacker=world.addUnit(WARRIOR,7,8,1);
        auto* attraction=world.addBuilding("inn",12,8);
        REQUIRE(victim); REQUIRE(attacker); REQUIRE(attraction);
        // Ground exploration needs goals outside the inn's occupied footprint.
        attraction->unitStayRange=5; attraction->dirtyGradients();
        attacker->experienceLevel=INT_MAX;
        for(int hit=0;hit<2;++hit) {
            attacker->action=ATTACK_SPEED; attacker->speed=12; attacker->delta=128;
            attacker->dx=1; attacker->dy=0; attacker->syncStep();
        }
        REQUIRE(victim->hp==INT_MIN); REQUIRE_FALSE(victim->isDead);
        REQUIRE(victim->medical==Unit::MED_FREE);
        attraction->desiredMaxUnitWorking=attraction->maxUnitWorking=1;
        attraction->subscriptionWorkingTimer=32;
        REQUIRE(attraction->subscribeForFlagingStep());
        CHECK(victim->jobPurpose==UnitJobPurpose::Explore);
        victim->delta=255; victim->syncStep();
        CHECK(victim->isDead); CHECK(attraction->unitsWorking.empty());
        CHECK(world.game.map.getGroundUnit(8,8)==NOGUID);
        CHECK(world.team->stats.measurements.deaths[3][GameplayMeasurements::COMBAT]==1);
    }

    TEST_CASE("construction qualification excludes higher-level couriers in live and captured queries")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto courier=units["units"][WORKER]; courier["key"]="fixture:qualified-courier";
        courier["behaviors"]["construct"]=false;
        units["units"].push_back(courier);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump())); world.game.configureBuildingCatalog();
        auto* carrier=world.addUnit(3,8,8,0,3); auto* builder=world.addUnit(WORKER,12,8,0,1);
        REQUIRE(carrier); REQUIRE(builder);
        REQUIRE(carrier->performance[BUILD]>0); REQUIRE(carrier->workerLevel()==3);
        REQUIRE_FALSE(carrier->hasCapability(UnitRuntimeTraits::Construct));
        CHECK(world.team->maxBuildLevel()==1);
        auto captured=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
        CHECK(AIEngine::ObservationQueries::maxBuildLevel(*captured,0)==1);
    }

    TEST_CASE("zero-consumption patients heal without a rebound meal despite retained low hunger")
    {
        glob2test::HeadlessGlobals globals;
        for(int retainedHunger:{75000,-1}) {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto& traits=units["units"][EXPLORER]["behaviors"];
            traits["hungerRate"]=0; traits["healingSpeedQ8"]=65536;
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump())); world.game.configureBuildingCatalog();
            world.game.gameHeader.setUnitUpgradesDisabled(true);
            auto* patient=world.addUnit(EXPLORER,8,8);
            auto* hospital=world.addBuilding("hospital",10,8); auto* inn=world.addBuilding("inn",20,20);
            REQUIRE(patient); REQUIRE(hospital); REQUIRE(inn);
            REQUIRE(patient->hasCapability(UnitRuntimeTraits::ServiceRebound));
            patient->hp=patient->performance[HP]/2; patient->hungry=retainedHunger;
            patient->medical=Unit::MED_DAMAGED; patient->needToRecheckMedical=true;
            inn->materials[WHEAT]=20; inn->updateCallLists();
            hospital->updateCallLists();
            REQUIRE_FALSE(patient->isUnitHungry());
            REQUIRE(world.team->findNearestHeal(patient)==hospital);
            for(int tick=0;tick<4096 && world.team->stats.measurements.healingVisits==0;++tick) world.game.syncStep(0);
            REQUIRE(world.team->stats.measurements.healingVisits==1);
            for(int tick=0;tick<256;++tick) world.game.syncStep(0);
            CHECK(patient->hp==patient->performance[HP]); CHECK(patient->hungry==retainedHunger);
            CHECK(patient->medical==Unit::MED_FREE);
            CHECK(patient->destinationPurpose!=FEED);
            CHECK(inn->materials[WHEAT]==20); CHECK(inn->reservedMaterials[WHEAT]==0);
            CHECK(inn->unitsInside.empty()); CHECK(hospital->unitsInside.empty());
            CHECK_FALSE(patient->serviceResourcesReserved);
        }
    }

    TEST_CASE("configured magic cooldown follows the magic level while imported tables retain legacy indexing")
    {
        glob2test::HeadlessGlobals globals;
        for(bool legacy:{false,true}) {
            CAPTURE(legacy);
            glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
            std::vector<std::array<UnitType,NB_UNIT_LEVELS>> tables;
            auto base=UnitCatalog::legacyMigration();
            for(unsigned type=0;type<base->size();++type)tables.push_back(base->levels(type));
            for(int level=0;level<NB_UNIT_LEVELS;++level)
                tables[EXPLORER][level].magicActionCooldown=11*(level+1);
            // The stock explorer unlocks ground magic at level 3. Give this
            // fixture a ground spell at level 2 so it actually casts at the
            // divergent cooldown level being checked.
            tables[EXPLORER][2].performance[MAGIC_ATTACK_GROUND]=8;
            auto catalog=base->withLegacyLevels(tables,425);
            if(!legacy) {
                auto authored=nlohmann::json::parse(catalog->serialize());
                authored.erase("legacyPerformancePolicies"); authored.erase("legacyLevelMovement");
                catalog=UnitCatalog::deserialize(authored.dump());
            }
            world.game.gameHeader.setUnitCatalog(catalog); world.game.configureBuildingCatalog();
            auto* caster=world.addUnit(EXPLORER,8,8,0,2);
            auto* target=world.addUnit(WORKER,9,8,1);
            REQUIRE(caster); REQUIRE(target);
            caster->level[MAGIC_ATTACK_AIR]=0; caster->performance[MAGIC_ATTACK_AIR]=0;
            caster->level[STOP_FLY]=1;
            REQUIRE(caster->performance[MAGIC_ATTACK_GROUND]==8);
            const int previousHP=target->hp;
            caster->handleMagic();
            REQUIRE(target->hp<previousHP);
            CHECK(caster->magicActionTimeout==(legacy?22:33));
            CHECK(world.team->stats.measurements.shots[GameplayMeasurements::MAGIC]==1);
        }
    }

    TEST_CASE("setup rejects immobile interior patients without publishing partial catalog state")
    {
        glob2test::HeadlessGlobals globals;
        for(auto phase:{Unit::DIS_ENTERING_BUILDING,Unit::DIS_INSIDE,Unit::DIS_EXITING_BUILDING})
            for(bool removeFlags:{false,true}) {
                CAPTURE(phase); CAPTURE(removeFlags);
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
                auto* patient=world.addUnit(WORKER,8,8); auto* inn=world.addBuilding("inn",9,9);
                REQUIRE(patient); REQUIRE(inn);
                inn->materials[WHEAT]=5;
                patient->destinationPurpose=FEED;
                inn->subscribeUnitForInside(patient);
                REQUIRE(patient->serviceResourcesReserved);
                REQUIRE(patient->attachedBuilding==inn);
                patient->posX=9; patient->posY=9; patient->dx=1; patient->dy=1;
                patient->displacement=Unit::DIS_ENTERING_BUILDING;
                patient->movement=Unit::MOV_ENTERING_BUILDING;
                if(phase!=Unit::DIS_ENTERING_BUILDING) patient->handleDisplacement();
                if(phase==Unit::DIS_EXITING_BUILDING) {
                    patient->insideTimeout=0;
                    patient->handleDisplacement();
                }
                REQUIRE(patient->displacement==phase);
                const auto serialized=[](auto& entity) {
                    auto* memory=new GAGCore::MemoryStreamBackend;
                    GAGCore::BinaryOutputStream output(memory); entity.save(&output);
                    return memory->takeContents();
                };
                const auto patientState=serialized(*patient), buildingState=serialized(*inn);
                const auto reserved=inn->reservedMaterials;
                const auto inside=inn->unitsInside;
                const auto legacyRandom=world.game.syncRandom;
                const auto worldRandom=world.game.map.worldRandom.streams;
                const bool randomInitialized=world.game.map.worldRandom.initialized;
                const auto oldCatalog=world.team->race.getCatalog();
                const bool waterOnly=world.game.hasWaterOnlyUnits();
                REQUIRE_FALSE(waterOnly);
                REQUIRE_FALSE(world.game.isUnitTypeAvailable(3));
                auto definitions=nlohmann::json::parse(oldCatalog->serialize());
                if(removeFlags) {
                    for(const char* capability:{"walk","swim","fly"})
                        definitions["units"][WORKER]["behaviors"][capability]=false;
                } else {
                    for(auto& level:definitions["units"][WORKER]["levels"])
                        for(int ability:{WALK,SWIM,FLY}) level["performance"][ability]=0;
                }
                auto swimmer=definitions["units"][WORKER];
                swimmer["key"]="fixture:water-only";
                swimmer["behaviors"]["walk"]=false; swimmer["behaviors"]["fly"]=false;
                swimmer["behaviors"]["swim"]=true;
                for(auto& level:swimmer["levels"]) level["performance"][SWIM]=1;
                definitions["units"].push_back(swimmer);
                world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
                CHECK_THROWS_WITH(world.game.configureBuildingCatalog(),
                    "Unit catalog cannot remove movement during an interior service");
                CHECK(world.team->race.getCatalog()==oldCatalog);
                CHECK_FALSE(world.game.isUnitTypeAvailable(3));
                CHECK(world.game.hasWaterOnlyUnits()==waterOnly);
                CHECK(serialized(*patient)==patientState); CHECK(serialized(*inn)==buildingState);
                CHECK(inn->reservedMaterials==reserved); CHECK(inn->unitsInside==inside);
                CHECK(patient->attachedBuilding==inn);
                CHECK(world.game.syncRandom==legacyRandom);
                CHECK(world.game.map.worldRandom.streams==worldRandom);
                CHECK(world.game.map.worldRandom.initialized==randomInitialized);
                world.game.gameHeader.setUnitCatalog(oldCatalog);
            }
    }

    TEST_CASE("current unit records require idle and service assignments to have no working purpose")
    {
        glob2test::HeadlessGlobals globals;
        for(auto activity:{Unit::ACT_RANDOM,Unit::ACT_UPGRADING})
            for(auto purpose:{UnitJobPurpose::None,UnitJobPurpose::Transport,UnitJobPurpose::Clear,
                              UnitJobPurpose::Explore,UnitJobPurpose::Defend}) {
                CAPTURE(activity); CAPTURE(purpose);
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
                auto* unit=world.addUnit(WORKER,8,8); REQUIRE(unit);
                unit->activity=activity; unit->jobPurpose=purpose;
                if(activity==Unit::ACT_UPGRADING) unit->destinationPurpose=FEED;
                auto* memory=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(memory); unit->save(&output);
                const auto bytes=memory->takeContents();
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
                input.seekFromStart(0);
                if(purpose==UnitJobPurpose::None)
                    CHECK_NOTHROW(unit->load(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG));
                else CHECK_THROWS_WITH(unit->load(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG),
                    "Saved unit assignment does not match its activity");
            }
    }


    TEST_CASE("active water-only cargo preserves queued and pending gradient deadlines [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        // Fixture-local extension includes memberships omitted by the shared helper.
        // Serializing each building covers its private ordered unitsHarvesting and
        // pending orders through its existing save and saveCrossRef traversals.
        const auto authoritativeAudit=[](Game& value) {
            auto result=continuationAudit(value);
            const auto append=[&](const auto& list) {
                result.first.push_back(Uint32(list.size()));
                for (const auto* entity : list) result.first.push_back(entity->gid);
            };
            for (int teamId=0;teamId<value.teamsCount();++teamId) {
                auto* team=value.teams[teamId];
                append(team->liveUnits.entries()); append(team->liveBuildings.entries());
                append(team->canExchange); append(team->stockSuppliers); append(team->directStockSuppliers);
                append(team->combatFlags); append(team->swarms); append(team->turrets);
                append(team->clearingFlags); append(team->virtualBuildings);
                append(team->buildingsWaitingForDestruction); append(team->buildingsToBeDestroyed);
                append(team->buildingsTryToBuildingSiteRoom);
                result.first.push_back(Uint32(team->buildingsNeedingUnits.size()));
                for (const auto& [priority,list] : team->buildingsNeedingUnits) {
                    result.first.push_back(Uint32(priority)); append(list);
                }
                for (auto* building : team->liveBuildings.entries()) {
                    auto* storage=new GAGCore::MemoryStreamBackend;
                    GAGCore::BinaryOutputStream output(storage); building->save(&output);
                    building->saveCrossRef(&output); output.flush();
                    result.second.push_back(storage->takeContents());
                }
                for (auto* unit : team->liveUnits.entries()) {
                    auto* storage=new GAGCore::MemoryStreamBackend;
                    GAGCore::BinaryOutputStream output(storage); unit->saveCrossRef(&output); output.flush();
                    result.second.push_back(storage->takeContents());
                }
            }
            return result;
        };
        enum class Boundary { Queued, PendingAtCapture, PendingHalfway };
        for (unsigned threads : {1u, 4u})
            for (auto boundary : {Boundary::Queued, Boundary::PendingAtCapture, Boundary::PendingHalfway})
                for (bool superseded : {false, true}) {
            CAPTURE(threads); CAPTURE(int(boundary)); CAPTURE(superseded);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
            configure(world);
            auto& game=world.game; auto& map=game.map;
            game.gameHeader.setHungerDisabled(true);
            game.gameHeader.setResourceGrowthDisabled(true);
            game.gameHeader.setBuildingGradientDelay(4);
            auto definitions=nlohmann::json::parse(game.unitCatalog().serialize());
            const auto type=*game.unitCatalog().find("swimmer");
            definitions["units"][type]["behaviors"]["cargoCapacity"]=3;
            definitions["units"][type]["behaviors"]["cargoKinds"]=3;
            game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
            game.configureBuildingCatalog();
            auto buildings=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
            for (auto& variant : buildings["variants"])
                if (variant["semantics"]["feeding"]["enabled"].get<bool>()) {
                    variant["properties"]["maxMaterial"][ALGA]=3;
                    variant["semantics"]["replenishMaterials"].push_back("algae");
                }
            // Install all catalogs before publishing buildings or units.
            game.buildingsTypes.loadSnapshotJson(buildings.dump()); game.configureBuildingCatalog();
            auto* inn=world.addBuilding("inn",10,8); REQUIRE(inn);
            const int shore=inn->posY+inn->type->height, y=shore+1;
            for (int row=shore;row<=shore+2;++row)
                for (int x=4;x<=14;++x) map.paintCell(x,row,WATER);
            deposit(map,7,y,ALGA,3);
            auto* unit=world.addUnit(type,5,y); REQUIRE(unit);
            const auto unitId=Unit::GIDtoID(unit->gid), buildingId=Building::GIDtoID(inn->gid);
            REQUIRE(game.hasWaterOnlyUnits()); REQUIRE(unit->swimClass()==WATER_ONLY_CLASS);
            unit->receiveCarriedMaterial(STONE,{1,2});
            unit->destinationPurpose=ALGA;
            unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
            inn->unitsWorking.push_back(unit); inn->updateCallLists();
            REQUIRE(map.materialAvailable(0,MaterialId::Algae,WATER_ONLY_CLASS,5,y));
            REQUIRE(map.buildingGradient(inn,WATER_ONLY_CLASS));
            const int slot=inn->routeSlot(WATER_ONLY_CLASS,BuildingRoute::Footprint);
            const auto probe=map.coordToIndex(13,y);
            REQUIRE(inn->globalGradient[slot][probe]!=GRADIENT_FORBIDDEN);
            map.configureCompute(threads);
            map.configureGradientPipeline(threads>1?threads-1:0,4);
            map.advanceGradientPipeline(); // one deterministic admission boundary
            map.addForbidden(13,y,0); // serving field must lag the captured refresh
            REQUIRE(map.requestBuildingRefresh(inn,slot));
            REQUIRE(map.buildingGradientPipelineStatus().queued==1);
            REQUIRE(inn->refreshRequested.test(slot));
            if (boundary!=Boundary::Queued) {
                map.stagePeriodicGradientPreparation(); map.preparePendingGradient();
                REQUIRE(map.buildingGradientPipelineStatus().pending==1);
                REQUIRE(map.buildingGradientPipelineStatus().queued==0);
                if (boundary==Boundary::PendingHalfway)
                    for (int tick=0;tick<2;++tick) game.syncStep(0);
            }
            if (superseded) {
                // A newer synchronous lifetime invalidates queued or pending work.
                map.updateGlobalGradient(inn,WATER_ONLY_CLASS,BuildingRoute::Footprint);
                map.finishBuildingGradient(inn,WATER_ONLY_CLASS,BuildingRoute::Footprint);
                REQUIRE_FALSE(inn->refreshRequested.test(slot));
            }
            const auto runtimeBytes=[](Map& value) {
                auto* memory=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(memory);
                value.saveRuntimeState(&output); output.flush();
                return memory->takeContents();
            };
            auto* memory=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(memory);
            game.save(&output,false,"active class7 queued/pending carrier"); output.flush();
            const auto checkpoint=memory->takeContents();
            const auto baselineRuntime=runtimeBytes(map);
            const auto statusAtSave=map.buildingGradientPipelineStatus();
            if (boundary==Boundary::Queued) REQUIRE(statusAtSave.queued==1);
            else REQUIRE(statusAtSave.pending==1);
            if (boundary!=Boundary::Queued) {
                bool actualClass7=false;
                map.gradientRuntime->buildings.visitPending([&](auto& job,unsigned remaining) {
                    const auto& p=job.payload;
                    if (p.buildingId==buildingId && p.swim==WATER_ONLY_CLASS && p.slot==slot) {
                        actualClass7=true;
                        REQUIRE(remaining==(boundary==Boundary::PendingHalfway?2u:4u));
                        REQUIRE((job.superseded || p.epoch!=inn->refreshEpoch[slot])==superseded);
                    }
                });
                REQUIRE(actualClass7); // never substitute an inactive/null class7 row
            }
            GameGUI resumed;
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(checkpoint.data(),checkpoint.size()));
            input.seekFromStart(0); REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
            auto& restored=resumed.game; restored.map.configureCompute(threads);
            restored.map.setGradientWorkerCount(threads>1?threads-1:0); // loader defaults to shared2
            auto* restoredInn=restored.teams[0]->myBuildings[buildingId]; REQUIRE(restoredInn);
            auto* restoredUnit=restored.teams[0]->myUnits[unitId]; REQUIRE(restoredUnit);
            REQUIRE(restoredUnit->swimClass()==WATER_ONLY_CLASS);
            REQUIRE(authoritativeAudit(restored)==authoritativeAudit(game));
            REQUIRE(runtimeBytes(restored.map)==baselineRuntime);
            REQUIRE(restoredInn->refreshRequested.test(slot)==inn->refreshRequested.test(slot));
            if (boundary!=Boundary::Queued) {
                bool reboundClass7=false;
                restored.map.gradientRuntime->buildings.visitPending([&](auto& job,unsigned remaining) {
                    const auto& payload=job.payload;
                    if (payload.buildingId==buildingId && payload.slot==slot) {
                        reboundClass7=true;
                        REQUIRE(payload.swim==WATER_ONLY_CLASS);
                        REQUIRE(remaining==(boundary==Boundary::PendingHalfway?2u:4u));
                        REQUIRE(job.superseded==superseded);
                        if (!superseded) REQUIRE(payload.epoch==restoredInn->refreshEpoch[slot]);
                    }
                });
                REQUIRE(reboundClass7);
            }
            const int advancesUntilPublication=boundary==Boundary::Queued?5:
                boundary==Boundary::PendingHalfway?2:4;
            const auto resumedStatus=restored.map.buildingGradientPipelineStatus();
            REQUIRE(resumedStatus.pending==statusAtSave.pending); REQUIRE(resumedStatus.queued==statusAtSave.queued);
            const auto originalPublished=statusAtSave.published, originalDiscarded=statusAtSave.discarded;
            const auto restoredPublished=resumedStatus.published, restoredDiscarded=resumedStatus.discarded;
            for (int tick=0;tick<512;++tick) {
                CAPTURE(tick);
                game.syncStep(0); restored.syncStep(0);
                REQUIRE(authoritativeAudit(restored)==authoritativeAudit(game));
                // Publish at the original fixed deadline, never at save/load or
                // worker completion. The newer synchronous result is already live
                // in the superseded branch, so only the unsuperseded case is timed.
                if (!superseded && tick<advancesUntilPublication) {
                    const bool due=tick+1==advancesUntilPublication;
                    REQUIRE((inn->globalGradient[slot][probe]==GRADIENT_FORBIDDEN)==due);
                    REQUIRE((restoredInn->globalGradient[slot][probe]==GRADIENT_FORBIDDEN)==due);
                }
                REQUIRE(runtimeBytes(restored.map)==runtimeBytes(map));
                REQUIRE(restored.syncRandom==game.syncRandom);
                REQUIRE(restored.map.worldRandom.streams==map.worldRandom.streams);
                REQUIRE(restored.map.worldRandom.initialized==map.worldRandom.initialized);
                REQUIRE(restored.unitCargo.entries()==game.unitCargo.entries());
                const auto lhs=map.buildingGradientPipelineStatus(), rhs=restored.map.buildingGradientPipelineStatus();
                REQUIRE(lhs.pending==rhs.pending); REQUIRE(lhs.queued==rhs.queued);
                REQUIRE(lhs.published-originalPublished==rhs.published-restoredPublished);
                REQUIRE(lhs.discarded-originalDiscarded==rhs.discarded-restoredDiscarded);
            }
            REQUIRE(inn->materials[ALGA]==3); REQUIRE(restoredInn->materials[ALGA]==3);
            REQUIRE(unit->hasCarriedMaterial(STONE)); REQUIRE(restoredUnit->hasCarriedMaterial(STONE));
            REQUIRE(world.team->stats.measurements.harvested[ALGA]==3);
            REQUIRE(restored.teams[0]->stats.measurements.harvested[ALGA]==3);
            REQUIRE(world.team->stats.measurements.materialSpillageEvents==0);
            REQUIRE(unit->integrity()); REQUIRE(restoredUnit->integrity());
            // Check the selected row actually publishes the new passability value.
            map.finishBuildingGradient(inn,WATER_ONLY_CLASS,BuildingRoute::Footprint);
            restored.map.finishBuildingGradient(restoredInn,WATER_ONLY_CLASS,BuildingRoute::Footprint);
            REQUIRE(inn->globalGradient[slot]);
            REQUIRE(inn->globalGradient[slot][probe]==GRADIENT_FORBIDDEN);
            REQUIRE(restoredInn->globalGradient[slot]);
            REQUIRE(restoredInn->globalGradient[slot][probe]==GRADIENT_FORBIDDEN);
        }
    }

    TEST_CASE("custom carriers resume actual pickup and delivery including active water only fields")
    {
        glob2test::HeadlessGlobals globals;
        for(bool waterOnly:{false,true})
            for(auto phase:{Unit::DIS_GOING_TO_RESOURCE,Unit::DIS_HARVESTING,Unit::DIS_FILLING_BUILDING}) {
                CAPTURE(waterOnly); CAPTURE(int(phase));
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921}); configure(world);
                world.game.gameHeader.setHungerDisabled(true);
                world.game.gameHeader.setResourceGrowthDisabled(true);
                auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
                const auto type=*world.game.unitCatalog().find(waterOnly?"swimmer":"ablation-1");
                definitions["units"][type]["behaviors"]["cargoCapacity"]=3;
                definitions["units"][type]["behaviors"]["cargoKinds"]=3;
                world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
                world.game.configureBuildingCatalog();
                if(waterOnly) {
                    // Stock inns consume wheat. Explicitly author an algae
                    // destination so the swim-only continuation has real demand.
                    auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
                    for(auto& variant:buildings["variants"]) if(variant["semantics"]["feeding"]["enabled"].get<bool>()) {
                        variant["properties"]["maxMaterial"][ALGA]=3;
                        variant["semantics"]["replenishMaterials"].push_back("algae");
                    }
                    world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
                }
                auto* inn=world.addBuilding("inn",10,8); REQUIRE(inn);
                const int shore=inn->posY+inn->type->height;
                if(waterOnly) for(int y=shore;y<=shore+2;++y) for(int x=4;x<=14;++x) world.game.map.paintCell(x,y,WATER);
                const int material=waterOnly?ALGA:WHEAT;
                const int y=waterOnly?shore+1:8;
                deposit(world.game.map,7,y,material,3);
                auto* unit=world.addUnit(type,5,y); REQUIRE(unit);
                unit->receiveCarriedMaterial(STONE,{1,2});
                REQUIRE(inn->materialDeliveryNeed(material)>0);
                unit->destinationPurpose=material;
                unit->subscriptionSuccess(inn,false,false,UnitJobPurpose::Transport);
                inn->unitsWorking.push_back(unit); inn->updateCallLists();
                REQUIRE(world.game.map.materialAvailable(world.team->teamNumber,static_cast<MaterialId>(material),unit->swimClass(),unit->posX,unit->posY));
                REQUIRE(world.game.map.buildingGradient(inn,unit->swimClass()));
                if(waterOnly) REQUIRE(unit->swimClass()==WATER_ONLY_CLASS);
                for(int tick=0;tick<4096 && unit->displacement!=phase;++tick) world.game.syncStep(0);
                REQUIRE(unit->displacement==phase);
                REQUIRE(unit->jobPurpose==UnitJobPurpose::Transport);
                if(phase==Unit::DIS_FILLING_BUILDING) REQUIRE(unit->hasCarriedMaterial(material));
                checkPhaseContinuation(world.game,512);
                CHECK(inn->materials[material]==3);
                CHECK(unit->hasCarriedMaterial(STONE));
                CHECK(world.team->stats.measurements.harvested[material]==3);
                CHECK(world.team->stats.measurements.materialSpillageEvents==0);
                CHECK(unit->integrity());
                if(waterOnly) CHECK(world.game.map.terrainPropertiesAt(unit->posX,unit->posY).swimmable);
            }
    }

    TEST_CASE("custom feeding healing and training resume entry interior and exit with exact reservations")
    {
        glob2test::HeadlessGlobals globals;
        for(int purpose:{int(FEED),int(HEAL),int(WALK)})
            for(auto phase:{Unit::DIS_ENTERING_BUILDING,Unit::DIS_INSIDE,Unit::DIS_EXITING_BUILDING}) {
                CAPTURE(purpose); CAPTURE(int(phase));
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921}); configure(world);
                world.game.gameHeader.setHungerDisabled(true);
                auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
                auto& semantics=buildings["variants"][3]["semantics"];
                semantics["admittedUnits"]={"fast-service"};
                semantics["feeding"]["units"]={"fast-service"};
                semantics["feeding"]["duration"]=3;
                semantics["feeding"]["cost"]={{"food",2}};
                semantics["healing"]={{"enabled",true},{"units",{"fast-service"}},{"duration",3},{"cost",{{"food",2}}}};
                semantics["training"]["walk"]={{"enabled",true},{"units",{"fast-service"}},{"targetLevel",1},{"duration",3},{"cost",{{"food",2}}}};
                world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
                auto* inn=world.addBuilding("inn",10,8);
                auto* unit=world.addUnit(*world.game.unitCatalog().find("fast-service"),6,8);
                REQUIRE(inn); REQUIRE(unit);
                inn->materials[WHEAT]=2;
                unit->hp=purpose==HEAL?50:unit->performance[HP];
                unit->hungry=purpose==FEED?10000:unit->foodCapacity();
                unit->medical=purpose==FEED?Unit::MED_HUNGRY:purpose==HEAL?Unit::MED_DAMAGED:Unit::MED_FREE;
                unit->destinationPurpose=purpose; unit->needToRecheckMedical=purpose==WALK;
                REQUIRE(inn->canOfferService(unit,purpose)); inn->subscribeUnitForInside(unit);
                REQUIRE(unit->serviceResourcesReserved); REQUIRE(inn->reservedMaterials[WHEAT]==2);
                for(int tick=0;tick<4096 && unit->displacement!=phase;++tick) world.game.syncStep(0);
                REQUIRE(unit->displacement==phase);
                REQUIRE(unit->jobPurpose==UnitJobPurpose::None);
                checkPhaseContinuation(world.game,256);
                CHECK_FALSE(unit->serviceResourcesReserved);
                CHECK(inn->reservedMaterials[WHEAT]==0); CHECK(inn->materials[WHEAT]==0);
                CHECK(inn->unitsInside.empty()); CHECK(unit->integrity());
                if(purpose==FEED) CHECK(unit->hungry==unit->foodCapacity());
                else if(purpose==HEAL) CHECK(unit->hp==unit->performance[HP]);
                else { CHECK(unit->level[WALK]==1); CHECK(world.team->stats.measurements.trainingVisits[unit->typeNum]==1); }
            }
    }

    TEST_CASE("mixed combat and fractional regeneration continue with cached clocks cargo and random streams")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921}); configure(world);
        world.game.gameHeader.setHungerDisabled(true);
        auto* fighter=world.addUnit(*world.game.unitCatalog().find("ablation-3"),8,8);
        auto* enemy=world.addUnit(WORKER,9,8,1); REQUIRE(fighter); REQUIRE(enemy);
        fighter->receiveCarriedMaterial(WOOD,{1,2}); fighter->receiveCarriedMaterial(STONE,{});
        fighter->hp=100;
        world.game.map.setMapDiscovered(9,8,world.team->sharedVisionOther);
        fighter->delta=255;
        world.game.syncStep(0);
        REQUIRE(enemy->hp<enemy->performance[HP]);
        REQUIRE(fighter->regenerationRemainder!=0);
        const auto melee=world.team->stats.measurements.shots[GameplayMeasurements::MELEE];
        checkPhaseContinuation(world.game,128);
        CHECK(fighter->hp>100); CHECK(fighter->carriedPacketCount()==2);
        CHECK(world.team->stats.measurements.shots[GameplayMeasurements::MELEE]>melee);
        CHECK(fighter->integrity());
    }

    TEST_CASE("custom starvation and pending conversion preserve complete phase continuation")
    {
        glob2test::HeadlessGlobals globals;
        {
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921}); configure(world);
            auto* starver=world.addUnit(*world.game.unitCatalog().find("starver"),20,20); REQUIRE(starver);
            starver->hungry=0; starver->delta=255; world.game.syncStep(0);
            REQUIRE(starver->hungry==-1000); REQUIRE(starver->hp==197);
            checkPhaseContinuation(world.game,64);
            CHECK(starver->hp<197); CHECK(starver->hungry<-1000); CHECK(starver->integrity());
        }
        {
            glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921}); configure(world);
            auto* ownInn=world.addBuilding("inn",18,8,0,0);
            auto* enemyInn=world.addBuilding("inn",8,8,0,1);
            world.addUnit(WORKER,24,20,0); world.addUnit(WORKER,24,22,1);
            REQUIRE(ownInn); REQUIRE(enemyInn);
            enemyInn->materials[WHEAT]=10; enemyInn->materials[CHERRY]=10; enemyInn->updateCallLists();
            for(int tick=0;tick<160;++tick) world.game.syncStep(0);
            auto* unit=world.addUnit(*world.game.unitCatalog().find("ablation-0"),6,8); REQUIRE(unit);
            world.game.teams[1]->sharedVisionFood|=world.team->me; world.game.teams[1]->allies&=~world.team->me;
            unit->receiveCarriedMaterial(WOOD,{}); unit->receiveCarriedMaterial(STONE,{1,2});
            unit->destinationPurpose=WHEAT; deposit(world.game.map,19,7,WHEAT,3);
            unit->subscriptionSuccess(ownInn,false,false,UnitJobPurpose::Transport); ownInn->unitsWorking.push_back(unit);
            const auto oldGid=unit->gid;
            unit->previousClearingArea=Unit::ClearingAreaClaim{7,7}; unit->previousClearingAreaDistance=1;
            world.game.map.setClearingAreaClaimed(7,7,world.team->teamNumber,oldGid);
            unit->hungry=unit->trigHungryCarrying; unit->medical=Unit::MED_HUNGRY;
            unit->needToRecheckMedical=true; unit->delta=255;
            REQUIRE(world.team->findNearestFood(unit)==enemyInn);
            checkPhaseContinuation(world.game,64);
            CHECK(unit->owner==world.game.teams[1]); CHECK(unit->gid!=oldGid);
            CHECK(unit->configuredVisionRadius==unit->runtimeTraits().visionRadius);
            CHECK_FALSE(world.game.unitCargo.find(oldGid)); CHECK(unit->carriedPacketCount()==2);
            CHECK(world.game.map.isClearingAreaClaimed(7,7,world.team->teamNumber)==NOGUID);
            // Other workers may be recruited while the converted carrier is
            // eating. Its old assignment must be gone without banning new work.
            CHECK(std::find(ownInn->unitsWorking.begin(),ownInn->unitsWorking.end(),unit)==ownInn->unitsWorking.end());
            checkPhaseContinuation(world.game,64);
        }
    }
    TEST_CASE("catalog derived vision cache preserves padding wire state and visibility continuation [save-format]")
    {
        static_assert(std::is_trivially_copyable_v<UnitState>);
        static_assert(std::is_standard_layout_v<UnitState>);
        static_assert(offsetof(UnitState,configuredVisionRadius)==offsetof(UnitState,underAttackTimer)+1);
        static_assert(offsetof(UnitState,hp)==offsetof(UnitState,underAttackTimer)+4);
        // Native 64-bit layout guards; narrower targets still check the exact padding gap.
        static_assert(sizeof(void*)!=8 || sizeof(UnitState)==360);
        static_assert(sizeof(void*)!=8 || sizeof(Unit)==440);
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.clearImmobile=true,.header=true,.seed=4921});
        auto definitions=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto custom=definitions["units"][WORKER]; custom["key"]="fixture:vision-cache";
        definitions["units"].push_back(custom);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
        world.game.configureBuildingCatalog();
        auto* unit=world.addUnit(3,30,30); REQUIRE(unit);
        CHECK(unit->configuredVisionRadius==unit->runtimeTraits().visionRadius);
        for(int radius:{0,1,3,32}) {
            CAPTURE(radius);
            definitions["units"][3]["behaviors"]["visionRadius"]=radius;
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
            world.game.configureBuildingCatalog(); // Actual supported live setup rebinding.
            REQUIRE(unit->configuredVisionRadius==radius);
            unit->resetAtLevel(1); REQUIRE(unit->configuredVisionRadius==radius);
            const auto serialized=[&] {
                auto* memory=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(memory); unit->save(&output);
                return memory->takeContents();
            };
            std::vector<Uint32> checksumBefore,checksumPoisoned;
            const auto checksum=unit->checkSum(&checksumBefore);
            const auto bytes=serialized();
            unit->configuredVisionRadius=Uint8((radius+1)%33);
            CHECK(serialized()==bytes); // Derived cache adds no saved field.
            CHECK(unit->checkSum(&checksumPoisoned)==checksum);
            CHECK(checksumPoisoned==checksumBefore); // Adds no checksum field.
            unit->configuredVisionRadius=Uint8(radius);
            {
                GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
                input.seekFromStart(0);
                Unit restored(&input,world.team,FILE_FORMAT_VERSION_UNIT_CATALOG);
                REQUIRE(restored.configuredVisionRadius==radius);
            }
            auto frozen=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
            const auto* observed=frozen->unitSlots(0)[Unit::GIDtoID(unit->gid)]; REQUIRE(observed);
            CHECK(observed->configuredVisionRadius==radius);
            unit->configuredVisionRadius=Uint8((radius+1)%33);
            CHECK(observed->configuredVisionRadius==radius);
            unit->configuredVisionRadius=Uint8(radius);

            world.game.map.unsetMapDiscovered();
            unit->delta=255;
            { glob2test::BoundGameRandom bound(world.game); unit->syncStep(); }
            // Independent rectangular torus oracle around the action's final position.
            // This checks actual discoveries, including radius zero and wrapping at 32.
            const auto& map=world.game.map;
            for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x) {
                const int dx=std::min((x-unit->posX)&map.wMask,(unit->posX-x)&map.wMask);
                const int dy=std::min((y-unit->posY)&map.hMask,(unit->posY-y)&map.hMask);
                const bool expected=dx<=radius && dy<=radius;
                CHECK(map.isMapDiscovered(x,y,world.team->me)==expected);
            }
            auto* memory=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(memory);
            world.game.save(&output,false,"derived vision continuation");
            const auto checkpoint=memory->takeContents();
            GameGUI resumed;
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(checkpoint.data(),checkpoint.size()));
            input.seekFromStart(0); REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
            const auto* loaded=resumed.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)]; REQUIRE(loaded);
            REQUIRE(loaded->configuredVisionRadius==radius);
            REQUIRE(resumed.game.unitCatalog().digest()==world.game.unitCatalog().digest());
            for(int tick=0;tick<32;++tick) {
                CAPTURE(tick);
                world.game.syncStep(0); resumed.game.syncStep(0);
                CHECK(unit->configuredVisionRadius==radius); CHECK(loaded->configuredVisionRadius==radius);
                REQUIRE(continuationAudit(world.game)==continuationAudit(resumed.game));
                REQUIRE(world.game.map.mapDiscovered==resumed.game.map.mapDiscovered);
                REQUIRE(world.game.syncRandom==resumed.game.syncRandom);
                REQUIRE(world.game.unitCargo.entries()==resumed.game.unitCargo.entries());
            }
        }
    }
    TEST_CASE("training rejects loss of the last movement mode before reservation or direct mutation")
    {
        glob2test::HeadlessGlobals globals;
        for(bool modifiedBuiltin:{false,true}) {
        CAPTURE(modifiedBuiltin);
        const std::string key=modifiedBuiltin?"worker":"training-loss";
        const int type=modifiedBuiltin?WORKER:3;
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto custom=units["units"][WORKER]; custom["key"]=key;
        custom["behaviors"]["swim"]=false;
        custom["behaviors"]["hungerRate"]=0;
        custom["levels"][1]["performance"][WALK]=0;
        if(modifiedBuiltin)units["units"][WORKER]=custom; else units["units"].push_back(custom);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
        auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& semantics=buildings["variants"][3]["semantics"];
        semantics["admittedUnits"]={key};
        semantics["training"]=nlohmann::json::object();
        semantics["training"]["walk"]={{"enabled",true},{"units",{key}},{"targetLevel",1},{"duration",3},{"cost",{{"food",2}}}};
        world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
        auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(type,6,8);
        REQUIRE(inn); REQUIRE(unit); inn->materials[WHEAT]=2;
        const auto random=unit->entityRandom.exportState(); const int walk=unit->performance[WALK];
        CHECK(unit->needsTraining(inn->type->semantics.training[WALK],WALK));
        CHECK_FALSE(inn->canOfferService(unit,WALK));
        CHECK(world.team->findBestUpgrade(unit)==nullptr);
        auto captured=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
        REQUIRE(captured->unitAtSlot(unit->gid)); REQUIRE(captured->buildingAtSlot(inn->gid));
        CHECK_FALSE(AIEngine::ObservationQueries::trainingVisitSafe(*captured,*captured->unitAtSlot(unit->gid),*captured->buildingAtSlot(inn->gid),WALK));
        CHECK_FALSE(unit->applyTraining(inn->type->semantics.training[WALK],WALK));
        CHECK(unit->level[WALK]==0); CHECK(unit->performance[WALK]==walk);
        CHECK(unit->entityRandom.exportState()==random);
        CHECK_FALSE(unit->serviceResourcesReserved); CHECK(inn->reservedMaterials[WHEAT]==0); CHECK(inn->materials[WHEAT]==2);
        checkPhaseContinuation(world.game,64);
        CHECK(unit->integrity()); CHECK(inn->unitsInside.empty());
        }
    }

    TEST_CASE("parallel movement transfer checks exit terrain but ignores temporary occupants and resumes atomically")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto custom=units["units"][WORKER]; custom["key"]="training-transfer";
        custom["behaviors"]["hungerRate"]=0; custom["behaviors"]["clearIdle"]=false;
        custom["behaviors"]["adjacentClearInterrupt"]=false;
        custom["levels"][0]["performance"][SWIM]=0;
        custom["levels"][1]["performance"][WALK]=0; custom["levels"][1]["performance"][SWIM]=16;
        units["units"].push_back(custom);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
        auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& semantics=buildings["variants"][3]["semantics"];
        semantics["admittedUnits"]={"training-transfer"}; semantics["trainingInParallel"]=true;
        semantics["training"]=nlohmann::json::object();
        semantics["training"]["walk"]={{"enabled",true},{"units",{"training-transfer"}},{"targetLevel",1},{"duration",3},{"cost",{{"food",2}}}};
        semantics["training"]["swim"]={{"enabled",true},{"units",{"training-transfer"}},{"targetLevel",1},{"duration",3},{"cost",{{"food",3}}}};
        buildings["variants"][3]["properties"]["insideSpeed"]=256;
        world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
        auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(3,6,8);
        REQUIRE(inn); REQUIRE(unit); inn->materials[WHEAT]=5;
        CHECK_FALSE(inn->canOfferService(unit,WALK)); // swimming-only cannot exit a dry perimeter
        CHECK_FALSE(unit->applyTraining(inn->type->semantics.training[WALK],WALK));
        // Paint an actual exit and a following cell, away from the incoming land route.
        for(int y=inn->posY-2;y<inn->posY;++y) for(int x=inn->posX;x<inn->posX+inn->type->width;++x)
            world.game.map.paintCell(x,y,WATER);
        REQUIRE(world.game.map.terrainPropertiesAt(inn->posX,inn->posY-1).swimmable);
        auto* occupant=world.addUnit(3,inn->posX,inn->posY-1,0,1); REQUIRE(occupant);
        REQUIRE(inn->canOfferService(unit,WALK));
        const auto grants=unit->trainingVisitCourses(*inn,WALK); REQUIRE(grants);
        CHECK((*grants&((1u<<WALK)|(1u<<SWIM)))==((1u<<WALK)|(1u<<SWIM)));
        auto captured=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
        REQUIRE(captured->unitAtSlot(unit->gid)); REQUIRE(captured->buildingAtSlot(inn->gid));
        CHECK(AIEngine::ObservationQueries::trainingVisitSafe(*captured,*captured->unitAtSlot(unit->gid),*captured->buildingAtSlot(inn->gid),WALK));
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(occupant->posX,occupant->posY,Game::DEL_GROUND_UNIT));
        unit->destinationPurpose=WALK; unit->needToRecheckMedical=true; inn->subscribeUnitForInside(unit);
        REQUIRE(unit->serviceResourcesReserved); REQUIRE(inn->reservedMaterials[WHEAT]==5);
        for(int tick=0;tick<4096 && unit->displacement!=Unit::DIS_INSIDE;++tick) world.game.syncStep(0);
        REQUIRE(unit->displacement==Unit::DIS_INSIDE);
        checkPhaseContinuation(world.game,256);
        CHECK(unit->level[WALK]==1); CHECK(unit->level[SWIM]==1);
        CHECK(unit->performance[WALK]==0); CHECK(unit->performance[SWIM]==16);
        CHECK(world.game.map.terrainPropertiesAt(unit->posX,unit->posY).swimmable);
        CHECK(inn->unitsInside.empty()); CHECK_FALSE(unit->serviceResourcesReserved);
        CHECK(inn->reservedMaterials[WHEAT]==0); CHECK(inn->materials[WHEAT]==0);
        CHECK(world.team->stats.measurements.trainingVisits[3]==1); CHECK(unit->integrity());
    }

    TEST_CASE("restored unsafe ongoing training exits without charging materials or changing levels")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
        auto custom=units["units"][WORKER]; custom["key"]="training-restored";
        custom["behaviors"]["swim"]=false; custom["behaviors"]["hungerRate"]=0;
        custom["behaviors"]["clearIdle"]=false; custom["behaviors"]["adjacentClearInterrupt"]=false;
        units["units"].push_back(custom);
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
        auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& semantics=buildings["variants"][3]["semantics"];
        semantics["admittedUnits"]={"training-restored"}; semantics["training"]=nlohmann::json::object();
        semantics["training"]["walk"]={{"enabled",true},{"units",{"training-restored"}},{"targetLevel",1},{"duration",100},{"cost",{{"food",2}}}};
        buildings["variants"][3]["properties"]["insideSpeed"]=256;
        world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
        auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(3,6,8);
        REQUIRE(inn); REQUIRE(unit); inn->materials[WHEAT]=2;
        unit->destinationPurpose=WALK; unit->needToRecheckMedical=true; inn->subscribeUnitForInside(unit);
        REQUIRE(unit->serviceResourcesReserved);
        for(int tick=0;tick<4096 && unit->displacement!=Unit::DIS_INSIDE;++tick) world.game.syncStep(0);
        REQUIRE(unit->displacement==Unit::DIS_INSIDE); REQUIRE(inn->reservedMaterials[WHEAT]==2);
        // A changed future level table is allowed during a visit: current clocks,
        // learnability, selection and reservation costs are all still unchanged.
        units["units"][3]["levels"][1]["performance"][WALK]=0;
        world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump())); world.game.configureBuildingCatalog();
        CHECK_FALSE(unit->trainingVisitSafe(*inn,WALK));
        unit->insideTimeout=0; unit->delta=255;
        checkPhaseContinuation(world.game,256);
        CHECK(unit->level[WALK]==0); CHECK(unit->performance[WALK]>0);
        CHECK(world.team->stats.measurements.trainingVisits[3]==0);
        CHECK(world.team->stats.measurements.abilityGains[3][WALK]==0);
        CHECK_FALSE(unit->serviceResourcesReserved); CHECK(inn->reservedMaterials[WHEAT]==0);
        CHECK(inn->materials[WHEAT]==2); CHECK(inn->unitsInside.empty()); CHECK(unit->integrity());
    }

    TEST_CASE("imported movement tables retain historical course admission and direct grants")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
        auto base=UnitCatalog::legacyMigration(); std::vector<std::array<UnitType,NB_UNIT_LEVELS>> tables;
        for(unsigned type=0;type<base->size();++type)tables.push_back(base->levels(type));
        tables[WORKER][1].performance[WALK]=0;
        for(auto& level:tables[WORKER])level.performance[SWIM]=0;
        world.game.gameHeader.setUnitCatalog(base->withLegacyLevels(tables,425));
        auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& semantics=buildings["variants"][3]["semantics"];
        semantics["training"]=nlohmann::json::object();
        semantics["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",3},{"cost",nlohmann::json::object()}};
        world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
        auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(WORKER,6,8); REQUIRE(inn); REQUIRE(unit);
        REQUIRE(unit->hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies));
        CHECK(inn->canOfferService(unit,WALK));
        CHECK(unit->applyTraining(inn->type->semantics.training[WALK],WALK));
        CHECK(unit->level[WALK]==1); CHECK(unit->performance[WALK]==0);
    }

    TEST_CASE("parallel construction qualification captures all courses before grants and preserves immobile definitions")
    {
        glob2test::HeadlessGlobals globals;
        for(bool immobile:{false,true}) {
            CAPTURE(immobile);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto custom=units["units"][WORKER]; custom["key"]="training-qualification";
            custom["behaviors"]["hungerRate"]=0; custom["behaviors"]["clearIdle"]=false;
            custom["behaviors"]["adjacentClearInterrupt"]=false;
            if(immobile) {
                custom["behaviors"]["walk"]=false; custom["behaviors"]["swim"]=false;
                for(auto& level:custom["levels"]) { level["performance"][WALK]=0; level["performance"][SWIM]=0; }
            }
            units["units"].push_back(custom);
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
            auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            auto& semantics=buildings["variants"][3]["semantics"];
            semantics["admittedUnits"]={"training-qualification"}; semantics["trainingInParallel"]=true;
            semantics["training"]=nlohmann::json::object();
            semantics["training"]["build"]={{"enabled",true},{"units",{"training-qualification"}},{"targetLevel",0},{"constructionLevel",2},{"duration",3},{"cost",{{"food",2}}}};
            semantics["training"]["harvest"]={{"enabled",true},{"units",{"training-qualification"}},{"targetLevel",0},{"constructionLevel",2},{"duration",3},{"cost",{{"food",3}}}};
            buildings["variants"][3]["properties"]["insideSpeed"]=256;
            world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
            auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(3,6,8); REQUIRE(inn); REQUIRE(unit);
            inn->materials[WHEAT]=5;
            const int build=unit->performance[BUILD],harvest=unit->performance[HARVEST];
            REQUIRE(unit->constructionLevel==0); REQUIRE(inn->canOfferService(unit,BUILD));
            const auto courses=unit->trainingVisitCourses(*inn,BUILD); REQUIRE(courses);
            CHECK((*courses&((1u<<BUILD)|(1u<<HARVEST)))==((1u<<BUILD)|(1u<<HARVEST)));
            if(immobile) {
                // Direct construction grants need no movement and remain useful
                // to stationary definitions created by developer-authored setups.
                CHECK(unit->applyTraining(inn->type->semantics.training[BUILD],BUILD));
                CHECK(unit->constructionLevel==2); CHECK(unit->performance[WALK]==0); CHECK(unit->performance[SWIM]==0);
            } else {
                unit->destinationPurpose=BUILD; unit->needToRecheckMedical=true; inn->subscribeUnitForInside(unit);
                REQUIRE(inn->reservedMaterials[WHEAT]==5);
                for(int tick=0;tick<4096 && unit->displacement!=Unit::DIS_INSIDE;++tick) world.game.syncStep(0);
                REQUIRE(unit->displacement==Unit::DIS_INSIDE);
                checkPhaseContinuation(world.game,256);
                CHECK(unit->constructionLevel==2); CHECK(inn->materials[WHEAT]==0); CHECK(inn->reservedMaterials[WHEAT]==0);
                CHECK(inn->unitsInside.empty()); CHECK_FALSE(unit->serviceResourcesReserved);
                CHECK(world.team->stats.measurements.trainingVisits[3]==1);
            }
            CHECK(unit->level[BUILD]==0); CHECK(unit->level[HARVEST]==0);
            CHECK(unit->performance[BUILD]==build); CHECK(unit->performance[HARVEST]==harvest); CHECK(unit->integrity());
        }
    }

    TEST_CASE("parallel movement losses are assessed together before admission with a surviving mode alternative")
    {
        glob2test::HeadlessGlobals globals;
        for(bool airborneAlternative:{false,true}) {
            CAPTURE(airborneAlternative);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
            if(airborneAlternative) {
                // Import terrain before creating entities, using the detached
                // authoring setup also used by TerrainRuntime fixtures.
                world.game.map.game=nullptr;
                world.game.map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"fixture:training-no-fly","name":"No fly","base":"grass","properties":{"flyable":false},"appearance":"grass"}]})");
                world.game.map.setGame(&world.game);
            }
            auto units=nlohmann::json::parse(world.game.unitCatalog().serialize());
            auto custom=units["units"][WORKER]; custom["key"]="training-bundle";
            custom["behaviors"]["hungerRate"]=0; custom["behaviors"]["clearIdle"]=false;
            custom["behaviors"]["adjacentClearInterrupt"]=false;
            custom["behaviors"]["fly"]=airborneAlternative;
            for(auto& level:custom["levels"])level["performance"][FLY]=airborneAlternative?16:0;
            custom["levels"][0]["performance"][SWIM]=16;
            custom["levels"][1]["performance"][WALK]=0; custom["levels"][1]["performance"][SWIM]=0;
            units["units"].push_back(custom);
            world.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(units.dump()));
            auto buildings=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            auto& semantics=buildings["variants"][3]["semantics"];
            semantics["admittedUnits"]={"training-bundle"}; semantics["trainingInParallel"]=true;
            semantics["training"]=nlohmann::json::object();
            semantics["training"]["walk"]={{"enabled",true},{"units",{"training-bundle"}},{"targetLevel",1},{"duration",3},{"cost",{{"food",2}}}};
            semantics["training"]["swim"]={{"enabled",true},{"units",{"training-bundle"}},{"targetLevel",1},{"duration",3},{"cost",{{"food",3}}}};
            buildings["variants"][3]["properties"]["insideSpeed"]=256;
            world.game.buildingsTypes.loadSnapshotJson(buildings.dump()); world.game.configureBuildingCatalog();
            auto* inn=world.addBuilding("inn",10,8); auto* unit=world.addUnit(3,6,8); REQUIRE(inn); REQUIRE(unit);
            inn->materials[WHEAT]=5;
            const int walk=unit->performance[WALK],swim=unit->performance[SWIM];
            REQUIRE(walk>0); REQUIRE(swim>0);
            CHECK(inn->canOfferService(unit,WALK)==airborneAlternative);
            CHECK(inn->canOfferService(unit,SWIM)==airborneAlternative);
            auto captured=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
            REQUIRE(captured->unitAtSlot(unit->gid)); REQUIRE(captured->buildingAtSlot(inn->gid));
            CHECK(AIEngine::ObservationQueries::trainingVisitSafe(*captured,*captured->unitAtSlot(unit->gid),*captured->buildingAtSlot(inn->gid),WALK)==airborneAlternative);
            CHECK(unit->level[WALK]==0); CHECK(unit->level[SWIM]==0);
            CHECK(unit->performance[WALK]==walk); CHECK(unit->performance[SWIM]==swim);
            CHECK_FALSE(unit->serviceResourcesReserved); CHECK(inn->reservedMaterials[WHEAT]==0); CHECK(inn->materials[WHEAT]==5);
            if(airborneAlternative) {
                unit->destinationPurpose=WALK; unit->needToRecheckMedical=true; inn->subscribeUnitForInside(unit);
                REQUIRE(inn->reservedMaterials[WHEAT]==5);
                for(int tick=0;tick<4096 && unit->displacement!=Unit::DIS_INSIDE;++tick)world.game.syncStep(0);
                REQUIRE(unit->displacement==Unit::DIS_INSIDE);
                // Close only the outside vertex ring after entry. Every ground
                // exit perimeter cell becomes non-flyable, while every footprint
                // cell remains a valid actual air exit.
                const auto noFly=world.game.map.terrainRegistry().find("fixture:training-no-fly"); REQUIRE(noFly);
                auto& map=world.game.map;
                for(int y=inn->posY-1;y<=inn->posY+inn->type->height+1;++y)
                    for(int x=inn->posX-1;x<=inn->posX+inn->type->width+1;++x)
                        if(y==inn->posY-1 || y==inn->posY+inn->type->height+1 || x==inn->posX-1 || x==inn->posX+inn->type->width+1)
                            map.setVertexTerrain(x,y,*noFly);
                for(int y=inn->posY-1;y<=inn->posY+inn->type->height;++y)
                    for(int x=inn->posX-1;x<=inn->posX+inn->type->width;++x)
                        if(y==inn->posY-1 || y==inn->posY+inn->type->height || x==inn->posX-1 || x==inn->posX+inn->type->width)
                            REQUIRE_FALSE(map.terrainPropertiesAt(x,y).flyable);
                for(int y=inn->posY;y<inn->posY+inn->type->height;++y)
                    for(int x=inn->posX;x<inn->posX+inn->type->width;++x)
                        REQUIRE(map.terrainPropertiesAt(x,y).flyable);
                REQUIRE(unit->trainingVisitSafe(*inn,WALK));
                captured=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
                CHECK(AIEngine::ObservationQueries::trainingVisitSafe(*captured,*captured->unitAtSlot(unit->gid),*captured->buildingAtSlot(inn->gid),WALK));
                checkPhaseContinuation(world.game,256);
                CHECK(unit->performance[WALK]==0); CHECK(unit->performance[SWIM]==0); CHECK(unit->performance[FLY]==16);
                CHECK(unit->level[WALK]==1); CHECK(unit->level[SWIM]==1);
                CHECK(inn->materials[WHEAT]==0); CHECK(inn->reservedMaterials[WHEAT]==0); CHECK(inn->unitsInside.empty());
                CHECK(world.team->stats.measurements.trainingVisits[3]==1); CHECK(unit->integrity());
            } else {
                // Each grant alone preserves the other mode. The service must
                // reject their combined effect rather than applying them in order.
                auto* isolated=world.addUnit(3,6,9); REQUIRE(isolated);
                REQUIRE(isolated->applyTraining(inn->type->semantics.training[SWIM],SWIM));
                CHECK(isolated->performance[WALK]==walk); CHECK(isolated->performance[SWIM]==0);
                REQUIRE(unit->applyTraining(inn->type->semantics.training[WALK],WALK));
                CHECK(unit->performance[SWIM]==swim);
                CHECK_FALSE(unit->applyTraining(inn->type->semantics.training[SWIM],SWIM));
                CHECK(unit->level[SWIM]==0); CHECK(unit->performance[SWIM]==swim);
            }
        }
    }

// Prospective insertion inside existing UnitCustomization suite; no new includes.
// Not applied, compiled, executed or measured.
    TEST_CASE("enemy tower live index matches full slot oracle across lifecycle and save continuation [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=3,.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        auto& game=world.game;
        game.map.game=nullptr;
        game.map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"fixture:shot-blocker","name":"Shot blocker","base":"grass","properties":{"projectileBlocks":true},"appearance":"grass"}]})");
        game.map.setGame(&game);
        auto buildings=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
        const int towerType=game.buildingsTypes.getTypeNum("defencetower",0,false);
        const int siteType=game.buildingsTypes.getTypeNum("defencetower",0,true);
        REQUIRE(towerType>=0); REQUIRE(siteType>=0);
        for(const int type:{towerType,siteType}) {
            buildings["variants"][type]["properties"]["shootingRange"]=3;
            // The original avoidance predicate cares about range even without
            // launchable ammunition or damaging shots. Preserve that policy.
            buildings["variants"][type]["semantics"]["projectileDamage"]={0,0,0};
        }
        game.buildingsTypes.loadSnapshotJson(buildings.dump()); game.configureBuildingCatalog();
        game.gameHeader.setHungerDisabled(true); game.gameHeader.setResourceGrowthDisabled(true);
        auto* observer=world.addUnit(EXPLORER,28,28); REQUIRE(observer);
        world.addUnit(WORKER,28,24,1); world.addUnit(WORKER,26,24,2);
        auto* gap=world.addBuilding("inn",8,20,0,1); REQUIRE(gap);
        auto* tower=world.addBuilding("defencetower",8,8,0,1); REQUIRE(tower);
        auto* other=world.addBuilding("defencetower",22,8,0,2); REQUIRE(other);
        auto* friendly=world.addBuilding("defencetower",8,26,0,0); REQUIRE(friendly);
        auto* wrapped=world.addBuilding("defencetower",30,4,0,1); REQUIRE(wrapped);
        auto* site=game.addBuilding(20,20,siteType,1); REQUIRE(site);
        game.map.setBuilding(site->posX,site->posY,site->type->width,site->type->height,site->gid);
        REQUIRE(site->type->isBuildingSite); REQUIRE(site->runtime->shootingRange==3);
        REQUIRE(tower->bullets==0); REQUIRE(tower->runtime->interaction(observer->typeNum).projectileDamage==0);
        const auto reference=[](const Unit& unit,int x,int y) {
            // Retain the exact original full fixed-slot oracle, including its
            // non-ALIVE, zero-damage, range+1 and null-slot behavior.
            for(int team=0;team<Team::MAX_COUNT;++team) {
                const Team* enemy=unit.owner->game->teams[team];
                if(enemy && (unit.owner->enemies&enemy->me))
                    for(int slot=0;slot<Building::MAX_COUNT;++slot) {
                        const Building* building=enemy->myBuildings[slot];
                        if(building && building->runtime->shootingRange>0 &&
                            unit.owner->map->warpDistMax(building->posX,building->posY,x,y)<=building->runtime->shootingRange+1 &&
                            building->hasClearShotTo(x,y))return true;
                    }
            }
            return false;
        };
        const auto audit=[&](Game& candidate,Unit& unit) {
            const auto state=continuationAudit(candidate);
            const auto random=candidate.syncRandom; const auto entity=unit.entityRandom.exportState();
            for(int team=0;team<candidate.teamsCount();++team) {
                REQUIRE(candidate.teams[team]->liveBuildings.matches(candidate.teams[team]->myBuildings,Building::MAX_COUNT));
                REQUIRE(std::is_sorted(candidate.teams[team]->liveBuildings.slots().begin(),candidate.teams[team]->liveBuildings.slots().end()));
            }
            std::vector<Uint8> truth;
            for(int y=0;y<candidate.map.getH();++y)for(int x=0;x<candidate.map.getW();++x) {
                CAPTURE(x); CAPTURE(y);
                const bool expected=reference(unit,x,y);
                REQUIRE(unit.locationIsInEnemyGuardTowerRange(x,y)==expected);
                truth.push_back(expected);
            }
            REQUIRE(candidate.syncRandom==random); REQUIRE(unit.entityRandom.exportState()==entity);
            REQUIRE(continuationAudit(candidate)==state);
            return truth;
        };
        observer->owner->enemies=game.teams[1]->me;
        auto truth=audit(game,*observer);
        REQUIRE(std::find(truth.begin(),truth.end(),Uint8(1))!=truth.end());
        REQUIRE(std::find(truth.begin(),truth.end(),Uint8(0))!=truth.end());
        CHECK(observer->locationIsInEnemyGuardTowerRange(12,8)); // Exact range+1 boundary.
        CHECK_FALSE(observer->locationIsInEnemyGuardTowerRange(13,8));
        CHECK_FALSE(observer->locationIsInEnemyGuardTowerRange(22,8)); // Nonenemy tower.
        CHECK_FALSE(observer->locationIsInEnemyGuardTowerRange(8,26)); // Friendly tower.
        CHECK(observer->locationIsInEnemyGuardTowerRange(20,20)); // Range-bearing site.
        CHECK(observer->locationIsInEnemyGuardTowerRange(0,4)); // Wrapped range/footprint.
        observer->owner->enemies|=game.teams[2]->me;
        CHECK(observer->locationIsInEnemyGuardTowerRange(22,8)); audit(game,*observer);
        const auto blocker=game.map.terrainRegistry().find("fixture:shot-blocker"); REQUIRE(blocker);
        game.map.paintCell(12,8,*blocker); REQUIRE(game.map.terrainPropertiesAt(12,8).projectileBlocks);
        CHECK_FALSE(observer->locationIsInEnemyGuardTowerRange(12,8)); audit(game,*observer);
        // Actual deletion leaves a gap; publication reuses it ahead of the
        // older tower. The live index must continue to visit ascending slots.
        const auto gapGid=gap->gid;
        gap->kill(); audit(game,*observer);
        game.teams[1]->syncStep();
        REQUIRE(game.teams[1]->myBuildings[Building::GIDtoID(gapGid)]==nullptr);
        audit(game,*observer);
        auto* reused=world.addBuilding("inn",8,20,0,1); REQUIRE(reused); CHECK(reused->gid==gapGid);
        audit(game,*observer);
        const auto towerGid=tower->gid;
        tower->kill(); REQUIRE(tower->buildingState==Building::DEAD);
        REQUIRE(game.teams[1]->myBuildings[Building::GIDtoID(towerGid)]==tower);
        CHECK(observer->locationIsInEnemyGuardTowerRange(8,8)); // DEAD still published until collection.
        audit(game,*observer);
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory); game.save(&output,false,"tower live slot continuation");
        const auto bytes=memory->takeContents();
        GameGUI resumed;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0); REQUIRE(resumed.game.load(&input)); resumed.game.setWaitingOnMask(0);
        auto* loaded=resumed.game.teams[0]->myUnits[Unit::GIDtoID(observer->gid)]; REQUIRE(loaded);
        REQUIRE(audit(game,*observer)==audit(resumed.game,*loaded));
        REQUIRE(continuationAudit(game)==continuationAudit(resumed.game));
        for(int tick=0;tick<16;++tick) {
            CAPTURE(tick);
            game.syncStep(0); resumed.game.syncStep(0);
            REQUIRE(audit(game,*observer)==audit(resumed.game,*loaded));
            REQUIRE(continuationAudit(game)==continuationAudit(resumed.game));
            REQUIRE(game.syncRandom==resumed.game.syncRandom);
        }
        CHECK(game.teams[1]->myBuildings[Building::GIDtoID(towerGid)]==nullptr);
        CHECK(resumed.game.teams[1]->myBuildings[Building::GIDtoID(towerGid)]==nullptr);
    }

    TEST_CASE("enemy tower live index retains last slot and full occupied table oracle")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true,.seed=4921});
        auto definitions=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int towerType=world.game.buildingsTypes.getTypeNum("defencetower",0,false); REQUIRE(towerType>=0);
        definitions["variants"][towerType]["properties"]["shootingRange"]=3;
        world.game.buildingsTypes.loadSnapshotJson(definitions.dump()); world.game.configureBuildingCatalog();
        auto* observer=world.addUnit(EXPLORER,28,28); REQUIRE(observer);
        auto* enemy=world.game.teams[1]; observer->owner->enemies=enemy->me;
        const int filler=world.game.buildingsTypes.getTypeNum("warflag",0,false); REQUIRE(filler>=0);
        REQUIRE(world.game.buildingsTypes.get(filler)->isVirtual);
        REQUIRE(world.game.buildingsTypes.getRuntime(filler)->shootingRange==0);
        // Actual Game publication fills every slot. Virtual zero-range flags
        // occupy no map footprint; the final physical tower must be observed.
        for(int slot=0;slot<Building::MAX_COUNT-1;++slot) {
            auto* flag=world.game.addBuilding(slot&31,(slot>>5)&31,filler,1); REQUIRE(flag);
            REQUIRE(Building::GIDtoID(flag->gid)==slot);
        }
        auto* tower=world.addBuilding("defencetower",8,8,0,1); REQUIRE(tower);
        REQUIRE(Building::GIDtoID(tower->gid)==Building::MAX_COUNT-1);
        REQUIRE(enemy->liveBuildings.size()==Building::MAX_COUNT);
        REQUIRE(enemy->liveBuildings.matches(enemy->myBuildings,Building::MAX_COUNT));
        const auto random=world.game.syncRandom; const auto entity=observer->entityRandom.exportState();
        int positives=0,negatives=0;
        const std::array<std::array<int,2>,8> points={{{8,8},{24,24},{31,31},{16,16},{7,8},{8,7},{0,8},{8,0}}};
        for(const auto& point:points) {
            const int x=point[0],y=point[1]; bool expected=false;
            for(int slot=0;slot<Building::MAX_COUNT;++slot) {
                const auto* building=enemy->myBuildings[slot];
                if(building && building->runtime->shootingRange>0 && world.game.map.warpDistMax(building->posX,building->posY,x,y)<=building->runtime->shootingRange+1 && building->hasClearShotTo(x,y)) { expected=true; break; }
            }
            CHECK(observer->locationIsInEnemyGuardTowerRange(x,y)==expected);
            positives+=expected;negatives+=!expected;
        }
        CHECK(positives>0); CHECK(negatives>0);
        CHECK(world.game.syncRandom==random); CHECK(observer->entityRandom.exportState()==entity);
    }

}
