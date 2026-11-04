// SPDX-License-Identifier: GPL-3.0-or-later
// Real worker movement, GUI save envelopes and exported cross-platform inputs.
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "map/claims/ActiveRouteRuntime.h"
#include "map/claims/ClaimPolicyConfig.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <fstream>
#include <memory>

TEST_CASE("experimental fetch preserves GUI continuations and execution across participants" * doctest::test_suite("ExperimentalClaimContinuation"))
{
    glob2test::HeadlessGlobals globals;
    const auto run=[&](unsigned participants,bool probe,bool mixed) {
        const auto prefix=std::string(mixed?"mixed-d4-":"experimental-")+(probe?"probe-":"direct-");
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7183});
        auto& game=f.game; auto& map=game.map;
        map.computeExecutor().configure(participants);
        game.gameHeader.setResourceGrowthDisabled(true);
        game.gameHeader.getWinningConditions().clear(); // Let CLI parity runs continue through delivery.
        map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;
        if(mixed){policy.sourceVersion=5;policy.pattern="illustrative258-middle13-v1";policy.patternFrame=ResourceClaims::ClaimPolicyConfig::PatternFrame::NearestD4;}
        policy.pendingStep=ResourceClaims::ClaimPolicyConfig::PendingStep::DirectHeading;
        policy.routeTarget=probe?ResourceClaims::ClaimPolicyConfig::RouteTarget::ProbeThenGradient:ResourceClaims::ClaimPolicyConfig::RouteTarget::ClaimedAccess;
        policy.routeRefreshJ=probe?3:1;policy.destinationRefreshJ=probe?4:6;
        map.setMaterialClaimPolicyConfig(policy);
        auto* inn=f.addBuilding("inn",18,8); REQUIRE(inn);
        inn->maxUnitWorking=1; inn->setMaterial(WHEAT,0); inn->updateCallLists();
        auto* unit=f.addUnit(WORKER,8,8); REQUIRE(unit);
        unit->medical=Unit::MED_FREE; unit->activity=Unit::ACT_RANDOM; unit->hungriness=0;
        const auto gid=unit->gid; const auto buildingGid=inn->gid;
        REQUIRE(map.incResourceByIndex(13,20,WHEAT,0));
        const auto source=Uint32(map.coordToIndex(13,20)); map.setMaterialAmountSlot(source,WHEAT,1);
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(inn->subscribeToBringMaterialsStep());
        REQUIRE(unit->attachedBuilding==inn); REQUIRE(inn->unitsWorking.size()==1);
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
        bool moved=false, harvested=false, refreshedGoal=false, refreshedRoute=false, followedTail=false; Uint64 pendingTick=0;
        for(unsigned tick=0;tick<2048;++tick) {
            CAPTURE(participants); CAPTURE(tick);
            f.step();
            for(auto* copy:{pendingCopy.get(),cachedCopy.get()}) if(copy) {
                copy->step(); REQUIRE(copy->game.checkSum()==game.checkSum());
                CHECK(copy->game.teams[0]->myBuildings[Building::GIDtoID(buildingGid)]->material(WHEAT)==inn->material(WHEAT));
            }
            REQUIRE(game.teams[0]->myUnits[Unit::GIDtoID(gid)]==unit);
            CHECK(map.materialClaimPolicyConfig()==policy);
            const bool traveling=unit->activeMaterialClaim && unit->displacement==Unit::DIS_GOING_TO_RESOURCE;
            if(traveling && !pendingCopy && map.materialClaimRoutes && !map.materialClaimRoutes->states.empty()) {
                const auto& state=map.materialClaimRoutes->states.begin()->second;
                REQUIRE(state.pending); CHECK(state.due==game.stepCounter);
                if(!probe) CHECK((unit->dx!=0 || unit->dy!=0)); // Requested heading; physical movement follows normal action cadence.
                CHECK(map.materialClaimState().at(source)==1);
                pendingTick=game.stepCounter; pendingCopy=copyGame((prefix+"pending").c_str());
            }
            if(traveling && pendingCopy && !cachedCopy && map.materialClaimRoutes && !map.materialClaimRoutes->states.empty()) {
                const auto& state=map.materialClaimRoutes->states.begin()->second;
                if(!state.pending) {
                    CHECK(game.stepCounter>pendingTick); REQUIRE(state.route.status==ResourceClaims::RouteStatus::Found);
                    cachedCopy=copyGame((prefix+"cached").c_str());
                }
            }
            refreshedGoal|=unit->materialClaimPolicyState.lastGoalAttempt>1;
            if(map.materialClaimRoutes) {
                refreshedRoute|=map.materialClaimRoutes->pipeline.requestSequence()>1;
                for(const auto& [owner,state]:map.materialClaimRoutes->states)followedTail|=state.followingGradient;
            }
            moved|=unit->posX!=8 || unit->posY!=8;
            harvested|=unit->hasCarriedMaterial(WHEAT);
            if(unit->hasCarriedMaterial(WHEAT)) CHECK_FALSE(unit->activeMaterialClaim);
            trace.push_back(game.checkSum());
            evidence<<game.stepCounter<<'\t'<<trace.back()<<'\t'<<unit->posX<<'\t'<<unit->posY<<'\t'
                <<bool(unit->activeMaterialClaim)<<'\t'<<map.materialAmountAtSlot(source,WHEAT)<<'\t'<<inn->material(WHEAT)<<'\n';
            if(inn->material(WHEAT)==1)break;
        }
        REQUIRE(pendingCopy); REQUIRE(cachedCopy); CHECK(moved); CHECK(harvested); CHECK(refreshedGoal);
        if(probe) CHECK(followedTail); else CHECK(refreshedRoute);
        CHECK(inn->material(WHEAT)==1); CHECK(map.materialAmountAtSlot(source,WHEAT)==0);
        CHECK(map.materialClaimLedger().size()==0); CHECK(evidence.good());
        return trace;
    };
    for(bool mixed:{false,true})for(bool probe:{false,true}) {
        CAPTURE(mixed);
        CAPTURE(probe);
        const auto ownerOnly=run(1,probe,mixed), parallel=run(3,probe,mixed);
        REQUIRE(ownerOnly.size()==parallel.size());
        for(std::size_t tick=0;tick<ownerOnly.size();++tick) {CAPTURE(tick); REQUIRE(ownerOnly[tick]==parallel[tick]);}
    }
}

