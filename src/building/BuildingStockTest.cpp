// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
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
#include <nlohmann/json.hpp>

namespace {
// Emit historical wide stock without bypassing the bounded runtime setters.
struct HistoricalStockOutput : GAGCore::BinaryOutputStream {
    int slot;
    Sint32 stock;
    HistoricalStockOutput(GAGCore::MemoryStreamBackend* bytes,int slot,Sint32 stock)
        : BinaryOutputStream(bytes),slot(slot),stock(stock) {}
    void writeSint32(Sint32 value,const std::string name) override {
        BinaryOutputStream::writeSint32(name=="localRessource["+std::to_string(slot)+"]" ? stock : value,name);
    }
};
std::string historicalGame(Game& game,int slot,Sint32 stock) {
    auto* bytes=new GAGCore::MemoryStreamBackend;
    HistoricalStockOutput out(bytes,slot,stock);
    game.save(&out,false,"Private stock cap migration");out.flush();
    return bytes->takeContents();
}
bool loadGame(Game& game,const std::string& bytes) {
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    in.seekFromStart(0);return game.load(&in);
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
