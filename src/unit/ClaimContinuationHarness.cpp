// SPDX-License-Identifier: GPL-3.0-or-later
// Real worker movement under resource claims, GUI save envelopes taken while a
// route is pending and after it is published, and exported cross-platform inputs.
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameFixtures.h"
#include "GradientFixture.h"
#include "map/claims/ActiveRouteRuntime.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <fstream>
#include <memory>

TEST_CASE("claim fetch preserves GUI continuations and execution across participants" * doctest::test_suite("ClaimContinuation"))
{
    glob2test::HeadlessGlobals globals;
    const auto run=[&](unsigned participants) {
        const std::string prefix="claims-";
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7183});
        auto& game=f.game; auto& map=game.map;
        map.computeExecutor().configure(participants);
        game.gameHeader.setResourceGrowthDisabled(true);
        game.gameHeader.getWinningConditions().clear(); // Let CLI parity runs continue through delivery.
        map.enableResourceClaims();
        // Two workers fetching the same stock crowd one another, so claims
        // engage (ResourceClaims::Policy::ContentionMinWorkers) and spread
        // them over the two neighbouring units of wheat.
        auto* inn=f.addBuilding("inn",18,8); REQUIRE(inn);
        inn->maxUnitWorking=2; inn->setMaterial(WHEAT,0); inn->updateCallLists();
        auto* unit=f.addUnit(WORKER,8,8); REQUIRE(unit);
        auto* partner=f.addUnit(WORKER,8,10); REQUIRE(partner);
        for(auto* worker:{unit,partner}) {worker->medical=Unit::MED_FREE; worker->activity=Unit::ACT_RANDOM; worker->hungriness=0;}
        const auto gid=unit->gid; const auto buildingGid=inn->gid;
        REQUIRE(map.incResourceByIndex(13,20,WHEAT,0));REQUIRE(map.incResourceByIndex(15,20,WHEAT,0));
        const auto source=Uint32(map.coordToIndex(13,20)),other=Uint32(map.coordToIndex(15,20));
        map.setMaterialAmountSlot(source,WHEAT,1);map.setMaterialAmountSlot(other,WHEAT,1);
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(inn->subscribeToBringMaterialsStep());REQUIRE(inn->subscribeToBringMaterialsStep());
        REQUIRE(unit->attachedBuilding==inn); REQUIRE(partner->attachedBuilding==inn); REQUIRE(inn->unitsWorking.size()==2);
        glob2test::discardPrematureHiringSuggestions(game);
        REQUIRE_FALSE(unit->activeMaterialClaim);
        std::unique_ptr<glob2test::HeadlessGame> pendingCopy, cachedCopy;
        const auto copyGame=[&](const char* label) {
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);
            const auto before=game.checkSum(); f.gui.save(&output,label); output.flush();
            const auto bytes=backend->takeContents(); CHECK(game.checkSum()==before);
            if(participants==1) {
                std::ofstream saved(glob2test::artifactDir()/(std::string(label)+".game"),std::ios::binary);
                saved.write(bytes.data(),std::streamsize(bytes.size())); REQUIRE(saved.good());
            }
            auto result=std::make_unique<glob2test::HeadlessGame>();
            result->game.map.clear();
            // clear() retains cell storage. Remove the old extent so loading
            // cannot validate a saved claim against the fixture's preset map.
            result->game.map.resourceCells.clear();
            REQUIRE(result->game.map.cellCount()==0);
            result->game.map.computeExecutor().configure(participants);
            auto* inputBackend=new GAGCore::MemoryStreamBackend;
            inputBackend->write(bytes.data(),bytes.size()); inputBackend->seekFromStart(0);
            GAGCore::BinaryInputStream input(inputBackend); REQUIRE(result->gui.load(&input,true));
            REQUIRE(result->game.checkSum()==before);
            return result;
        };
        std::vector<Uint32> trace;
        std::ofstream evidence(glob2test::artifactDir()/(prefix+std::to_string(participants)+"-participants.tsv"));
        REQUIRE(evidence.good()); evidence<<"tick\tchecksum\tx\ty\tclaim\tstock\tdelivered\n";
        bool moved=false, harvested=false, claimed=false; Uint64 pendingTick=0;
        for(unsigned tick=0;tick<2048;++tick) {
            CAPTURE(participants); CAPTURE(tick);
            f.step();
            for(auto* copy:{pendingCopy.get(),cachedCopy.get()}) if(copy) {
                copy->step(); REQUIRE(copy->game.checkSum()==game.checkSum());
                CHECK(copy->game.teams[0]->myBuildings[Building::GIDtoID(buildingGid)]->material(WHEAT)==inn->material(WHEAT));
            }
            REQUIRE(game.teams[0]->myUnits[Unit::GIDtoID(gid)]==unit);
            CHECK(map.activeClaimsEnabled());
            claimed|=unit->activeMaterialClaim.has_value();
            const bool traveling=unit->activeMaterialClaim && unit->displacement==Unit::DIS_GOING_TO_RESOURCE;
            const auto* state=[&]()->const ResourceClaims::ActiveRouteRuntime::State* {
                if(!map.materialClaimRoutes)return nullptr;
                const auto found=map.materialClaimRoutes->states.find(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity));
                return found==map.materialClaimRoutes->states.end()?nullptr:&found->second;
            }();
            if(traveling && !pendingCopy && state && state->pending) {
                CHECK(state->due==game.stepCounter);
                CHECK(map.materialClaimLedger().materialCount(unit->activeMaterialClaim->source,WHEAT)==1);
                pendingTick=game.stepCounter; pendingCopy=copyGame((prefix+"pending").c_str());
            }
            if(traveling && pendingCopy && !cachedCopy && state) {
                if(!state->pending) {
                    CHECK(game.stepCounter>pendingTick); REQUIRE(state->route.status==ResourceClaims::RouteStatus::Found);
                    cachedCopy=copyGame((prefix+"cached").c_str());
                }
            }
            moved|=unit->posX!=8 || unit->posY!=8;
            harvested|=unit->hasCarriedMaterial(WHEAT);
            if(unit->hasCarriedMaterial(WHEAT)) CHECK_FALSE(unit->activeMaterialClaim);
            trace.push_back(game.checkSum());
            evidence<<game.stepCounter<<'\t'<<trace.back()<<'\t'<<unit->posX<<'\t'<<unit->posY<<'\t'
                <<bool(unit->activeMaterialClaim)<<'\t'<<map.materialAmountAtSlot(source,WHEAT)+map.materialAmountAtSlot(other,WHEAT)<<'\t'<<inn->material(WHEAT)<<'\n';
            if(inn->material(WHEAT)==2)break;
        }
        REQUIRE(pendingCopy); REQUIRE(cachedCopy); CHECK(claimed); CHECK(moved); CHECK(harvested);
        CHECK(inn->material(WHEAT)==2); CHECK(map.materialAmountAtSlot(source,WHEAT)==0); CHECK(map.materialAmountAtSlot(other,WHEAT)==0);
        CHECK(map.materialClaimLedger().size()==0); CHECK(evidence.good());
        return trace;
    };
    {
        const auto ownerOnly=run(1), parallel=run(3);
        REQUIRE(ownerOnly.size()==parallel.size());
        for(std::size_t tick=0;tick<ownerOnly.size();++tick) {CAPTURE(tick); REQUIRE(ownerOnly[tick]==parallel[tick]);}
    }
}

