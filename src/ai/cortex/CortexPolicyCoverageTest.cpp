// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ai/cortex/CortexPolicy.h"
#include <functional>
#include <cstdlib>
#include <optional>
#include <vector>

namespace
{
using namespace Cortex;
CortexObservation healthy()
{
    auto obs=makeEmptyObservation();
    obs.valid=1; obs.totalUnit=20; obs.workers=10; obs.warriors=9; obs.explorers=1;
    obs.freeWorkers=5; obs.feedCapacity=100; obs.maxBuildLevel=1;
    obs.swarmsProducing=1; obs.swarmsProducingWarrior=1;
    for (int type : {CORTEX_BUILD_SWARM,CORTEX_BUILD_FOOD,CORTEX_BUILD_HEAL,
                     CORTEX_BUILD_SCIENCE,CORTEX_BUILD_WALKSPEED,CORTEX_BUILD_ATTACK})
    {
        obs.buildingCountPerLevel[type][1]=2;
        obs.upgradableCount[type]=1;
        obs.buildCandidates[type][1].valid=1;
    }
    obs.buildingCountPerLevel[CORTEX_BUILD_SWARM][1]=1;
    obs.buildLevel[1]=10; obs.walkLevel[1]=19; obs.attackStrengthLevel[1]=9;
    obs.warriorWalkLevel[1]=9;
    obs.algaeReachable=1;
    return obs;
}
struct PolicyEnvironment
{
    std::vector<std::pair<const char*,std::optional<std::string>>> previous;
    PolicyEnvironment()
    {
        for (const char* name : {"GLOB2_CORTEX_POLICY","GLOB2_CORTEX_NET","GLOB2_CORTEX_DECISION_NET"})
        {
            const char* value=std::getenv(name);
            previous.emplace_back(name,value ? std::optional<std::string>(value) : std::nullopt);
            glob2test::unsetEnv(name);
        }
    }
    ~PolicyEnvironment()
    {
        for (const auto& [name,value] : previous)
            if (value) glob2test::setEnv(name,value->c_str()); else glob2test::unsetEnv(name);
    }
};
std::string constantModel(int inputs,int outputs,int preferred)
{
    std::string bytes;
    auto word=[&](Sint32 value) {
        const Uint32 bits=static_cast<Uint32>(value);
        for (int shift : {0,8,16,24}) bytes.push_back(static_cast<char>(bits>>shift));
    };
    for (int value : {0x434e5831,1,16,2,3,inputs,1,outputs,inputs,1}) word(value);
    for (int i=0; i<inputs; ++i) word(0);
    word(0); word(1); word(outputs);
    for (int i=0; i<outputs; ++i) word(0);
    for (int i=0; i<outputs; ++i) word(i==preferred ? 65536 : 0);
    return bytes;
}
using Score=ScoredAction (CortexPolicy::*)(const CortexObservation&,const CortexPolicy::DecideFacts&) const;
ScoredAction score(CortexPolicy& policy,Score fn,const CortexObservation& obs)
{
    return (policy.*fn)(obs,CortexPolicy::computeFacts(obs));
}
}

