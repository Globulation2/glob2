// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "LegacyPopulationOutput.h"
#include "Building.h"
#include "BuildingType.h"
#include "Team.h"
#include "Unit.h"
#include "Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <climits>
#include <algorithm>
#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
// Emit historical wide stock without bypassing the bounded runtime setters.
struct HistoricalStockOutput : glob2test::LegacyPopulationOutput {
    int slot;
    Sint32 stock;
    int version;
    HistoricalStockOutput(GAGCore::MemoryStreamBackend* bytes,int slot,Sint32 stock,int version=VERSION_MINOR,Game* game=nullptr)
        : LegacyPopulationOutput(bytes,game,version),slot(slot),stock(stock),version(version) {}
    void writeSint32(Sint32 value,const std::string name) override {
        if(name=="versionMinor") value=version;
        if(name=="localRessource["+std::to_string(slot)+"]") value=stock;
        BinaryOutputStream::writeSint32(value,name);
    }
};
std::string historicalGame(Game& game,int slot,Sint32 stock,int version=VERSION_MINOR) {
    auto* bytes=new GAGCore::MemoryStreamBackend;
    HistoricalStockOutput out(bytes,slot,stock,version,&game);
    game.save(&out,false,"Private stock cap migration");out.flush();
    return bytes->takeContents();
}
bool loadGame(Game& game,const std::string& bytes) {
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    in.seekFromStart(0);return game.load(&in);
}
std::string saveGame(Game& game) {
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(bytes);
    game.save(&out,false,"Canceled service continuation");out.flush();
    return bytes->takeContents();
}
void saveArtifact(const std::filesystem::path& path,const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path,std::ios::binary);
    output.write(bytes.data(),bytes.size());
    REQUIRE(output.good());
}
std::vector<Uint32> continuationComponents(Game& game) {
    std::vector<Uint32> state,buildings,units;
    game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin()); // Resaving upgrades the historical save header to the current version.
    state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());
    return state;
}
Uint32 componentDigest(const std::vector<Uint32>& values) {
    Uint32 hash=2166136261u;
    for(Uint32 value:values) hash=(hash^value)*16777619u;
    return hash;
}
void configureMigrationInn(Game& game,bool aura=false) {
    auto catalog=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    const int type=game.buildingsTypes.getTypeNum("inn",0,false);
    auto& inn=catalog["variants"][type];
    inn["semantics"]["feeding"]["cost"]={{"food",200}};
    inn["properties"]["maxMaterial"][WHEAT]=600;
    if(aura) inn["semantics"]["areaEffects"]={{"radius",8},{"healingQ8",256},{"feedingQ8",256}};
    game.buildingsTypes.loadSnapshotJson(catalog.dump());game.configureBuildingCatalog();
}
// A valid historical reservation can exceed the new live stock cap. Assemble
// that pre-cap visit and emit its original inventory through HistoricalStockOutput.
void savedVisit(Unit* unit,Building* building,bool entering) {
    unit->attachedBuilding=building;unit->activity=Unit::ACT_UPGRADING;
    unit->displacement=entering ? Unit::DIS_ENTERING_BUILDING : Unit::DIS_INSIDE;
    unit->movement=entering ? Unit::MOV_ENTERING_BUILDING : Unit::MOV_INSIDE;
    unit->destinationPurpose=FEED;unit->setServiceResourcesReserved(true);
    unit->setNeedToRecheckMedical(false);unit->hungry=50;
    unit->insideTimeout=entering ? 0 : -100;
    unit->speed=0;
    if(!entering) {
        if(unit->performance[FLY]) unit->owner->map->setAirUnit(unit->posX,unit->posY,NOGUID);
        else unit->owner->map->setGroundUnit(unit->posX,unit->posY,NOGUID);
    }
    unit->posX=building->posX;unit->posY=building->posY;unit->dx=1;unit->dy=0;
    building->unitsInside.push_back(unit);
}
int occupiedCells(const Map& map,Uint16 gid,bool flying) {
    int count=0;
    for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x)
        count+=(flying ? map.getAirUnit(x,y) : map.getGroundUnit(x,y))==gid;
    return count;
}
}
TEST_SUITE("BuildingStock")
{
    TEST_CASE("all fifteen private stock slots clamp wide save values before narrowing [save-format]") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
        for(int slot=0;slot<MaterialSlotCount;++slot)for(Sint32 value:{INT_MIN,-1,0,255,256,INT_MAX}) {
            CAPTURE(slot);CAPTURE(value);
            auto* bytes=new GAGCore::MemoryStreamBackend;
            HistoricalStockOutput out(bytes,slot,value);building->save(&out);out.flush();
            const auto saved=bytes->takeContents();
            GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(saved.data(),saved.size()));in.seekFromStart(0);
            Building loaded(&in,&world.game.buildingsTypes,world.team,VERSION_MINOR);
            CHECK(loaded.privateMaterial(slot)==std::clamp(value,0,PrivateBuildingStockLimit));
            CHECK(loaded.privateStockClampedOnLoad==(value<0 || value>PrivateBuildingStockLimit));
        }
    }
    TEST_CASE("clamped stock keeps affordable service commitments and cancels unfunded visits without benefits [save-format]") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto* definition=world.game.buildingsTypes.getByType("inn",0,false);
        REQUIRE(definition);
        const int type=world.game.buildingsTypes.getTypeNum("inn",0,false);
        catalog["variants"][type]["semantics"]["feeding"]["cost"]={{"food",200}};
        catalog["variants"][type]["properties"]["maxMaterial"][WHEAT]=600;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
        building->setPrivateMaterial(WHEAT,255);
        const auto buildingGid=building->gid;
        Uint16 unitGids[2]{};
        for(int i=0;i<2;++i) {
            auto* unit=world.addUnit(WORKER,12+i,12);REQUIRE(unit);unitGids[i]=unit->gid;
            world.game.map.setGroundUnit(unit->posX,unit->posY,NOGBID);
            unit->attachedBuilding=building;unit->activity=Unit::ACT_UPGRADING;
            unit->displacement=Unit::DIS_INSIDE;unit->movement=Unit::MOV_INSIDE;
            unit->destinationPurpose=FEED;unit->setServiceResourcesReserved(true);
            unit->setNeedToRecheckMedical(false);unit->hungry=50;unit->insideTimeout=-1;
            building->unitsInside.push_back(unit);
        }
        building->reservedMaterials[WHEAT]=400;
        const auto saved=historicalGame(world.game,WHEAT,600);
        glob2test::HeadlessGame resumed({.header=true});REQUIRE(loadGame(resumed.game,saved));
        auto* loaded=resumed.game.teams[0]->myBuildings[Building::GIDtoID(buildingGid)];REQUIRE(loaded);
        auto* first=resumed.game.teams[0]->myUnits[Unit::GIDtoID(unitGids[0])];REQUIRE(first);
        auto* second=resumed.game.teams[0]->myUnits[Unit::GIDtoID(unitGids[1])];REQUIRE(second);
        CHECK(loaded->privateMaterial(WHEAT)==255);CHECK(loaded->reservedMaterials[WHEAT]==200);
        CHECK(first->serviceResourcesReserved());CHECK_FALSE(second->serviceResourcesReserved());
        CHECK(second->displacement==Unit::DIS_EXITING_BUILDING);CHECK(second->needToRecheckMedical());
        CHECK(second->hungry==50);CHECK(second->attachedBuilding==loaded);
        CHECK_FALSE(loaded->privateStockClampedOnLoad);
        loaded->releaseService(first);CHECK(loaded->reservedMaterials[WHEAT]==0);
        CHECK(loaded->privateMaterial(WHEAT)==255);
    }
    TEST_CASE("canceled entering services preserve ground and air occupancy across resave [save-format][artifacts]") {
        glob2test::HeadlessGlobals globals;
        // The adapter restores the pre-165 population encoding; only 159 predates bounded stock.
        for(int version:{FILE_FORMAT_VERSION_TURRET_TARGETING,160,VERSION_MINOR})
            for(int unitType:{WORKER,EXPLORER}) {
                CAPTURE(version);CAPTURE(unitType);
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
                configureMigrationInn(world.game);
                auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
                auto* retained=world.addUnit(WORKER,12,12);REQUIRE(retained);
                auto* canceled=world.addUnit(unitType,3,4);REQUIRE(canceled);
                savedVisit(retained,building,false);savedVisit(canceled,building,true);
                building->setMaterial(WHEAT,255);building->reservedMaterials[WHEAT]=400;
                REQUIRE(world.game.integrity());
                const auto buildingId=Building::GIDtoID(building->gid);
                const auto retainedId=Unit::GIDtoID(retained->gid),canceledId=Unit::GIDtoID(canceled->gid);
                const auto directory=glob2test::artifactDir()/(std::to_string(version)+"-"+std::to_string(unitType));
                const auto saved=historicalGame(world.game,WHEAT,600,version);
                saveArtifact(directory/"before-normalization.game",saved);
                glob2test::HeadlessGame resumed({.header=true});REQUIRE(loadGame(resumed.game,saved));
                auto* loaded=resumed.game.teams[0]->myBuildings[buildingId];REQUIRE(loaded);
                auto* visitor=resumed.game.teams[0]->myUnits[canceledId];REQUIRE(visitor);
                CHECK(visitor->displacement==Unit::DIS_ENTERING_BUILDING);
                CHECK_FALSE(visitor->serviceResourcesReserved());CHECK(visitor->insideTimeout==0);
                CHECK(resumed.game.teams[0]->myUnits[retainedId]->serviceResourcesReserved());
                CHECK(loaded->reservedMaterials[WHEAT]==200);CHECK(loaded->material(WHEAT)==255);
                CHECK(occupiedCells(resumed.game.map,visitor->gid,unitType==EXPLORER)==1);
                REQUIRE(resumed.game.integrity());
                visitor->speed=256;visitor->delta=0;visitor->action=STOP_WALK;
                const auto normalized=saveGame(resumed.game);
                saveArtifact(directory/"pending-cancellation.game",normalized);
                glob2test::HeadlessGame copy({.header=true});REQUIRE(loadGame(copy.game,normalized));
                auto* copied=copy.game.teams[0]->myUnits[canceledId];REQUIRE(copied);
                CHECK(continuationComponents(resumed.game)==continuationComponents(copy.game));
                // Finish the pending entry and exit through the real action handlers.
                visitor->syncStep();copied->syncStep();
                REQUIRE(visitor->attachedBuilding==nullptr);REQUIRE(copied->attachedBuilding==nullptr);
                CHECK(visitor->hungry==50);CHECK(copied->hungry==50);
                CHECK(resumed.game.teams[0]->stats.events().meals==0);
                CHECK(loaded->reservedMaterials[WHEAT]==200);CHECK(loaded->material(WHEAT)==255);
                CHECK(resumed.game.teams[0]->myUnits[retainedId]->serviceResourcesReserved());
                CHECK(occupiedCells(resumed.game.map,visitor->gid,unitType==EXPLORER)==1);
                CHECK((unitType==EXPLORER ? resumed.game.map.getAirUnit(visitor->posX,visitor->posY)
                    : resumed.game.map.getGroundUnit(visitor->posX,visitor->posY))==visitor->gid);
                REQUIRE(resumed.game.integrity());REQUIRE(copy.game.integrity());
                resumed.game.setWaitingOnMask(0);copy.game.setWaitingOnMask(0);
                std::ofstream trace(directory/"continuation.csv");
                trace<<"tick,normalized,resaved\n";
                for(int tick=0;tick<16;++tick) {
                    resumed.step();copy.step();
                    const auto expected=continuationComponents(resumed.game),actual=continuationComponents(copy.game);
                    CHECK(actual==expected);
                    trace<<resumed.game.stepCounter<<','<<componentDigest(expected)<<','<<componentDigest(actual)<<'\n';
                }
            }
    }
    TEST_CASE("canceled inside services reset their timer and resume terrain and area effects [save-format][artifacts]") {
        glob2test::HeadlessGlobals globals;
        for(int version:{FILE_FORMAT_VERSION_TURRET_TARGETING,160,VERSION_MINOR})
            for(int unitType:{WORKER,EXPLORER}) {
                CAPTURE(version);CAPTURE(unitType);
                glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
                configureMigrationInn(world.game,true);
                auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
                auto* retained=world.addUnit(WORKER,12,12);REQUIRE(retained);
                auto* canceled=world.addUnit(unitType,3,4);REQUIRE(canceled);
                savedVisit(retained,building,false);savedVisit(canceled,building,false);
                canceled->hp=canceled->performance[HP]/2; // Also exercises the explorer service-rebound policy.
                building->setMaterial(WHEAT,255);building->reservedMaterials[WHEAT]=400;
                const auto buildingId=Building::GIDtoID(building->gid),canceledId=Unit::GIDtoID(canceled->gid);
                const auto directory=glob2test::artifactDir()/(std::to_string(version)+"-"+std::to_string(unitType));
                const auto saved=historicalGame(world.game,WHEAT,600,version);
                saveArtifact(directory/"unfinished-service.game",saved);
                glob2test::HeadlessGame resumed({.header=true});REQUIRE(loadGame(resumed.game,saved));
                auto* loaded=resumed.game.teams[0]->myUnits[canceledId];REQUIRE(loaded);
                CHECK(loaded->displacement==Unit::DIS_EXITING_BUILDING);CHECK(loaded->insideTimeout==0);
                CHECK_FALSE(loaded->serviceResourcesReserved());
                loaded->speed=256;loaded->delta=0;loaded->action=STOP_WALK;
                loaded->syncStep();
                REQUIRE(loaded->attachedBuilding==nullptr);CHECK(loaded->hungry==50);
                CHECK(resumed.game.teams[0]->stats.events().meals==0);
                CHECK(resumed.game.teams[0]->myBuildings[buildingId]->reservedMaterials[WHEAT]==200);
                CHECK(resumed.game.teams[0]->myBuildings[buildingId]->material(WHEAT)==255);
                resumed.game.map.paintCell(loaded->posX,loaded->posY,ICE);
                REQUIRE(resumed.game.map.hasTerrainHealthEffects());
                loaded->speed=0;loaded->delta=0;loaded->action=STOP_WALK;
                const int hp=loaded->hp;REQUIRE(hp>1);
                for(int tick=0;tick<32;++tick) loaded->syncStep();
                const int terrainDamage=unitType==WORKER ? 1 : 0; // Ice damages ground units only.
                CHECK(loaded->hp==hp-terrainDamage);
                resumed.game.stepCounter=16;resumed.game.areaEffects.beginTick(resumed.game);
                REQUIRE(resumed.game.areaEffects.enabled());
                loaded->applyAreaServices();
                CHECK(loaded->hp==hp-terrainDamage+1);CHECK(loaded->hungry==51);
                saveArtifact(directory/"after-exit.game",saveGame(resumed.game));
            }
    }
    TEST_CASE("canceled entering explorers wait for a free exit before medical rebound [save-format]") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        configureMigrationInn(world.game);
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
        auto* retained=world.addUnit(WORKER,12,12);REQUIRE(retained);
        auto* canceled=world.addUnit(EXPLORER,3,4);REQUIRE(canceled);
        savedVisit(retained,building,false);savedVisit(canceled,building,true);
        canceled->hp=canceled->performance[HP]/2;
        building->setMaterial(WHEAT,255);building->reservedMaterials[WHEAT]=400;
        for(int x=building->posX;x<building->posX+building->type->width;++x)
            for(int y=building->posY;y<building->posY+building->type->height;++y) {
                auto* blocker=world.addUnit(EXPLORER,x,y);REQUIRE(blocker);
                blocker->speed=0;
            }
        const auto buildingId=Building::GIDtoID(building->gid),canceledId=Unit::GIDtoID(canceled->gid);
        glob2test::HeadlessGame resumed({.header=true});
        REQUIRE(loadGame(resumed.game,historicalGame(world.game,WHEAT,600,FILE_FORMAT_VERSION_TURRET_TARGETING)));
        auto* loaded=resumed.game.teams[0]->myUnits[canceledId];REQUIRE(loaded);
        auto* inn=resumed.game.teams[0]->myBuildings[buildingId];REQUIRE(inn);
        const int hp=loaded->hp;
        for(int attempt=0;attempt<3;++attempt) {
            loaded->speed=256;loaded->delta=0;loaded->action=STOP_WALK;
            loaded->syncStep();
            CHECK(loaded->attachedBuilding==inn);CHECK(loaded->activity==Unit::ACT_UPGRADING);
            CHECK(loaded->displacement==Unit::DIS_EXITING_BUILDING);CHECK(loaded->movement==Unit::MOV_INSIDE);
            CHECK(occupiedCells(resumed.game.map,loaded->gid,true)==0);
            CHECK(loaded->hp==hp);CHECK(loaded->hungry==50);CHECK(loaded->insideTimeout==0);
            CHECK(inn->reservedMaterials[WHEAT]==200);CHECK(inn->unitsInside.size()==2);
            REQUIRE(resumed.game.integrity());
        }
        const auto blocked=saveGame(resumed.game);
        glob2test::HeadlessGame copy({.header=true});REQUIRE(loadGame(copy.game,blocked));
        auto* copied=copy.game.teams[0]->myUnits[canceledId];REQUIRE(copied);
        copied->speed=256;copied->delta=0;copied->action=STOP_WALK;
        copied->syncStep();
        CHECK(copied->attachedBuilding==copy.game.teams[0]->myBuildings[buildingId]);
        CHECK(copied->insideTimeout==0);CHECK_FALSE(copied->serviceResourcesReserved());
        CHECK(occupiedCells(copy.game.map,copied->gid,true)==0);
        REQUIRE(copy.game.integrity());
        REQUIRE(resumed.game.removeUnitAndBuildingAndFlags(4,4,1,Game::DEL_AIR_UNIT));
        loaded->speed=256;loaded->delta=0;loaded->action=STOP_WALK;
        loaded->syncStep();
        CHECK(loaded->attachedBuilding==nullptr);CHECK(occupiedCells(resumed.game.map,loaded->gid,true)==1);
        CHECK(resumed.game.map.getAirUnit(4,4)==loaded->gid);
        CHECK(loaded->hp==hp);CHECK(loaded->hungry==50);CHECK(inn->unitsInside.size()==1);
        CHECK(inn->reservedMaterials[WHEAT]==200);CHECK(inn->material(WHEAT)==255);
        REQUIRE(resumed.game.integrity());
    }
    TEST_CASE("funded explorers rebound to healing only after a blocked exit opens") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto* inn=world.addBuilding("inn",4,4);REQUIRE(inn);
        auto* hospital=world.addBuilding("hospital",12,4);REQUIRE(hospital);
        for(int material=0;material<MaterialSlotCount;++material) hospital->setMaterial(material,10);
        hospital->updateCallLists();
        auto* visitor=world.addUnit(EXPLORER,3,4);REQUIRE(visitor);
        visitor->hp=visitor->performance[HP]/2;visitor->hungry=50;
        visitor->destinationPurpose=FEED;inn->setMaterial(WHEAT,10);
        inn->subscribeUnitForInside(visitor);REQUIRE(visitor->serviceResourcesReserved());
        for(int x=inn->posX;x<inn->posX+inn->type->width;++x)
            for(int y=inn->posY;y<inn->posY+inn->type->height;++y) {
                auto* blocker=world.addUnit(EXPLORER,x,y);REQUIRE(blocker);
                blocker->speed=0;
            }
        visitor->posX=4;visitor->posY=4;visitor->dx=1;visitor->dy=0;
        visitor->displacement=Unit::DIS_ENTERING_BUILDING;visitor->movement=Unit::MOV_ENTERING_BUILDING;
        visitor->speed=256;visitor->delta=0;visitor->action=STOP_WALK;
        visitor->syncStep();REQUIRE(visitor->displacement==Unit::DIS_INSIDE);
        visitor->insideTimeout=0; // Complete the funded meal on the next action.
        const int hp=visitor->hp;
        for(int attempt=0;attempt<3;++attempt) {
            visitor->speed=256;visitor->delta=0;visitor->action=STOP_WALK;
            visitor->syncStep();
            CHECK(visitor->attachedBuilding==inn);CHECK(visitor->displacement==Unit::DIS_EXITING_BUILDING);
            CHECK(visitor->movement==Unit::MOV_INSIDE);CHECK(occupiedCells(world.game.map,visitor->gid,true)==0);
            CHECK(visitor->hp==hp);CHECK(visitor->hungry==visitor->foodCapacity());
            CHECK_FALSE(visitor->serviceResourcesReserved());CHECK(inn->unitsInside.size()==1);
            CHECK(world.team->stats.events().meals==1);
        }
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(4,4,1,Game::DEL_AIR_UNIT));
        visitor->speed=256;visitor->delta=0;visitor->action=STOP_WALK;
        visitor->syncStep();
        REQUIRE(visitor->attachedBuilding==nullptr);CHECK(inn->unitsInside.empty());
        CHECK(occupiedCells(world.game.map,visitor->gid,true)==1);
        // Rebound is deferred only until the unit has actually claimed its exit.
        visitor->speed=256;visitor->delta=0;visitor->action=STOP_WALK;
        visitor->syncStep();
        CHECK(visitor->attachedBuilding==hospital);CHECK(visitor->destinationPurpose==HEAL);
        CHECK(visitor->serviceResourcesReserved());CHECK(occupiedCells(world.game.map,visitor->gid,true)==1);
        REQUIRE(world.game.integrity());
    }
    TEST_CASE("zero cost service admissions retain reservations through normal entry") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int type=world.game.buildingsTypes.getTypeNum("inn",0,false);
        catalog["variants"][type]["semantics"]["feeding"]["cost"]=nlohmann::json::object();
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
        auto* visitor=world.addUnit(WORKER,3,4);REQUIRE(visitor);
        visitor->destinationPurpose=FEED;
        REQUIRE(building->canOfferService(visitor,FEED));
        building->subscribeUnitForInside(visitor);
        REQUIRE(visitor->serviceResourcesReserved());
        visitor->posX=4;visitor->posY=4;visitor->dx=1;visitor->dy=0;
        visitor->displacement=Unit::DIS_ENTERING_BUILDING;visitor->movement=Unit::MOV_ENTERING_BUILDING;
        visitor->speed=256;visitor->delta=0;visitor->action=STOP_WALK;
        visitor->syncStep();
        CHECK(visitor->displacement==Unit::DIS_INSIDE);CHECK(visitor->attachedBuilding==building);
        CHECK(visitor->insideTimeout==-building->type->semantics.feeding.duration);
        CHECK(visitor->serviceResourcesReserved());CHECK(building->reservedMaterials[WHEAT]==0);
        CHECK(occupiedCells(world.game.map,visitor->gid,false)==0);
    }
    TEST_CASE("clamped stock trims construction funding while preserving wide budget and saved demand [save-format]") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        catalog["variants"][2]["semantics"]["constructionCost"]={{"food",500}};
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.game.addBuilding(4,4,2,0,1,1);REQUIRE(building);REQUIRE(building->type->isBuildingSite);
        building->setPrivateMaterial(WHEAT,255);building->constructionReserved[WHEAT]=400;
        building->siteCompletionPending=true;building->wishedMaterials[WHEAT]=INT_MIN;
        const auto gid=building->gid;
        const auto saved=historicalGame(world.game,WHEAT,600);
        glob2test::HeadlessGame resumed({.header=true});REQUIRE(loadGame(resumed.game,saved));
        const auto* loaded=resumed.game.teams[0]->myBuildings[Building::GIDtoID(gid)];REQUIRE(loaded);
        CHECK(loaded->constructionBudget[WHEAT]==500);CHECK(loaded->constructionReserved[WHEAT]==255);
        CHECK(loaded->reservedMaterials[WHEAT]==255);CHECK_FALSE(loaded->siteCompletionPending);
        CHECK(loaded->wishedMaterials[WHEAT]==INT_MIN);CHECK_FALSE(loaded->privateStockClampedOnLoad);
    }
    TEST_CASE("clamped private stock cancels an unfunded committed production job [save-format]") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int type=world.game.buildingsTypes.getTypeNum("swarm",0,false);
        auto& production=catalog["variants"][type]["semantics"]["production"];
        production["scheduling"]="weighted_committed_job";
        production["recipes"]={{"worker",{{"enabled",true},{"duration",10},{"cost",{{"food",400}}}}}};
        catalog["variants"][type]["properties"]["maxMaterial"][WHEAT]=600;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("swarm",4,4);REQUIRE(building);
        building->setPrivateMaterial(WHEAT,255);building->productionUnit=WORKER;
        building->productionTimeout=5;building->reservedMaterials[WHEAT]=400;
        const auto gid=building->gid;
        const auto saved=historicalGame(world.game,WHEAT,600);
        glob2test::HeadlessGame resumed({.header=true});REQUIRE(loadGame(resumed.game,saved));
        const auto* loaded=resumed.game.teams[0]->myBuildings[Building::GIDtoID(gid)];REQUIRE(loaded);
        CHECK(loaded->privateMaterial(WHEAT)==255);CHECK(loaded->reservedMaterials[WHEAT]==0);
        CHECK(loaded->productionUnit==-1);CHECK(loaded->productionTimeout==10);
        CHECK_FALSE(loaded->privateStockClampedOnLoad);
    }
    TEST_CASE("clamping inactive private inventory preserves wide shared stock and service funding [save-format]") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int type=world.game.buildingsTypes.getTypeNum("inn",0,false);
        catalog["variants"][type]["semantics"]["market"]["sharedStock"]=true;
        catalog["variants"][type]["semantics"]["feeding"]["cost"]={{"food",400}};
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);REQUIRE(building->usesTeamMaterials());
        world.team->teamMaterials[WHEAT]=600;world.team->reservedTeamMaterials[WHEAT]=400;
        building->reservedMaterials[WHEAT]=400;
        auto* unit=world.addUnit(WORKER,12,12);REQUIRE(unit);
        world.game.map.setGroundUnit(unit->posX,unit->posY,NOGBID);
        unit->attachedBuilding=building;unit->activity=Unit::ACT_UPGRADING;unit->destinationPurpose=FEED;
        unit->displacement=Unit::DIS_INSIDE;unit->movement=Unit::MOV_INSIDE;
        unit->setServiceResourcesReserved(true);unit->setNeedToRecheckMedical(false);
        building->unitsInside.push_back(unit);
        const auto gid=building->gid,unitGid=unit->gid;
        const auto saved=historicalGame(world.game,WHEAT,600);
        glob2test::HeadlessGame resumed({.header=true});REQUIRE(loadGame(resumed.game,saved));
        const auto* loaded=resumed.game.teams[0]->myBuildings[Building::GIDtoID(gid)];REQUIRE(loaded);
        const auto* visitor=resumed.game.teams[0]->myUnits[Unit::GIDtoID(unitGid)];REQUIRE(visitor);
        CHECK(loaded->privateMaterial(WHEAT)==255);CHECK(loaded->material(WHEAT)==600);
        CHECK(resumed.game.teams[0]->reservedTeamMaterials[WHEAT]==400);CHECK(visitor->serviceResourcesReserved());
        CHECK(visitor->displacement==Unit::DIS_INSIDE);CHECK_FALSE(loaded->privateStockClampedOnLoad);
    }

    TEST_CASE("runtime stock updates saturate every slot with wide intermediate arithmetic") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
        REQUIRE_FALSE(building->usesTeamMaterials());
        for(int slot=0;slot<MaterialSlotCount;++slot) {
            CAPTURE(slot);
            for(Sint64 value:{Sint64(INT_MIN),Sint64(-1),Sint64(0),Sint64(255),Sint64(256),Sint64(INT_MAX)+255}) {
                CAPTURE(value);
                building->setMaterial(slot,value);
                CHECK(building->material(slot)==std::clamp<Sint64>(value,0,255));
            }
            building->setMaterial(slot,254);
            building->changeMaterial(slot,INT_MAX); // 254 + INT_MAX must not overflow first.
            CHECK(building->material(slot)==255);
            building->changeMaterial(slot,INT_MIN);
            CHECK(building->material(slot)==0);
            building->changeMaterial(slot,256);
            CHECK(building->material(slot)==255);
            building->changeMaterial(slot,-1);
            CHECK(building->material(slot)==254);
        }
        BuildingMaterialCost cost{};cost.fill(17);
        REQUIRE(building->reserveMaterials(cost));
        for(int slot=0;slot<MaterialSlotCount;++slot) {
            CHECK(building->material(slot)==254);
            CHECK(building->availableMaterial(slot)==237);
        }
        building->consumeReservedMaterials(cost,-1);
        for(int slot=0;slot<MaterialSlotCount;++slot) {
            CHECK(building->material(slot)==237);
            CHECK(building->reservedMaterials[slot]==0);
            CHECK(building->availableMaterial(slot)==237);
        }
    }
    TEST_CASE("delivery at the private cap records acceptance and retains exact cargo residual") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int type=world.game.buildingsTypes.getTypeNum("inn",0,false);
        catalog["variants"][type]["properties"]["maxMaterial"][WHEAT]=600;
        catalog["variants"][type]["properties"]["materialMultiplier"][WHEAT]=4;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);
        const auto deliveredBefore=world.team->stats.events().delivered[WHEAT];
        const auto spillsBefore=world.team->stats.events().materialSpillageEvents;
        building->setMaterial(WHEAT,254);
        const auto partial=building->deliverMaterialPacket(WHEAT,{});
        CHECK(partial.acceptedStock==1);CHECK(building->material(WHEAT)==255);
        CHECK(partial.discardedNumerator==3);CHECK(partial.discardedDenominator==4);
        CHECK(world.team->stats.events().delivered[WHEAT]==deliveredBefore+1);
        CHECK(world.team->stats.events().materialSpillageEvents==spillsBefore+1);
        const auto full=building->deliverMaterialPacket(WHEAT,{});
        CHECK(full.acceptedStock==0);CHECK(building->material(WHEAT)==255);
        CHECK(full.discardedNumerator==1);CHECK(full.discardedDenominator==1);
        CHECK(world.team->stats.events().delivered[WHEAT]==deliveredBefore+1);
        CHECK(world.team->stats.events().materialSpillageEvents==spillsBefore+2);
        building->setMaterial(WHEAT,254);
        const auto cargo=building->deliverCargoPacket(WHEAT,{1,1});
        CHECK(cargo.acceptedStock==1);CHECK(building->material(WHEAT)==255);
        CHECK(cargo.residual.numerator==3);CHECK(cargo.residual.denominator==4);
        const auto blocked=building->deliverCargoPacket(WHEAT,cargo.residual);
        CHECK(blocked.acceptedStock==0);CHECK(blocked.residual==cargo.residual);
        CHECK(world.team->stats.events().delivered[WHEAT]==deliveredBefore+2);
        CHECK(world.team->stats.events().materialSpillageEvents==spillsBefore+2);
    }
    TEST_CASE("shared runtime reservations and consumption retain wide stock and inactive private inventory") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int type=world.game.buildingsTypes.getTypeNum("inn",0,false);
        catalog["variants"][type]["semantics"]["market"]["sharedStock"]=true;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());world.game.configureBuildingCatalog();
        auto* building=world.addBuilding("inn",4,4);REQUIRE(building);REQUIRE(building->usesTeamMaterials());
        BuildingMaterialCost cost{};cost.fill(400);
        for(int slot=0;slot<MaterialSlotCount;++slot) {
            building->setPrivateMaterial(slot,7);
            building->setMaterial(slot,600);
            building->changeMaterial(slot,256);
            CHECK(world.team->teamMaterials[slot]==856);
            CHECK(building->privateMaterial(slot)==7);
        }
        REQUIRE(building->reserveMaterials(cost));
        for(int slot=0;slot<MaterialSlotCount;++slot) {
            CHECK(building->availableMaterial(slot)==456);
            CHECK(world.team->reservedTeamMaterials[slot]==400);
        }
        building->releaseMaterials(cost);
        REQUIRE(building->reserveMaterials(cost));
        building->consumeReservedMaterials(cost,-1);
        for(int slot=0;slot<MaterialSlotCount;++slot) {
            CHECK(world.team->teamMaterials[slot]==456);
            CHECK(building->material(slot)==456);
            CHECK(world.team->reservedTeamMaterials[slot]==0);
            CHECK(building->reservedMaterials[slot]==0);
            CHECK(building->privateMaterial(slot)==7);
        }
    }

}