TEST_SUITE("CortexPolicyCoverage")
{
    TEST_CASE("learned policies load once retain feasibility masks and fall back on invalid files")
    {
        PolicyEnvironment environment;
        glob2test::TempDir scratch;
        glob2test::CapturedStderr diagnostics;
        auto obs=healthy();
        CortexPolicy hand;
        const auto expected=hand.decide(obs);
        for (const char* mode : {"", "unknown", "ml", "ml-decide"})
        {
            INFO(std::string(mode));
            glob2test::setEnv("GLOB2_CORTEX_POLICY",mode);
            glob2test::setEnv("GLOB2_CORTEX_NET",(scratch.path/"missing").string().c_str());
            glob2test::setEnv("GLOB2_CORTEX_DECISION_NET",(scratch.path/"missing").string().c_str());
            CortexPolicy fallback;
            const auto action=fallback.decide(obs);
            CHECK(action.kind==expected.kind); CHECK(action.buildingType==expected.buildingType);
            CHECK(action.locationSlot==expected.locationSlot);
        }
        const auto workerPath=scratch.path/"workers";
        glob2test::writeFile(workerPath,constantModel(16,20,0));
        glob2test::setEnv("GLOB2_CORTEX_POLICY","ml");
        glob2test::setEnv("GLOB2_CORTEX_NET",workerPath.string().c_str());
        CortexPolicy learnedWorkers;
        obs.swarmCount=1;
        auto& swarm=obs.trackedSwarms[0]; swarm.valid=1; swarm.maxUnitWorking=4;
        swarm.harvestableFoodSourcesNearby=100; swarm.supplyStock=0;
        CHECK(hand.tuneWorkers(obs).swarmWorkers[0]==5);
        CHECK(learnedWorkers.tuneWorkers(obs).swarmWorkers[0]==1);
        std::filesystem::remove(workerPath);
        CHECK(learnedWorkers.tuneWorkers(obs).swarmWorkers[0]==1);
        const auto decisionPath=scratch.path/"decisions";
        glob2test::writeFile(decisionPath,constantModel(48,18,10));
        glob2test::setEnv("GLOB2_CORTEX_POLICY","ml-decide");
        glob2test::setEnv("GLOB2_CORTEX_DECISION_NET",decisionPath.string().c_str());
        CortexPolicy learnedDecision;
        obs=healthy();
        DecideTrace trace;
        const auto decision=learnedDecision.decide(obs,&trace);
        CHECK(decision.kind==ACTION_UPGRADE_BUILDING);
        CHECK(decision.buildingType==CORTEX_BUILD_SCIENCE);
        CHECK((trace.eligibleMask & (1u<<10))!=0);
        obs.upgradableCount[CORTEX_BUILD_SCIENCE]=0;
        learnedDecision.decide(obs,&trace);
        CHECK((trace.eligibleMask & (1u<<10))==0);
        obs.valid=0;
        CHECK(learnedDecision.decide(obs).kind==ACTION_NOOP);
    }

    TEST_CASE("tech upgrades require trained units and decline below the boundary")
    {
        CortexPolicy policy;
        struct Upgrade { int type; Score score; };
        for (const auto& entry : {Upgrade{CORTEX_BUILD_ATTACK,&CortexPolicy::scoreBarracksUpgrade},
                                 Upgrade{CORTEX_BUILD_SCIENCE,&CortexPolicy::scoreSchoolUpgrade},
                                 Upgrade{CORTEX_BUILD_WALKSPEED,&CortexPolicy::scoreRacetrackUpgrade}})
        {
            CAPTURE(entry.type);
            auto obs=healthy();
            CHECK(score(policy,entry.score,obs).action.kind==ACTION_UPGRADE_BUILDING);
            CHECK(score(policy,entry.score,obs).action.buildingType==entry.type);
            obs.upgradableCount[entry.type]=0;
            CHECK(score(policy,entry.score,obs).score==0);
            obs=healthy(); obs.buildingCountPerLevel[entry.type][2]=1;
            CHECK(score(policy,entry.score,obs).score==0);
            obs=healthy(); obs.freeWorkers=0;
            CHECK(score(policy,entry.score,obs).score==0);
            obs=healthy(); obs.starvingUnits=2;
            CHECK(score(policy,entry.score,obs).score==0);
        }
        auto obs=healthy();
        obs.buildLevel[1]=5;
        CHECK(score(policy,&CortexPolicy::scoreSchoolUpgrade,obs).score==0);
        obs.buildLevel[1]=6;
        CHECK(score(policy,&CortexPolicy::scoreSchoolUpgrade,obs).action.kind==ACTION_UPGRADE_BUILDING);
        obs=healthy(); obs.buildingCountPerLevel[CORTEX_BUILD_ATTACK][1]=1;
        CHECK(score(policy,&CortexPolicy::scoreBarracksUpgrade,obs).action.kind==ACTION_BUILD);
    }

    TEST_CASE("first tech buildings require prerequisites and feasible locations")
    {
        CortexPolicy policy;
        struct Build { int type; Score score; };
        for (const auto& entry : {Build{CORTEX_BUILD_SCIENCE,&CortexPolicy::scoreSchool},
                                 Build{CORTEX_BUILD_WALKSPEED,&CortexPolicy::scoreRacetrack},
                                 Build{CORTEX_BUILD_HEAL,&CortexPolicy::scoreHospital},
                                 Build{CORTEX_BUILD_ATTACK,&CortexPolicy::scoreBarracks}})
        {
            CAPTURE(entry.type);
            auto obs=healthy(); obs.buildingCountPerLevel[entry.type][1]=0;
            const auto result=score(policy,entry.score,obs);
            CHECK(result.action.kind==ACTION_BUILD);
            CHECK(result.action.buildingType==entry.type);
            CHECK(result.action.locationSlot==1);
            obs.buildCandidates[entry.type][1].valid=0;
            CHECK(score(policy,entry.score,obs).score==0);
            obs.buildCandidates[entry.type][1].valid=1;
            obs.buildingCountPerLevel[entry.type][0]=1;
            CHECK(score(policy,entry.score,obs).score==0);
        }
        auto obs=healthy(); obs.algaeDiscovered=1;
        obs.buildCandidates[CORTEX_BUILD_SWIMSPEED][2].valid=1;
        CHECK(score(policy,&CortexPolicy::scoreSwimmingPool,obs).action.buildingType==CORTEX_BUILD_SWIMSPEED);
        obs.buildingCountPerLevel[CORTEX_BUILD_WALKSPEED][1]=0;
        CHECK(score(policy,&CortexPolicy::scoreSwimmingPool,obs).score==0);
        obs=healthy(); obs.buildingCountPerLevel[CORTEX_BUILD_SCIENCE][1]=0;
        obs.algaeReachable=0;
        CHECK(score(policy,&CortexPolicy::scoreSchool,obs).score==0);
    }

    TEST_CASE("inn upgrades retain enough feeding capacity and hospitals scale with army")
    {
        CortexPolicy policy;
        auto obs=healthy();
        obs.innCount=2;
        for (int i=0; i<2; ++i) { obs.trackedInns[i].valid=1; obs.trackedInns[i].maxUnitInside=30; }
        CHECK(score(policy,&CortexPolicy::scoreInnUpgrade,obs).action.kind==ACTION_UPGRADE_BUILDING);
        obs.feedCapacity=40;
        CHECK(score(policy,&CortexPolicy::scoreInnUpgrade,obs).action.kind==ACTION_BUILD);
        obs.buildingCountPerLevel[CORTEX_BUILD_FOOD][0]=1;
        CHECK(score(policy,&CortexPolicy::scoreInnUpgrade,obs).score==0);
        obs=healthy();
        CHECK(score(policy,&CortexPolicy::scoreHospitalExpandUpgrade,obs).action.kind==ACTION_UPGRADE_BUILDING);
        obs.warriors=16;
        CHECK(score(policy,&CortexPolicy::scoreHospitalExpandUpgrade,obs).action.kind==ACTION_BUILD);
    }

    TEST_CASE("worker tuning respects depletion floors settling and a shared construction budget")
    {
        CortexPolicy policy;
        auto obs=healthy(); obs.swarmCount=1;
        auto& swarm=obs.trackedSwarms[0]; swarm.valid=1; swarm.maxUnitWorking=4;
        swarm.harvestableFoodSourcesNearby=100; swarm.supplyStock=0;
        CHECK(policy.tuneWorkers(obs).swarmWorkers[0]==5);
        swarm.harvestableFoodSourcesNearby=0;
        CHECK(policy.tuneWorkers(obs).swarmWorkers[0]==CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP);
        swarm.harvestableFoodSourcesNearby=-1; swarm.supplyStock=100;
        CHECK(policy.tuneWorkers(obs).swarmWorkers[0]==3);
        swarm.valid=0;
        obs.innCount=1;
        auto& inn=obs.trackedInns[0]; inn.valid=1; inn.priority=CORTEX_PRIORITY_NORMAL;
        inn.maxUnitWorking=5; inn.nearestFoodSourceDistance=1; inn.restockTripsNeeded=2;
        inn.ticksSinceFinished=0;
        CHECK(policy.tuneWorkers(obs).kind==ACTION_NOOP);
        inn.ticksSinceFinished=CORTEX_INN_TUNE_DELAY_TICKS;
        CHECK(policy.tuneWorkers(obs).innWorkers[0]==2);
        inn.restockTripsNeeded=100;
        CHECK(policy.tuneWorkers(obs).innWorkers[0]==CORTEX_INN_WORKER_CAP);
        inn.nearestFoodSourceDistance=-1;
        CHECK(policy.tuneWorkers(obs).innWorkers[0]==CORTEX_INN_WORKER_MIN);
        inn.valid=0;
        obs.siteCount=2; obs.freeWorkers=3;
        for (auto& site : obs.trackedSites) { site.valid=1; site.priority=CORTEX_PRIORITY_LOW; site.maxUnitWorking=0; site.deliveriesLeft=2; }
        auto tune=policy.tuneWorkers(obs);
        CHECK(tune.siteWorkers[0]==2); CHECK(tune.siteWorkers[1]==1);
        obs.valid=0; CHECK(policy.tuneWorkers(obs).kind==ACTION_NOOP);
    }

    TEST_CASE("combat prioritizes serious defense and respects attack hold and amphibious capacity")
    {
        CortexPolicy policy;
        auto obs=healthy(); obs.flagTargets[0].valid=1;
        obs.flagTargetSupportDist[0]=1;
        CHECK(policy.decideCombat(obs).kind==ACTION_PLACE_WAR_FLAG);
        obs.campaignAmphibious=1; obs.swimWarriors=7;
        CHECK(policy.decideCombat(obs).kind==ACTION_NOOP);
        obs.swimWarriors=8;
        CHECK(policy.decideCombat(obs).kind==ACTION_PLACE_WAR_FLAG);
        obs=healthy(); obs.buildingsUnderAttack=1; obs.defenseTargets[0].valid=1;
        CHECK(policy.decideCombat(obs).kind==ACTION_PLACE_DEFENSE_FLAG);
        obs.flagPosture=CORTEX_POSTURE_OFFENSE; obs.tick=10; obs.offenseHoldUntil=20;
        CHECK(policy.decideCombat(obs).kind==ACTION_NOOP);
        obs.buildingsUnderAttack=CORTEX_DEFENSE_SERIOUS_BUILDINGS;
        CHECK(policy.decideCombat(obs).kind==ACTION_PLACE_DEFENSE_FLAG);
        obs=healthy(); obs.warFlagsActive=1;
        CHECK(policy.decideCombat(obs).kind==ACTION_CLEAR_FLAGS);
        obs.enemyUnitsNearFlag=1;
        CHECK(policy.decideCombat(obs).kind==ACTION_NOOP);
    }

    TEST_CASE("protection hunger and bootstrap decisions honor observation validity")
    {
        CortexPolicy policy;
        auto obs=makeEmptyObservation();
        CHECK(policy.decide(obs).kind==ACTION_NOOP);
        CHECK_FALSE(policy.wantFoodSourceProtection(obs));
        obs.valid=1; obs.totalUnit=100; obs.wheatProtectAddCount=1;
        CHECK(policy.wantFoodSourceProtection(obs));
        obs.starvingUnits=5; CHECK(policy.wantFoodSourceProtection(obs));
        obs.starvingUnits=6; CHECK_FALSE(policy.wantFoodSourceProtection(obs));
        obs=makeEmptyObservation(); obs.valid=1; obs.totalUnit=4;
        obs.buildCandidates[CORTEX_BUILD_FOOD][0].valid=1;
        CHECK(policy.decide(obs).buildingType==CORTEX_BUILD_FOOD);
        obs.buildingCountPerLevel[CORTEX_BUILD_FOOD][0]=1;
        CHECK(policy.decide(obs).kind==ACTION_NOOP);
        obs=healthy(); obs.starvingUnits=2; obs.flagTargets[0].valid=1;
        CHECK(policy.wantFoodBurstLift(obs));
        obs.flagTargets[0].valid=0; CHECK_FALSE(policy.wantFoodBurstLift(obs));
    }
}
