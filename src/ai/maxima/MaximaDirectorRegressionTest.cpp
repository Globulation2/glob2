#include "MaximaObservationFixture.h"
// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "EngineFixtures.h"
#include "Version.h"
#include "FileFormatVersions.h"
#include "WinningConditions.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"
#include <memory>
#include <algorithm>
#include <array>
#include <limits>
#include <locale>
#include <tuple>
#include <type_traits>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>
// Access the scheduler boundary without adding a production testing API.
#include "../../src/ai/maxima/AIMaximaRuntime.h"
#include "../../src/ai/maxima/AIMaxima.h"
#include "../../src/building/Building.h"
#include "BuildingType.h"
#include <nlohmann/json.hpp>
#include "../../src/building/IntBuildingType.h"
#include "../../src/unit/Unit.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <FileManager.h>
#include <iostream>
#include <memory>

using namespace AIMaximaRuntime;

// Director decisions must survive scheduling, diplomacy and runtime lifecycles.
namespace director_regressions
{
using namespace AIMaxima;
struct Fixture
{
    Game game;
    Player player;
    std::unique_ptr<Maxima> ai;

    Fixture() : game(NULL)
    {
        game.map.setSize(6,6,GRASS);
        game.map.setGame(&game);
        for(int team=0; team<3; ++team) {
            game.addTeam();
            game.teams[team]->race.loadDefault();
            game.teams[team]->playersMask=1u<<team;
        }
        player.setTeam(game.teams[0]);
        ai.reset(new Maxima(&player));
        for(int y=0; y<64; ++y) for(int x=0; x<64; ++x) {
            game.map.setMapDiscovered(x,y,player.team->me);
            // Loading a playable map clears these; setSize alone does not.
            game.map.clearImmobileUnit(x,y);
        }
        ai->timer=100;
        ai->posture=Maxima::PostureCampaign;
        ai->snapshot.population=150;
        ai->snapshot.workers=100;
        ai->snapshot.trained_warriors=12;
        ai->budget.defense_reserve=0;
        ai->budget.food_emergency=false;
        ai->budget.colony_emergency=false;
        ai->strategy.tactics.enabled=true;
        ai->strategy.tactics.siege_enabled=true;
        ai->strategy.tactics.min_force=4;
        ai->strategy.raiding.enabled=false;
    }

    ::Building* building(int x, int y, int team, const char* type="inn", int level=0)
    {
        ::Building* result=game.addBuilding(game.map.normalizeX(x),game.map.normalizeY(y),
            game.buildingsTypes.getTypeNum(type,level,false),team);
        REQUIRE(result);
        return result;
    }

    int id(::Building* building)
    {
        for(auto record:ai->context.buildings.found())
            if(record.second.gid==building->gid) return record.first;
        REQUIRE(false);
        return -1;
    }

    Unit* warrior(int x, int y, int level=1)
    {
        Unit* unit=game.addUnit(x,y,0,WARRIOR,level,0,0,0);
        REQUIRE(unit);
        unit->medical=Unit::MED_FREE;
        unit->activity=Unit::ACT_RANDOM;
        return unit;
    }

    void attach(Unit* unit, ::Building* flag)
    {
        unit->attachedBuilding=flag;
        unit->activity=Unit::ACT_FLAG;
        flag->unitsWorking.push_back(unit);
    }

    void remember(::Building* building)
    {
        ai->reconnaissance.beginObservation(ai->timer,{building->owner->teamNumber});
        ai->reconnaissance.observeBuilding(Recon::BuildingSighting(building->gid,
            building->owner->teamNumber,building->typeNum,
            building->posX,building->posY,building->type->width,
            building->type->height,false,ai->timer));
        ai->reconnaissance.finishObservation();
    }

    void islands()
    {
        for(int y=0; y<64; ++y) for(int x=0; x<64; ++x) {
            const bool land=y>=5 && y<=55 && ((x>=5 && x<=19) || (x>=26 && x<=45));
            game.map.paintCell(x, y, land ? GRASS : WATER);
        }
        ai->context.gradients.invalidate();
    }
};



static void quarantineBudgetContinuation()
{
    Fixture original;
    auto& a=*original.ai;
    a.strategy.tactics.failed_target_quarantine_ticks=2468;
    // Deliberately differ from both the constructor and canonical strategy:
    // current saves must preserve the executable budget, not reconstruct it.
    a.budget.tactical_quarantine_ticks=1234;
    a.director.initialized=true;
    a.director.dirty=false;
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    glob2test::withMaximaObservation(a.context,[&] { a.saveExecutionState(&output); });
    output.flush();
    const std::string current(backend->getBuffer(),backend->getPosition());
    REQUIRE(current.size()>4);
    // Locate the old payload boundary by reading it through the preceding reader.
    Fixture preceding;
    auto* prefixBackend=new GAGCore::MemoryStreamBackend(current.data(),current.size());
    GAGCore::BinaryInputStream prefixInput(prefixBackend);prefixInput.seekFromStart(0);
    glob2test::withMaximaObservation(preceding.ai->context,[&] {
        preceding.ai->loadExecutionState(&prefixInput,FILE_FORMAT_VERSION_MAXIMA_PRESTIGE-1);
    });
    const size_t legacyBoundary=prefixBackend->getPosition();
    for (bool legacy : {false,true}) {
        auto bytes=current;
        // Format 165 has the identical preceding execution payload, but no
        // appended quarantine word. The sentinel catches accidental overreads.
        if (legacy) bytes.resize(legacyBoundary-4);
        bytes.append("\x12\x34\x56\x78",4);
        Fixture restored;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        glob2test::withMaximaObservation(restored.ai->context,[&] {
            restored.ai->loadExecutionState(&input,legacy ? FILE_FORMAT_VERSION_INTRUSIVE_POPULATION-1 : VERSION_MINOR);
        });
        CHECK(input.readUint32("sentinel")==0x12345678u);
        CHECK(restored.ai->director.initialized);
        CHECK_FALSE(restored.ai->director.dirty);
        CHECK(restored.ai->strategy.tactics.failed_target_quarantine_ticks==2468);
        CHECK(restored.ai->budget.tactical_quarantine_ticks==(legacy ? 2468 : 1234));
    }
    for (int invalid : {-1,1000001}) {
        auto bytes=current;
        const Uint32 bits=Uint32(invalid);
        for (unsigned i=0;i<4;++i)
            bytes[legacyBoundary-4+i]=char(bits>>(24-8*i));
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        CHECK_THROWS_AS(glob2test::withMaximaObservation(a.context,[&] {
            a.loadExecutionState(&input,VERSION_MINOR);
        }),std::runtime_error);
    }
}

static void prestigeEconomyAndCancellation()
{
    Fixture f;auto& a=*f.ai;auto& c=a.context;
    CHECK(a.strategy.prestige.enabled);
    a.strategy.prestige.enabled=true;
    auto* school=f.building(10,10,0,"school",1);
    f.building(35,35,1);
    c.initialize();
    a.environment.accessible_corn=a.environment.accessible_wood=1000;
    a.environment.accessible_stone=a.environment.accessible_algae=1000;
    a.demands.technology=40;
    a.snapshot.population=100;a.snapshot.workers=80;
    auto& stat=*f.player.team->stats.getLatestStat();
    stat.workersByConstructionLevel[2]=30;stat.totalUnit=100;
    f.game.prestigeToReach=500;f.game.totalPrestige=450;
    f.game.teams[0]->prestige=150;f.game.teams[1]->prestige=200;
    f.game.teams[2]->prestige=100;
    glob2test::withMaximaObservation(c,[&] {
        CHECK(c.observation().prestigeWinCondition);
        CHECK(c.observation().prestigeToReach==500);
        a.assess_prestige(c);
        CHECK(a.prestige_race.state==Prestige::Finish);
        CHECK(a.prestige_race.gain==50);
        a.allocate_resources(c);a.finalize_director_plan(c);
        CHECK(a.budget.prestige_target_level==3);
        CHECK(a.budget.prestige_desired_buildings==1);
        CHECK(a.budget.prestige_priority==100);
        CHECK(a.budget.desired_schools==1);
    });
    // A public-score change makes this completion lose, so issue is vetoed.
    f.game.teams[0]->prestige=100;
    f.game.teams[2]->prestige=150;
    // A recent completion can precede the next 32-tick total refresh. The
    // individual scores already imply 450 even while the cached total is 400.
    f.game.totalPrestige=400;
    const int root=a.prestige_race.buildingType;
    glob2test::withMaximaObservation(c,[&] {
        CHECK_FALSE(a.prestige_action_allowed(c,root,2,3));
        a.assess_prestige(c);CHECK(a.prestige_race.reason==Prestige::CannotCatchUp);
    });
    REQUIRE(school->launchConstruction(0,0));
    glob2test::withMaximaObservation(c,[&] {a.guard_prestige_completions(c);});
    REQUIRE(a.prestige_cancellations.size()==1);
    REQUIRE(c.orders.size()==1);
    auto cancel=c.orders.front();c.orders.pop_front();
    REQUIRE(dynamic_cast<OrderCancelConstruction*>(cancel.get()));
    glob2test::withMaximaObservation(c,[&] {a.guard_prestige_completions(c);});
    CHECK(c.orders.empty());
    a.orderExecutionCompleted(*cancel,false);
    CHECK(a.prestige_cancellations.empty());
    glob2test::withMaximaObservation(c,[&] {a.guard_prestige_completions(c);});
    REQUIRE(c.orders.size()==1);cancel=c.orders.front();c.orders.pop_front();
    a.orderExecutionCompleted(*cancel,true);
    school->cancelConstruction(0);
    glob2test::withMaximaObservation(c,[&] {a.guard_prestige_completions(c);});
    CHECK(a.prestige_cancellations.empty());CHECK_FALSE(school->type->isBuildingSite);
    glob2test::withMaximaObservation(c,[&] {CHECK_FALSE(a.prestige_action_allowed(c,root,2,3));});
    // New standings permit a restart. Disabling the policy restores ordinary authorization.
    f.game.totalPrestige=350;
    f.game.teams[2]->prestige=50;
    glob2test::withMaximaObservation(c,[&] {CHECK(a.prestige_action_allowed(c,root,2,3));});
    f.game.totalPrestige=450;
    f.game.teams[2]->prestige=150;
    // A repair uses the same opcode but adds no prestige and must stay queued.
    --school->hp;
    glob2test::withMaximaObservation(c,[&] {c.push_order(std::make_shared<OrderConstruction>(school->gid,0,0));a.guard_prestige_completions(c);});
    CHECK(c.orders.size()==1);c.orders.clear();++school->hp;
    glob2test::withMaximaObservation(c,[&] {c.push_order(std::make_shared<OrderConstruction>(school->gid,0,0));a.guard_prestige_completions(c);});
    CHECK(c.orders.empty());
    a.strategy.prestige.enabled=false;f.game.totalPrestige=450;
    glob2test::withMaximaObservation(c,[&] {
        CHECK(a.prestige_action_allowed(c,root,2,3));a.assess_prestige(c);a.build_policy_bids();
        CHECK(a.policy_bids[Maxima::PolicyPrestige].utility==0);
    });
    a.strategy.prestige.enabled=true;
    WinningCondition::setPrestigeWinCondition(f.game.gameHeader.getWinningConditions(),false);
    glob2test::withMaximaObservation(c,[&] {
        CHECK_FALSE(c.observation().prestigeWinCondition);a.assess_prestige(c);
        CHECK(a.prestige_race.reason==Prestige::Disabled);
        CHECK(a.prestige_action_allowed(c,root,2,3));
    });
}

// Exercise the real assessment -> fresh allocation -> arbitration -> execution
// handoff. Directly building bids would miss a reset between these stages.
static void prestigeAllocatedPipeline()
{
    using namespace AIMaximaPlacement;
    for(const auto& levels:std::vector<std::vector<int>>{{0},{1},{2},{0,1,2},{2,2}}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;
        a.strategy.prestige.enabled=true;a.demands.technology=20;
        a.environment.accessible_corn=a.environment.accessible_wood=1000;
        a.environment.accessible_stone=a.environment.accessible_algae=1000;
        a.snapshot.population=100;a.snapshot.workers=80;a.snapshot.schools=int(levels.size());
        ::Building* firstSchool=nullptr;
        a.applied_farm_protection_mask.assign(64*64,0);
        int finished=0;
        for(size_t i=0;i<levels.size();++i) {
            auto* school=f.building(10+int(i)*10,10,0,"school",levels[i]);
            if(!firstSchool)firstSchool=school;
            finished+=levels[i]==2;
        }
        REQUIRE(f.game.addUnit(4,4,0,WORKER,2,0,0,0));
        auto& stat=*f.player.team->stats.getLatestStat();
        stat.workersByConstructionLevel[2]=30;stat.totalUnit=100;
        f.game.prestigeToReach=1000;f.game.totalPrestige=finished*50;
        f.player.team->prestige=finished*50;c.initialize();
        glob2test::withMaximaObservation(c,[&] {
            a.allocate_resources(c);a.finalize_director_plan(c);
            CHECK(a.budget.prestige_target_level==3);
            CHECK(a.budget.prestige_desired_buildings==finished+1);
            CHECK(a.budget.desired_schools==finished+1);
            const int root=a.budget.prestige_building_type;
            const auto world=a.collect_development_world(c);
            int requests=0,unmet=0;
            for(const auto& intent:a.collect_development_intents(world))
                if(intent.buildingType==root && intent.purpose==CoreCapacity) {
                    ++requests;unmet+=intent.unmetCount;
                }
            CHECK(requests<=1);
            CHECK(unmet==std::max(0,finished+1-int(levels.size())));
            if(levels==std::vector<int>{0}) {
                REQUIRE(a.budget.prestige_priority>0);
                CHECK(a.budget.allow_upgrades);
                CHECK(a.collect_development_limits(c).upgradePriority(root,1)==a.budget.prestige_priority);
            }
        });
        // Mutate live construction only between immutable observation scopes.
        if(levels==std::vector<int>{0}) {
            REQUIRE(firstSchool);REQUIRE(firstSchool->launchConstruction(0,0));
        }
        if(levels==std::vector<int>{0})glob2test::withMaximaObservation(c,[&] {
            a.allocate_resources(c);a.finalize_director_plan(c);
            CHECK(a.budget.prestige_desired_buildings==1);
            for(const auto& intent:a.collect_development_intents(a.collect_development_world(c)))
                if(intent.buildingType==a.budget.prestige_building_type && intent.purpose==CoreCapacity)
                    CHECK(intent.unmetCount==0);
        });
        // Ordinary demand combines by max, never an additive second request.
        a.snapshot.population=210;a.demands.technology=100;
        glob2test::withMaximaObservation(c,[&] {
            a.allocate_resources(c);a.finalize_director_plan(c);
            CHECK(a.budget.desired_schools==std::max(3,finished+1));
        });
        a.snapshot.population=100;a.demands.technology=0;
        // A new allocation must clear the previous active policy contribution.
        a.strategy.prestige.enabled=false;
        glob2test::withMaximaObservation(c,[&] {
            a.allocate_resources(c);a.finalize_director_plan(c);
            CHECK(a.budget.prestige_priority==0);
            CHECK(a.budget.prestige_target_level==0);
            CHECK(a.budget.prestige_desired_buildings==0);
            CHECK(a.budget.desired_schools==0);
        });
    }
}

struct PrestigeCapacityFixture : Fixture
{
    ::Building* school;
    std::vector<Unit*> workers;
    explicit PrestigeCapacityFixture(int count=56)
    {
        school=building(10,10,0,"school",1);
        for(int index=0;index<count;++index) {
            auto* unit=game.addUnit(30+index%16,30+index/16,0,WORKER,2,0,0,0);
            REQUIRE(unit);unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;
            unit->jobPurpose=UnitJobPurpose::None;workers.push_back(unit);
        }
        auto& a=*ai;a.strategy.prestige.enabled=true;a.strategy.prestige.max_pipelines=4;
        a.strategy.staffing.construction_training_workers=5;
        a.strategy.upgrades.level1_workers=8;a.strategy.upgrades.level2_workers=12;
        a.environment.accessible_corn=a.environment.accessible_wood=1000;
        a.environment.accessible_stone=a.environment.accessible_algae=1000;
        a.snapshot.population=100;a.snapshot.workers=count;a.snapshot.schools=1;
        a.demands.technology=20;a.labour_plan.trainingReserve=8;
        auto& stat=*player.team->stats.getLatestStat();
        stat.totalUnit=100;stat.workersByConstructionLevel[2]=count;
        ai->context.initialize();ai->initialize_farming_cache(ai->context);
    }
    int allocate()
    {
        game.snapshotStore().invalidateBoundary();
        return glob2test::withMaximaObservation(ai->context,[&] {
            ai->allocate_resources(ai->context);ai->finalize_director_plan(ai->context);
            REQUIRE(ai->prestige_race.state!=Prestige::Hold);
            return ai->budget.prestige_desired_buildings;
        });
    }
    Prestige::PipelineCapacity capacity()
    {
        return glob2test::withMaximaObservation(ai->context,[&] {
            const auto& profiles=ai->collect_building_profiles();
            const auto profile=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p) {
                return p.buildingType==ai->budget.prestige_building_type;
            });REQUIRE(profile!=profiles.end());
            Prestige::PipelineCapacity facts;
            ai->prestige_pipeline_allowance(*profile,&facts);
            return facts;
        });
    }
};

static void prestigeAdaptiveCapacity()
{
    for(const auto [workers,expected]:std::vector<std::pair<int,int>>{{20,1},{32,2},{44,3},{56,4}}) {
        PrestigeCapacityFixture f(workers);
        CHECK(f.allocate()==expected);
        CHECK(f.ai->budget.desired_schools==expected);
        const auto facts=f.capacity();
        CHECK(facts.sampled);CHECK(facts.qualifiedIdle==workers);CHECK(facts.rootAssigned==0);
        CHECK(facts.trainingReserve==8);CHECK(facts.crew==12);
        CHECK(facts.workerLimit==expected);CHECK(facts.materialLimit==4);
    }
    PrestigeCapacityFixture f;
    REQUIRE(f.allocate()==4);
    auto& a=*f.ai;auto* unit=f.workers.front();
    const auto check=[&](int expected) {CHECK(f.allocate()==expected);};
    unit->medical=Unit::MED_DAMAGED;check(3);unit->medical=Unit::MED_FREE;
    unit->medical=Unit::MED_HUNGRY;check(3);
    f.game.gameHeader.setHungerDisabled(true);check(4);
    f.game.gameHeader.setHungerDisabled(false);unit->medical=Unit::MED_FREE;
    unit->activity=Unit::ACT_UPGRADING;check(3);
    unit->activity=Unit::ACT_FILLING;check(3);unit->activity=Unit::ACT_RANDOM;
    const auto buildLevel=unit->level[BUILD],constructionLevel=unit->constructionLevel;
    unit->level[BUILD]=3;unit->constructionLevel=0;check(3); // Fast construction clock is not qualification.
    unit->level[BUILD]=0;unit->constructionLevel=constructionLevel;check(4); // Qualification can be learned independently.
    unit->level[BUILD]=buildLevel;
    for(auto purpose:{UnitJobPurpose::Defend,UnitJobPurpose::Explore,UnitJobPurpose::Clear}) {
        unit->jobPurpose=purpose;check(3);
    }
    unit->jobPurpose=UnitJobPurpose::None;
    const auto flags=unit->capabilityFlags;unit->capabilityFlags&=~UnitRuntimeTraits::Construct;
    check(3);unit->capabilityFlags=flags;
    unit->capabilityFlags&=~UnitRuntimeTraits::Transport;check(3);unit->capabilityFlags=flags;
    const auto buildSpeed=unit->performance[BUILD];unit->performance[BUILD]=0;
    check(3);unit->performance[BUILD]=buildSpeed;
    for(int configured:{1,2,3,4}) {a.strategy.prestige.max_pipelines=configured;check(configured);}
    a.strategy.prestige.max_pipelines=1;a.labour_plan.trainingReserve=1000;
    check(1); // Explicit legacy mode retains its original single request.
    a.strategy.prestige.enabled=false;
    f.game.snapshotStore().invalidateBoundary();
    glob2test::withMaximaObservation(a.context,[&] {
        a.allocate_resources(a.context);a.finalize_director_plan(a.context);
        CHECK(a.budget.prestige_desired_buildings==0);
        const auto& profile=a.collect_building_profiles().front();
        CHECK(a.prestige_pipeline_allowance(profile)==0);
    });
    a.strategy.prestige.enabled=true;a.strategy.prestige.max_pipelines=4;
    WinningCondition::setPrestigeWinCondition(f.game.gameHeader.getWinningConditions(),false);
    f.game.snapshotStore().invalidateBoundary();
    glob2test::withMaximaObservation(a.context,[&] {
        a.allocate_resources(a.context);a.finalize_director_plan(a.context);
        CHECK(a.budget.prestige_desired_buildings==0);
        CHECK(a.prestige_pipeline_allowance(a.collect_building_profiles().front())==0);
    });
}

static void prestigeAdaptiveBacklogAndBuilders()
{
    using namespace AIMaximaPlacement;
    {
        PrestigeCapacityFixture f;REQUIRE(f.allocate()==4);
        REQUIRE(f.school->launchConstruction(12,0));
        CHECK_FALSE(f.school->type->isBuildingSite);
        CHECK(f.school->constructionResultState==Building::UPGRADE);
        for(int index=0;index<12;++index) {
            auto* unit=f.workers[index];unit->attachedBuilding=f.school;
            unit->activity=Unit::ACT_FILLING;unit->jobPurpose=UnitJobPurpose::Transport;
            f.school->unitsWorking.push_back(unit);
        }
        CHECK(f.allocate()==4); // Assigned prestige builders remain part of its capacity.
        const auto facts=f.capacity();CHECK(facts.qualifiedIdle==44);CHECK(facts.rootAssigned==12);
        CHECK_FALSE(glob2test::withMaximaObservation(f.ai->context,[&] {
            return f.ai->prestige_action_allowed(f.ai->context,f.ai->budget.prestige_building_type,2,3);
        })); // Parallel preparation does not authorize parallel positive completions.
    }
    {
        PrestigeCapacityFixture f;
        auto* inn=f.building(4,4,0,"inn",0);
        REQUIRE(inn->launchConstruction(12,0));
        CHECK_FALSE(inn->type->isBuildingSite);
        CHECK(inn->maxUnitWorking==12);
        CHECK(f.allocate()==3); // Launched ordinary work already reserves its crew.
    }
    {
        PrestigeCapacityFixture f;
        const int innSite=f.game.buildingsTypes.getTypeNum("inn",0,true);
        auto* site=f.game.addBuilding(4,4,innSite,0,12,0);REQUIRE(site);
        site->desiredMaxUnitWorking=12;
        f.game.snapshotStore().invalidateBoundary();
        glob2test::withMaximaObservation(f.ai->context,[&]{f.ai->context.buildings.initiate();});
        CHECK(f.allocate()==3);
        CHECK(f.capacity().ordinaryUnmet==12);
        DevelopmentAction observed;observed.id=1;observed.state=SiteObserved;
        observed.type=BuildStandalone;observed.buildingType=innSite;
        observed.buildingId=f.id(site);observed.workers=12;
        observed.centerX=site->posX-site->type->decLeft;
        observed.centerY=site->posY-site->type->decTop;
        f.ai->development_planner.actionMap[1]=observed;
        CHECK(f.allocate()==3); // The observed site's crew is not charged twice.
        site->maxUnitWorking=site->desiredMaxUnitWorking=0;
        CHECK(f.allocate()==3); // Its committed staffing order may still be pending.
        f.ai->development_planner.actionMap[1].buildingId=-1;
        CHECK(f.allocate()==3); // The parcel is enough while its register ID is unresolved.
        DevelopmentAction pending=observed;pending.id=2;pending.buildingId=-1;pending.state=CreateIssued;
        pending.centerX+=20;pending.centerY+=20;
        f.ai->development_planner.actionMap[2]=pending;
        CHECK(f.allocate()==2); // Unobserved commitments still reserve their crew.
        CHECK(f.capacity().unobservedCrews==12);
        for(int index=0;index<12;++index) {
            auto* unit=f.workers[index];unit->attachedBuilding=site;
            unit->activity=Unit::ACT_FILLING;unit->jobPurpose=UnitJobPurpose::Transport;
            site->unitsWorking.push_back(unit);
        }
        CHECK(f.allocate()==2); // Filled ordinary requests no longer subtract idle labour again.
        CHECK(f.capacity().ordinaryUnmet==0);CHECK(f.capacity().qualifiedIdle==44);
    }
}

static void prestigeAdaptiveMaterialsAndFinish()
{
    {
        PrestigeCapacityFixture f;auto& a=*f.ai;REQUIRE(f.allocate()==4);
        a.environment.accessible_corn=a.environment.accessible_wood=0;
        a.environment.accessible_stone=a.environment.accessible_algae=0;
        std::array<int,MaterialCount> cost{},nextCost{};
        glob2test::withMaximaObservation(a.context,[&] {
            const auto& profiles=a.collect_building_profiles();
            const auto profile=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p) {
                return p.buildingType==a.budget.prestige_building_type;
            });REQUIRE(profile!=profiles.end());
            for(const auto& level:profile->levels)if(level.level<=a.budget.prestige_target_level)
                for(int material=0;material<MaterialCount;++material)cost[material]+=level.constructionMaterials[material];
            std::copy_n(profile->atLevel(a.budget.prestige_target_level)->constructionMaterials,MaterialCount,nextCost.begin());
        });
        for(int material=0;material<MaterialCount;++material)f.player.team->teamMaterials[material]=2*cost[material];
        CHECK(f.allocate()==2);
        CHECK(f.capacity().materialLimit==2);
        for(int material=0;material<MaterialCount;++material)
            f.player.team->reservedTeamMaterials[material]=cost[material];
        CHECK(f.allocate()==1); // Ordinary reservations cannot fund extra prestige work.
        for(int material=0;material<MaterialCount;++material) {
            f.player.team->teamMaterials[material]=0;
            f.player.team->reservedTeamMaterials[material]=0;
            f.school->localMaterials[material]=nextCost[material];
        }
        CHECK(f.allocate()==1); // Only the existing building can spend its private stores.
        CHECK(f.capacity().materialLimit==1);
    }
    {
        PrestigeCapacityFixture f;auto& a=*f.ai;
        for(int index=0;index<8;++index)f.building(4+index*5,20,0,"school",2);
        f.game.prestigeSyncStep();
        f.game.snapshotStore().invalidateBoundary();
        glob2test::withMaximaObservation(a.context,[&]{a.context.buildings.initiate();});
        CHECK(f.allocate()==10); // Eight finished providers plus two steps to the threshold.
        REQUIRE(a.prestige_race.finishSteps==2);
        CHECK(f.capacity().remainingSteps==2);CHECK(f.capacity().workerLimit==4);
        f.building(44,20,0,"school",2);f.game.prestigeSyncStep();
        f.game.snapshotStore().invalidateBoundary();
        glob2test::withMaximaObservation(a.context,[&]{a.context.buildings.initiate();});
        CHECK(f.allocate()==10); // A winning final step always has allowance one.
        CHECK(a.prestige_race.state==Prestige::Finish);
        CHECK_FALSE(f.capacity().sampled);
    }
}

static void prestigeAdaptiveCompletedCommitments()
{
    using namespace AIMaximaPlacement;
    PrestigeCapacityFixture f;auto& a=*f.ai;
    auto* inn=f.building(4,4,0,"inn",0);
    auto* upgraded=f.building(16,4,0,"inn",1);
    const int innRoot=f.game.buildingsTypes.getTypeNum("inn",0,true);
    f.game.snapshotStore().invalidateBoundary();
    glob2test::withMaximaObservation(a.context,[&]{a.context.buildings.initiate();});
    const int innId=f.id(inn),upgradedId=f.id(upgraded);
    DevelopmentAction action;action.id=1;action.state=SiteObserved;
    action.type=BuildStandalone;action.buildingType=innRoot;
    action.buildingId=innId;action.fromLevel=0;action.targetLevel=1;action.workers=12;
    action.centerX=inn->posX-inn->type->decLeft;action.centerY=inn->posY-inn->type->decTop;
    a.development_planner.actionMap[1]=action;
    CHECK(f.allocate()==4); // The live completed build outruns the planner's stale state.
    CHECK(f.capacity().unobservedCrews==0);
    auto& stale=a.development_planner.actionMap[1];
    stale.type=UpgradeBuilding;stale.fromLevel=1;stale.targetLevel=2;
    CHECK(f.allocate()==3); // A completed old level does not prove the upgrade finished.
    stale.buildingId=upgradedId;
    CHECK(f.allocate()==4);
    stale.type=RepairBuilding;stale.buildingId=innId;stale.targetLevel=1;
    --inn->hp;CHECK(f.allocate()==3);
    ++inn->hp;CHECK(f.allocate()==4);
    stale.type=BuildStandalone;stale.targetLevel=1;
    const int gid=inn->gid;const auto generation=inn->scriptIdentity;
    REQUIRE(f.game.removeUnitAndBuildingAndFlags(4,4,Game::DEL_BUILDING));
    auto* replacement=f.building(4,4,0,"inn",0);
    REQUIRE(replacement->gid==gid);REQUIRE(replacement->scriptIdentity!=generation);
    CHECK(f.allocate()==3); // Same slot, type and parcel still lack the registered generation.
    CHECK(f.capacity().unobservedCrews==12);
}

static void prestigeAdaptiveContinuation()
{
    PrestigeCapacityFixture original,restored;
    auto& a=*original.ai;auto& b=*restored.ai;
    a.strategy.prestige.max_pipelines=3;
    REQUIRE(original.allocate()==3);
    auto* memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    glob2test::withMaximaObservation(a.context,[&]{a.save(&output);});output.flush();
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
        std::string(memory->getBuffer(),memory->getPosition())));
    REQUIRE(b.load(&input,&restored.player,VERSION_MINOR));
    CHECK(b.strategy.prestige.max_pipelines==3);
    CHECK(b.budget.prestige_desired_buildings==3);
    CHECK(restored.allocate()==3);
}


static void prestigeBuildingLimitContinuation()
{
    constexpr int buildingLimit=Building::MAX_COUNT;
    // Seven teams have a real 1050-point threshold. A supported one-point
    // catalog can fill all 1024 building slots without ending the race.
    glob2test::HeadlessGame world({.wDec=8,.hDec=8,.teams=7,
        .discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& game=world.game;
    auto catalog=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    const int finalSchool=game.buildingsTypes.getTypeNum("school",2,false);
    REQUIRE(finalSchool>=0);
    catalog["variants"][finalSchool]["properties"]["prestige"]=1;
    game.buildingsTypes.loadSnapshotJson(catalog.dump());game.configureBuildingCatalog();
    GameHeader header=game.gameHeader;header.setNumberOfPlayers(7);
    header.setBuildingCatalogSnapshot(game.buildingsTypes.snapshotJson());
    header.setHungerDisabled(true);
    for(int team=0;team<7;++team)
        header.getBasePlayer(team)=BasePlayer(team,"building-limit",team,team==0
            ?BasePlayer::playerTypeFromImplementationID(AI::MAXIMA):BasePlayer::P_LOCAL);
    game.setGameHeader(header,true);

    const auto* shape=game.buildingsTypes.get(finalSchool);
    const int stride=std::max(shape->width,shape->height)+2;
    constexpr int columns=32;
    REQUIRE(8+columns*stride<game.map.getW());
    REQUIRE(8+((buildingLimit+columns-1)/columns)*stride<game.map.getH());
    for(int index=0;index<buildingLimit;++index) {
        auto* school=world.addBuilding("school",8+(index%columns)*stride,
            8+(index/columns)*stride,2);
        REQUIRE(school);
        REQUIRE(Building::GIDtoID(school->gid)==index);
    }
    constexpr int workers=80;
    for(int index=0;index<workers;++index)
        REQUIRE(world.addUnit(WORKER,160+index%16,160+index/16,0,2));
    for(int team=1;team<7;++team)REQUIRE(world.addUnit(WORKER,160+team,180,team));
    auto* own=game.teams[0];
    for(MaterialId material:{MaterialId::Food,MaterialId::Wood,MaterialId::Stone,MaterialId::Algae})
        own->teamMaterials[materialIndex(material)]=1000;
    auto& stat=*own->stats.getLatestStat();
    stat.totalUnit=stat.numberUnitPerType[WORKER]=workers;
    stat.workersByConstructionLevel[2]=workers;
    REQUIRE(own->liveBuildings.size()==std::size_t(buildingLimit));
    REQUIRE(own->prestige==buildingLimit);
    REQUIRE(game.prestigeToReach==1050);
    game.prestigeSyncStep();
    REQUIRE(game.totalPrestige==buildingLimit);
    REQUIRE_FALSE(game.totalPrestigeReached);

    auto* ai=dynamic_cast<Maxima*>(game.players[0]->ai->aiImplementation);REQUIRE(ai);
    auto& a=*ai;auto& c=a.context;
    a.ensure_strategy();a.strategy.prestige.enabled=true;
    game.gameHeader.setAIConfig(0,a.canonicalStrategy());
    a.posture=Maxima::PostureCampaign;a.demands.technology=20;
    a.snapshot.population=a.snapshot.workers=workers;
    a.snapshot.trained_workers=a.snapshot.trained_workers_level2=workers;
    a.snapshot.schools=a.snapshot.school_level3=buildingLimit;
    game.snapshotStore().invalidateBoundary();c.initialize();
    glob2test::withMaximaObservation(c,[&] {
        a.allocate_resources(c);a.finalize_director_plan(c);
        REQUIRE(a.prestige_race.state==Prestige::Prepare);
        CHECK(a.prestige_race.own==buildingLimit);
        CHECK(a.prestige_race.remaining==1050-buildingLimit);
        CHECK(a.budget.prestige_target_level==3);
        CHECK(a.budget.prestige_desired_buildings==buildingLimit);
        CHECK(a.budget.desired_schools==buildingLimit);
    });

    auto* memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    game.save(&output,false,"prestige-building-limit");output.flush();
    const std::string bytes(memory->getBuffer(),memory->getPosition());
    REQUIRE(bytes.size()<32*1024*1024);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(std::string(bytes)));
    GameGUI restored;REQUIRE(restored.game.load(&input));
    CHECK(restored.game.prestigeToReach==1050);
    CHECK(restored.game.teams[0]->liveBuildings.size()==std::size_t(buildingLimit));
    CHECK(restored.game.teams[0]->prestige==buildingLimit);
    auto* resumed=dynamic_cast<Maxima*>(restored.game.players[0]->ai->aiImplementation);REQUIRE(resumed);
    CHECK(resumed->prestige_race.state==Prestige::Prepare);
    CHECK(resumed->budget.prestige_desired_buildings==buildingLimit);
    CHECK(resumed->budget.desired_schools==buildingLimit);
}

static void prestigeAllocatedContinuation()
{
    Fixture original,restored;
    for(auto* f:{&original,&restored}) {
        f->building(10,10,0,"school",0);f->ai->context.initialize();
        f->ai->strategy.prestige.enabled=true;
        f->ai->environment.accessible_corn=f->ai->environment.accessible_wood=1000;
        f->ai->environment.accessible_stone=f->ai->environment.accessible_algae=1000;
        f->ai->demands.technology=20;f->ai->snapshot.schools=1;f->game.prestigeToReach=1000;
        REQUIRE(f->game.addUnit(4,4,0,WORKER,2,0,0,0));
    }
    auto& a=*original.ai;auto& b=*restored.ai;
    glob2test::withMaximaObservation(a.context,[&] {
        a.allocate_resources(a.context);a.finalize_director_plan(a.context);
    });
    REQUIRE(a.budget.prestige_target_level==3);
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    glob2test::withMaximaObservation(a.context,[&]{a.saveExecutionState(&output);});output.flush();
    const std::string bytes(backend->getBuffer(),backend->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
    glob2test::withMaximaObservation(b.context,[&]{b.loadExecutionState(&input,VERSION_MINOR);});
    const auto compare=[&] {
        CHECK(b.budget.prestige_target_level==a.budget.prestige_target_level);
        CHECK(b.budget.prestige_desired_buildings==a.budget.prestige_desired_buildings);
        CHECK(b.budget.desired_schools==a.budget.desired_schools);
        CHECK(b.budget.prestige_priority==a.budget.prestige_priority);
        CHECK(b.budget.prestige_completion_allowed==a.budget.prestige_completion_allowed);
        const auto executable=[](Maxima& ai) {
            return glob2test::withMaximaObservation(ai.context,[&] {
                std::vector<std::tuple<int,int,int>> intents;
                for(const auto& intent:ai.collect_development_intents(ai.collect_development_world(ai.context)))
                    intents.emplace_back(intent.buildingType,int(intent.purpose),intent.unmetCount);
                const auto limits=ai.collect_development_limits(ai.context);
                return std::tuple{intents,limits.allowUpgrades,limits.upgradePriorities};
            });
        };
        CHECK(executable(b)==executable(a));
    };
    compare(); // Executable plan before another strategic cadence.
    for(auto* ai:{&a,&b})glob2test::withMaximaObservation(ai->context,[&] {
        ai->allocate_resources(ai->context);ai->finalize_director_plan(ai->context);
    });
    compare();
}

static void prestigeRelease191GameContinuation()
{
    auto* backend=globalContainer->fileManager->openInflatingInputStreamBackend(
        "test/maxima/fixtures/save-continuation/maxima-groundlooks-v191.game.gz");
    REQUIRE(backend);
    GAGCore::BinaryInputStream input(backend);
    GameGUI loaded;REQUIRE(loaded.game.load(&input));
    REQUIRE(loaded.game.mapHeader.getVersionMinor()==191);
    const auto verifyTerrain=[](const Map& map) {
        CHECK(map.terrainTypeAt(2,2)==LUSH_GRASS);
        CHECK(map.terrainTypeAt(5,2)==DRY_GRASS);
        CHECK(map.terrainTypeAt(8,2)==DUNE_SAND);
        CHECK(map.terrainTypeAt(11,2)==PALE_SAND);
        const auto bog=map.terrainRegistry().find("fixture:bog");
        const auto mud=map.terrainRegistry().find("fixture:mud");
        REQUIRE(bog);REQUIRE(mud);
        CHECK(*bog==TerrainType(TERRAIN_COUNT));CHECK(*mud==TerrainType(TERRAIN_COUNT+1));
        CHECK(map.terrainTypeAt(4,6)==*bog);CHECK(map.terrainTypeAt(7,6)==*mud);
        CHECK(map.terrainProperties(*bog).swimmable);
        CHECK(map.terrainProperties(*bog).groundSpeedQ8==128);
        CHECK(map.terrainProperties(*mud).groundSpeedQ8==192);
        CHECK_FALSE(map.terrainProperties(*mud).buildable);
    };
    verifyTerrain(loaded.game.map);
    auto* a=dynamic_cast<Maxima*>(loaded.game.players[0]->ai->aiImplementation);REQUIRE(a);
    CHECK(a->timer==1234);CHECK(a->budget.tactical_quarantine_ticks==2468);
    CHECK(a->director.initialized);CHECK_FALSE(a->director.dirty);
    CHECK(a->development_cycle_pending);CHECK(a->maintenance_clearing_pending);
    CHECK(a->prestige_migration_pending);CHECK(a->strategy.prestige.enabled);
    CHECK(a->strategy.prestige.max_pipelines==1);
    CHECK(a->budget.prestige_target_level==0);CHECK(a->prestige_cancellations.empty());
    (void)a->getOrder();
    CHECK_FALSE(a->prestige_migration_pending);
    CHECK(a->strategy.prestige.enabled);
    glob2test::withMaximaObservation(a->context,[&]{CHECK(a->prestige_enabled(a->context));});
    CHECK(a->prestige_race.reason!=Prestige::Disabled);
    CHECK(a->timer>=1234);CHECK(a->timer<=1235);
    auto* saved=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(saved);
    loaded.game.save(&output,false,"migrated-release191");output.flush();
    const std::string bytes(saved->getBuffer(),saved->getPosition());
    GAGCore::BinaryInputStream current(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    current.seekFromStart(0);
    GameGUI resumed;REQUIRE(resumed.game.load(&current));
    CHECK(resumed.game.mapHeader.getVersionMinor()==VERSION_MINOR);
    verifyTerrain(resumed.game.map);
    CHECK(resumed.game.map.checkSum(true)==loaded.game.map.checkSum(true));
    auto* b=dynamic_cast<Maxima*>(resumed.game.players[0]->ai->aiImplementation);REQUIRE(b);
    CHECK_FALSE(b->prestige_migration_pending);CHECK(b->strategy.prestige.enabled);
    CHECK(b->strategy.prestige.max_pipelines==1);
    CHECK(b->timer==a->timer);
    CHECK(b->budget.tactical_quarantine_ticks==a->budget.tactical_quarantine_ticks);
    CHECK(b->development_cycle_pending==a->development_cycle_pending);
    CHECK(b->maintenance_clearing_pending==a->maintenance_clearing_pending);
    CHECK(b->budget.prestige_target_level==a->budget.prestige_target_level);
    CHECK(b->budget.prestige_desired_buildings==a->budget.prestige_desired_buildings);
}

static void prestigeLegacyExecutionBoundary()
{
    // Release format 191 belongs to ground looks and has no prestige tail.
    // Exercise its execution-state prefix directly; this is not a generated
    // historical map/save fixture and does not stand in for terrain remapping.
    CHECK(FILE_FORMAT_VERSION_GROUND_LOOKS==191);
    CHECK(FILE_FORMAT_VERSION_MAXIMA_PRESTIGE>FILE_FORMAT_VERSION_GROUND_LOOKS);
    Fixture source;auto& a=*source.ai;
    a.director.initialized=true;a.director.dirty=false;
    a.budget.tactical_quarantine_ticks=1234;
    a.budget.prestige_target_level=3;
    a.budget.prestige_desired_buildings=2;
    a.prestige_race.state=Prestige::Invest;a.prestige_race.priority=60;
    a.prestige_cancellations[12]={4,7,123,true};
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    glob2test::withMaximaObservation(a.context,[&]{a.saveExecutionState(&output);});output.flush();
    const std::string current(backend->getBuffer(),backend->getPosition());
    for(int legacyVersion:{190,191}) {
        CAPTURE(legacyVersion);
        Fixture restored;auto& b=*restored.ai;
        auto* prefixBackend=new GAGCore::MemoryStreamBackend(current.data(),current.size());
        GAGCore::BinaryInputStream prefixInput(prefixBackend);prefixInput.seekFromStart(0);
        glob2test::withMaximaObservation(b.context,[&]{b.loadExecutionState(&prefixInput,legacyVersion);});
        const size_t boundary=prefixBackend->getPosition();
        REQUIRE(boundary<current.size());
        auto legacy=current.substr(0,boundary);
        legacy.append("\x12\x34\x56\x78",4);
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(legacy.data(),legacy.size()));
        input.seekFromStart(0);
        glob2test::withMaximaObservation(b.context,[&]{b.loadExecutionState(&input,legacyVersion);});
        CHECK(input.readUint32("sentinel")==0x12345678u);
        CHECK(b.director.initialized);CHECK_FALSE(b.director.dirty);
        CHECK(b.budget.tactical_quarantine_ticks==1234);
        CHECK(b.prestige_migration_pending);
        CHECK(b.prestige_race.state==Prestige::Hold);
        CHECK(b.prestige_cancellations.empty());
        CHECK(b.budget.prestige_target_level==0);
        CHECK(b.budget.prestige_desired_buildings==0);
        CHECK_FALSE(b.budget.prestige_completion_allowed);
    }
}

static void prestigeContinuation()
{
    Fixture f;auto& a=*f.ai;
    a.prestige_race.state=Prestige::Invest;a.prestige_race.reason=Prestige::Developing;
    a.prestige_race.pending=50;a.prestige_race.priority=60;
    a.budget.prestige_priority=60;a.budget.prestige_desired_buildings=3;
    a.prestige_cancellations[12]={4,7,123,true};
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    glob2test::withMaximaObservation(a.context,[&]{a.saveExecutionState(&output);});output.flush();
    const std::string bytes(backend->getBuffer(),backend->getPosition());
    Fixture restored;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
    glob2test::withMaximaObservation(restored.ai->context,[&]{restored.ai->loadExecutionState(&input,VERSION_MINOR);});
    CHECK(restored.ai->prestige_race.state==Prestige::Invest);
    CHECK(restored.ai->prestige_race.pending==50);
    CHECK(restored.ai->budget.prestige_desired_buildings==3);
    REQUIRE(restored.ai->prestige_cancellations.count(12));
    CHECK(restored.ai->prestige_cancellations.at(12).generation==123);
    CHECK(restored.ai->prestige_cancellations.at(12).acknowledged);
    CHECK_FALSE(restored.ai->prestige_migration_pending);
    // Old canonical strategies omit the new controls; migration explicitly enables them.
    std::string canonical=a.canonicalStrategy();
    const auto start=canonical.find("prestige.enabled=");
    REQUIRE(start!=std::string::npos);
    const auto end=canonical.find("upgrades.first_prestige",start);
    REQUIRE(end!=std::string::npos);canonical.erase(start,end-start);
    MaximaStrategy values{};std::string error;
    REQUIRE(StrategyResolver::restoreValues(canonical,values,error,FILE_FORMAT_VERSION_MAXIMA_PRESTIGE-1));
    CHECK(values.prestige.enabled);CHECK(values.prestige.finish_priority==100);
    CHECK(values.prestige.max_pipelines==1);
    canonical=a.canonicalStrategy();
    const auto capStart=canonical.find("prestige.max_pipelines=");REQUIRE(capStart!=std::string::npos);
    const auto capEnd=canonical.find(',',capStart);
    canonical.erase(capStart,(capEnd==std::string::npos?canonical.size():capEnd+1)-capStart);
    REQUIRE(StrategyResolver::restoreValues(canonical,values,error,FILE_FORMAT_VERSION_MAXIMA_PRESTIGE_PARALLEL-1));
    CHECK(values.prestige.max_pipelines==1); // Saved format192 keeps its former single pipeline.
    CHECK_FALSE(StrategyResolver::restoreValues(canonical,values,error,VERSION_MINOR));
}

static void prestigeCancellationCommandContinuation()
{
    Fixture original;auto& a=*original.ai;auto& c=a.context;
    a.strategy.prestige.enabled=true;
    auto* school=original.building(10,10,0,"school",1);
    c.initialize();original.game.totalPrestige=450;original.game.prestigeToReach=500;
    original.game.teams[0]->prestige=100;original.game.teams[1]->prestige=200;
    REQUIRE(school->launchConstruction(0,0));
    glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
    REQUIRE(c.orders.size()==1);REQUIRE(a.prestige_cancellations.size()==1);
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);a.save(&output);output.flush();
    const std::string bytes(backend->getBuffer(),backend->getPosition());
    Fixture restored;auto& other=*restored.ai;
    auto* copy=restored.building(10,10,0,"school",1);
    restored.game.totalPrestige=450;restored.game.prestigeToReach=500;
    restored.game.teams[0]->prestige=100;restored.game.teams[1]->prestige=200;
    REQUIRE(copy->launchConstruction(0,0));
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
    REQUIRE(other.load(&input,&restored.player,VERSION_MINOR));
    auto expected=a.getOrder();auto actual=other.getOrder();
    REQUIRE(dynamic_cast<OrderCancelConstruction*>(expected.get()));
    REQUIRE(dynamic_cast<OrderCancelConstruction*>(actual.get()));
    CHECK(expected->getDataLength()==actual->getDataLength());
    CHECK(std::string(reinterpret_cast<char*>(expected->getData()),expected->getDataLength())
        ==std::string(reinterpret_cast<char*>(actual->getData()),actual->getDataLength()));
    CHECK(expected->aiSelectedTarget->generation==actual->aiSelectedTarget->generation);
    school->cancelConstruction(0);copy->cancelConstruction(0);
    a.orderExecutionCompleted(*expected,true);other.orderExecutionCompleted(*actual,true);
    glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
    glob2test::withMaximaObservation(other.context,[&]{other.guard_prestige_completions(other.context);});
    CHECK(a.prestige_cancellations.empty());CHECK(other.prestige_cancellations.empty());
    CHECK(c.orders.empty());CHECK(other.context.orders.empty());
}

static void prestigeSupplyAndAllianceOrder()
{
    Fixture f;auto& a=*f.ai;auto& c=a.context;
    a.strategy.prestige.enabled=true;
    auto* school=f.building(10,10,0,"school",1);
    auto* inn=f.building(35,35,0);c.initialize();
    a.snapshot.population=100;a.demands.technology=40;
    f.player.team->stats.getLatestStat()->workersByConstructionLevel[2]=30;
    f.game.prestigeToReach=500;
    for(int m=0;m<MaterialCount;++m)inn->localMaterials[m]=100;
    glob2test::withMaximaObservation(c,[&]{a.assess_prestige(c);CHECK(a.prestige_race.reason==Prestige::Unavailable);});
    for(int m=0;m<MaterialCount;++m)school->localMaterials[m]=100;
    glob2test::withMaximaObservation(c,[&]{a.assess_prestige(c);CHECK(a.prestige_race.state==Prestige::Invest);});
    a.posture=Maxima::PostureRecover;
    glob2test::withMaximaObservation(c,[&]{a.assess_prestige(c);CHECK(a.prestige_race.reason==Prestige::Emergency);});
    a.posture=Maxima::PostureCampaign;
    f.game.totalPrestige=450;f.game.teams[0]->prestige=0;f.game.teams[1]->prestige=200;f.game.teams[2]->prestige=250;
    f.game.teams[0]->allies|=f.game.teams[2]->me;f.game.teams[2]->allies|=f.game.teams[0]->me;
    glob2test::withMaximaObservation(c,[&]{CHECK(Prestige::wins(a.prestige_standings(c),0,50));});
    auto& conditions=f.game.gameHeader.getWinningConditions();
    conditions.remove_if([](const auto& rule){return rule->getType()==WCAllies;});
    conditions.push_back(std::make_shared<WinningConditionAllies>());
    glob2test::withMaximaObservation(c,[&]{CHECK_FALSE(Prestige::wins(a.prestige_standings(c),0,50));});
    // Fresh own buildings are visible before the batched register discovers them.
    auto* unregistered=f.building(40,10,0,"school",1);
    REQUIRE(unregistered->launchConstruction(0,0));
    glob2test::withMaximaObservation(c,[&]{
        a.guard_prestige_completions(c);
        CHECK_FALSE(a.prestige_action_allowed(c,a.prestige_race.buildingType,2,3));
    });
    REQUIRE(a.prestige_cancellations.count(unregistered->gid));
    CHECK(a.prestige_cancellations.at(unregistered->gid).buildingId==-1);
    REQUIRE(c.orders.size()==1);CHECK(dynamic_cast<OrderCancelConstruction*>(c.orders.front().get())!=nullptr);
}

static void prestigeSerializesLegacyCommitments()
{
    Fixture f;auto& a=*f.ai;auto& c=a.context;
    a.strategy.prestige.enabled=true;
    auto* first=f.building(10,10,0,"school",1);auto* second=f.building(30,30,0,"school",1);
    c.initialize();f.game.prestigeToReach=500;f.game.totalPrestige=0;
    glob2test::withMaximaObservation(c,[&] {
        c.push_order(std::make_shared<OrderConstruction>(first->gid,0,0));
        c.push_order(std::make_shared<OrderConstruction>(second->gid,0,0));
        a.guard_prestige_completions(c);
    });
    REQUIRE(c.orders.size()==1);
    CHECK(static_cast<OrderConstruction*>(c.orders.front().get())->gid==first->gid);
    c.orders.clear();
    c.get_building_register().set_upgrading(f.id(first),true);
    glob2test::withMaximaObservation(c,[&] {
        c.push_order(std::make_shared<OrderConstruction>(second->gid,0,0));
        a.guard_prestige_completions(c);
    });
    CHECK(c.orders.empty());
    REQUIRE(first->launchConstruction(0,0));REQUIRE(second->launchConstruction(0,0));
    glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
    REQUIRE(c.orders.size()==1);
    REQUIRE(dynamic_cast<OrderCancelConstruction*>(c.orders.front().get()));
    CHECK(static_cast<OrderCancelConstruction*>(c.orders.front().get())->gid==second->gid);
    CHECK(a.prestige_cancellations.count(first->gid)==0);
}

// Legacy construction may be absent from the planner and building register.
// Pending points describe the actual completion, including restoration by repair.
static void prestigePendingNewAndRepair()
{
    using namespace AIMaximaPlacement;
    for(bool repairing:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;
        a.strategy.prestige.enabled=true;
        const int root=f.game.buildingsTypes.getTypeNum("school",0,true);
        if(!repairing) {
            auto catalog=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
            const int first=f.game.buildingsTypes.getTypeNum("school",0,false);
            catalog["variants"][first]["properties"]["prestige"]=25;
            f.game.buildingsTypes.loadSnapshotJson(catalog.dump());f.game.configureBuildingCatalog();
        }
        auto* ordinary=f.building(10,10,0,"school",1);
        c.initialize();f.game.prestigeToReach=500;
        a.environment.accessible_corn=a.environment.accessible_wood=1000;
        a.environment.accessible_stone=a.environment.accessible_algae=1000;
        a.demands.technology=40;
        f.player.team->stats.getLatestStat()->workersByConstructionLevel[2]=30;
        // Ordinary first-level starts retain their generic unsafe-start check.
        if(!repairing) {
            f.game.teams[1]->prestige=245;f.game.teams[2]->prestige=245;f.game.totalPrestige=490;
            glob2test::withMaximaObservation(c,[&]{CHECK_FALSE(a.prestige_action_allowed(c,root,0,1));});
            f.game.teams[1]->prestige=f.game.teams[2]->prestige=f.game.totalPrestige=0;
            glob2test::withMaximaObservation(c,[&]{CHECK(a.prestige_action_allowed(c,root,0,1));});
        }
        ::Building* legacy=nullptr;
        if(repairing) {
            legacy=f.building(30,10,0,"school",2);
            --legacy->hp;REQUIRE(legacy->launchConstruction(0,0));
            REQUIRE(legacy->tryToBuildingSiteRoom());
            REQUIRE(legacy->constructionResultState==Building::REPAIR);
        } else {
            legacy=f.game.addBuilding(30,10,root,0);REQUIRE(legacy);
            REQUIRE(legacy->constructionResultState==Building::NEW_BUILDING);
        }
        REQUIRE(legacy->type->prestige==0);
        f.game.snapshotStore().invalidateBoundary();
        glob2test::withMaximaObservation(c,[&] {
            a.assess_prestige(c);
            CHECK(a.prestige_race.pending==(repairing?50:25));
            CHECK_FALSE(a.prestige_action_allowed(c,root,2,3));
            c.push_order(std::make_shared<OrderConstruction>(ordinary->gid,0,0));
            a.guard_prestige_completions(c);
        });
        CHECK(c.orders.empty());
        CHECK(a.prestige_cancellations.empty()); // Neither NEW nor REPAIR is cancelled.
        CHECK(legacy->constructionResultState==(repairing?Building::REPAIR:Building::NEW_BUILDING));
        if(!repairing) {
            DevelopmentAction action;action.id=1;action.state=CreateIssued;
            action.type=BuildStandalone;action.buildingType=root;
            action.buildingId=-1;action.fromLevel=0;action.targetLevel=1;
            action.centerX=legacy->posX-legacy->type->decLeft;
            action.centerY=legacy->posY-legacy->type->decTop;
            a.development_planner.actionMap[1]=action;
            glob2test::withMaximaObservation(c,[&] {
                a.assess_prestige(c);
                CHECK(a.prestige_race.pending==25); // Parcel-observed NEW work is counted once.
            });
        }
        // If an ordinary upgrade has already started, retain the uninterruptible
        // NEW/REPAIR completion and cancel only the reversible upgrade.
        REQUIRE(ordinary->launchConstruction(0,0));
        REQUIRE(ordinary->tryToBuildingSiteRoom());
        f.game.snapshotStore().invalidateBoundary();
        glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
        REQUIRE(c.orders.size()==1);
        const auto* cancel=dynamic_cast<OrderCancelConstruction*>(c.orders.front().get());REQUIRE(cancel);
        CHECK(cancel->gid==ordinary->gid);
        CHECK(a.prestige_cancellations.count(ordinary->gid)==1);
        CHECK(a.prestige_cancellations.count(legacy->gid)==0);
        CHECK(legacy->constructionResultState==(repairing?Building::REPAIR:Building::NEW_BUILDING));
    }
}

static void prestigeUnsupportedRoutesHold()
{
    for(int layout=0;layout<3;++layout) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;
        auto catalog=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
        const auto prestige=[&](int level,bool site,int points) {
            catalog["variants"][f.game.buildingsTypes.getTypeNum("school",level,site)]["properties"]["prestige"]=points;
        };
        if(layout==0)prestige(0,false,25); // Uncancellable first completion in the chain.
        if(layout==1) {prestige(0,false,50);prestige(1,false,50);prestige(2,false,100);}
        if(layout==2)prestige(2,true,1); // Cancelling this site changes public points.
        f.game.buildingsTypes.loadSnapshotJson(catalog.dump());f.game.configureBuildingCatalog();
        f.building(10,10,0,"school",1);c.initialize();
        a.strategy.prestige.enabled=true;
        a.environment.accessible_corn=a.environment.accessible_wood=1000;
        a.environment.accessible_stone=a.environment.accessible_algae=1000;
        a.demands.technology=40;f.game.prestigeToReach=1000;
        f.player.team->stats.getLatestStat()->workersByConstructionLevel[2]=30;
        glob2test::withMaximaObservation(c,[&] {
            a.assess_prestige(c);
            CHECK(a.prestige_race.state==Prestige::Hold);
            CHECK(a.prestige_race.reason==Prestige::Unavailable);
            CHECK(a.prestige_race.buildingType==-1);
            CHECK(a.budget.prestige_target_level==0);
        });
    }
}

static void prestigeUnsafeRevert()
{
    // An upgrade can be unsafe to complete AND unsafe to cancel: restoration
    // immediately adds the origin's points to the public total.
    for(bool positiveOrigin:{false,true}) {
        Fixture f;auto& a=*f.ai;auto& c=a.context;
        if(positiveOrigin) {
            auto catalog=nlohmann::json::parse(f.game.buildingsTypes.snapshotJson());
            catalog["variants"][f.game.buildingsTypes.getTypeNum("school",1,false)]["properties"]["prestige"]=50;
            catalog["variants"][f.game.buildingsTypes.getTypeNum("school",2,false)]["properties"]["prestige"]=100;
            f.game.buildingsTypes.loadSnapshotJson(catalog.dump());f.game.configureBuildingCatalog();
        }
        auto* school=f.building(10,10,0,"school",1);c.initialize();
        REQUIRE(school->launchConstruction(0,0));REQUIRE(school->tryToBuildingSiteRoom());
        REQUIRE(school->constructionResultState==Building::UPGRADE);
        REQUIRE(school->getConstructionOriginTypeNum()>=0);
        REQUIRE(school->type->prestige==0);
        a.strategy.prestige.enabled=true;f.game.prestigeToReach=500;
        f.game.teams[0]->prestige=0;f.game.teams[1]->prestige=positiveOrigin?200:250;
        f.game.teams[2]->prestige=250;f.game.totalPrestige=positiveOrigin?450:500;
        f.game.snapshotStore().invalidateBoundary();
        glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
        if(positiveOrigin) {
            CHECK(c.orders.empty());CHECK(a.prestige_cancellations.empty());
            // A queued cancellation must be revalidated when standings change.
            f.game.teams[1]->prestige=150;f.game.totalPrestige=400;
            f.game.snapshotStore().invalidateBoundary();
            glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
            REQUIRE(c.orders.size()==1);
            CHECK(dynamic_cast<OrderCancelConstruction*>(c.orders.front().get())!=nullptr);
            CHECK(a.prestige_cancellations.count(school->gid)==1);
            f.game.teams[1]->prestige=200;f.game.totalPrestige=450;
            f.game.snapshotStore().invalidateBoundary();
            glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
            CHECK(c.orders.empty());CHECK(a.prestige_cancellations.empty());
            // Withholding the order does not abandon the active commitment.
            f.game.teams[1]->prestige=150;f.game.totalPrestige=400;
            f.game.snapshotStore().invalidateBoundary();
            glob2test::withMaximaObservation(c,[&]{a.guard_prestige_completions(c);});
            REQUIRE(c.orders.size()==1);
            CHECK(a.prestige_cancellations.count(school->gid)==1);
        } else {
            // safe(0) is false after this threshold, but zero-gain stock
            // restoration must still be cancelled under the established policy.
            REQUIRE(c.orders.size()==1);
            CHECK(dynamic_cast<OrderCancelConstruction*>(c.orders.front().get())!=nullptr);
            CHECK(a.prestige_cancellations.count(school->gid)==1);
        }
    }
}

static void reconnaissanceWaitsForEightyPercent()
{
    Fixture f;auto& a=*f.ai;auto& c=a.context;c.initialize();
    a.strategy.reconnaissance.enabled=true;
    a.strategy.reconnaissance.scouting_missions_enabled=true;
    f.game.map.unsetMapDiscovered();
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        if(y*64+x<3276)f.game.map.setMapDiscovered(x,y,f.player.team->me);
    a.update_reconnaissance(c);
    REQUIRE(a.reconnaissance.report().exploredPercent==79);
    a.plan_reconnaissance_objectives(c);
    REQUIRE(a.budget.reconnaissance_suspended);
    REQUIRE(a.budget.reconnaissance_objectives.empty());
    REQUIRE(a.reconnaissance.report().desiredMissions==0);
    // A stale loaded budget cannot start missions before the next director pass.
    a.budget.reconnaissance_suspended=false;
    a.update_reconnaissance_missions(c);
    REQUIRE(a.reconnaissance_suspended);
    // 3277 / 4096 tiles is the first count that reaches 80 percent.
    f.game.map.setMapDiscovered(12,51,f.player.team->me);
    a.update_reconnaissance(c);
    REQUIRE(a.reconnaissance.report().exploredPercent==80);
    a.plan_reconnaissance_objectives(c);
    REQUIRE(!a.budget.reconnaissance_suspended);
    a.update_reconnaissance_missions(c);
    REQUIRE(!a.reconnaissance_suspended);
    a.budget.food_emergency=true;
    a.plan_reconnaissance_objectives(c);
    REQUIRE(!a.budget.reconnaissance_suspended);
    a.budget.colony_emergency=true;
    a.plan_reconnaissance_objectives(c);
    REQUIRE(a.budget.reconnaissance_suspended);
}

void cadence() {
    Fixture f; auto& a=*f.ai;
    a.strategy.scheduling.strategy_interval_ticks=10;
    a.snapshot.tick=100; a.timer=110; a.director.committed();
    a.director.evaluate(a,a.context);
    REQUIRE(a.snapshot.tick==110);
    // Duplicate calls within one tick remain suppressed; invalidation overrides it.
    a.snapshot.population=123;
    a.director.evaluate(a,a.context);
    REQUIRE(a.snapshot.population==123);
    a.director.invalidate(); a.director.evaluate(a,a.context);
    REQUIRE(a.snapshot.population!=123);
}

static void allyPrestige() {
    Fixture f; auto& a=*f.ai;
    f.player.team->enemies &= ~f.game.teams[2]->me;
    f.player.team->allies |= f.game.teams[2]->me;
    f.game.teams[2]->prestige=100; f.game.totalPrestige=100;
    a.snapshot=a.collect_snapshot(a.context);
    a.large_economy_committed=true; glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.build_policy_bids();});
    REQUIRE(a.snapshot.enemy_prestige==0);
    REQUIRE(a.policy_bids[Maxima::PolicyDefense].desired_towers==0);
    f.game.teams[1]->prestige=50; f.game.totalPrestige=150;
    a.snapshot=a.collect_snapshot(a.context); glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.build_policy_bids();});
    REQUIRE(a.snapshot.enemy_prestige==50);
    REQUIRE(a.policy_bids[Maxima::PolicyDefense].desired_towers>0);
}

static void thirdPartyTower() {
    Fixture f; f.building(10,10,0);
    auto target=f.building(40,40,1);
    auto tower=f.building(43,40,2,"defencetower",2);
    for(int i=0;i<12;++i) f.warrior(8+i,8);
    auto& a=*f.ai; auto& c=a.context; c.initialize();
    a.reconnaissance.beginObservation(a.timer,{1,2});
    for(auto b:{target,tower}) a.reconnaissance.observeBuilding(Recon::BuildingSighting(
        b->gid,b->owner->teamNumber,b->type->shortTypeNum,b->posX,b->posY,
        b->type->width,b->type->height,false,a.timer));
    a.reconnaissance.finishObservation();
    a.opponents[1].score=10000; a.opponents[2].score=0;
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_target_gid==target->gid);
    // Remembered towers belonging to a current ally must not be counted.
    f.player.team->enemies &= ~f.game.teams[2]->me;
    f.player.team->allies |= f.game.teams[2]->me;
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_target_gid==target->gid);
}

static void disconnectedArmy() {
    Fixture f; f.building(10,10,0); f.building(27,30,0);
    auto target=f.building(38,30,1); f.islands();
    std::vector<Unit*> warriors;
    for(int i=0;i<12;++i) {
        auto u=f.warrior(6+i,6);u->setPerformance(SWIM,0);warriors.push_back(u);
    }
    auto& a=*f.ai; auto& c=a.context; c.initialize(); f.remember(target);
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionNone);
    for(int i=0;i<4;++i) warriors[i]->setPerformance(SWIM,1);
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionSiege);
    REQUIRE(a.budget.tactical_requested_force==a.offense_diagnostics.eligibleWarriors);
    // Warriors already in the destination component need no swimming ability.
    for(int i=0;i<4;++i) {
        auto u=warriors[i];u->setPerformance(SWIM,0);
        f.game.map.setGroundUnit(u->posX,u->posY,NOGUID);
        u->posX=30+i;u->posY=20;f.game.map.setGroundUnit(u->posX,u->posY,u->gid);
    }
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionSiege);
    REQUIRE(a.budget.tactical_requested_force==a.offense_diagnostics.eligibleWarriors);
}

static void flyingArmyAndNonexploringMagicThreat() {
    Fixture f;
    auto definitions=nlohmann::json::parse(UnitCatalog::builtins()->serialize());
    auto fighter=definitions["units"][WARRIOR];fighter["key"]="fixture:fighter";
    fighter["behaviors"]["fly"]=true;fighter["behaviors"]["swim"]=false;
    for(auto& level:fighter["levels"]){level["performance"][FLY]=16;level["performance"][STOP_FLY]=8;}
    auto caster=definitions["units"][WORKER];caster["key"]="fixture:caster";
    caster["behaviors"]["magicGround"]=true;caster["behaviors"]["explore"]=false;
    for(auto& level:caster["levels"])level["performance"][MAGIC_ATTACK_GROUND]=10;
    definitions["units"].push_back(fighter);definitions["units"].push_back(caster);
    f.game.gameHeader.setUnitCatalog(UnitCatalog::fromJson(definitions.dump()));f.game.configureBuildingCatalog();
    f.building(10,10,0);f.building(27,30,0);
    auto target=f.building(38,30,1);f.islands();
    for(int i=0;i<12;++i){auto* unit=f.game.addUnit(6+i,6,0,3,1,0,0,0);REQUIRE(unit);unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;}
    auto& a=*f.ai;auto& c=a.context;c.initialize();f.remember(target);
    a.plan_offense(c);
    REQUIRE(a.budget.tactical_kind==Tactics::MissionSiege);
    CHECK(a.offense_diagnostics.eligibleWarriors>=4);
    auto* threat=f.game.addUnit(14,12,1,4,0,0,0,0);REQUIRE(threat);
    f.game.map.fogOfWar[f.game.map.coordToIndex(14,12)]|=f.player.team->me;
    f.game.map.markVisibility(f.game.map.coordToIndex(14,12));
    a.sample_reconnaissance_forces(c);
    CHECK(a.reconnaissance.report().visibleAttackExplorers==1);
}

static void reusedOwnId() {
    for(bool sameLocation:{false,true}) {
        Fixture f; auto old=f.building(10,10,0); auto& c=f.ai->context;c.initialize();
        int oldId=f.id(old),oldGid=old->gid;
        old->kill();f.player.team->removeFromAbilitiesLists(old);
        f.player.team->detachBuilding(::Building::GIDtoID(oldGid));delete old;
        // Identity remains safe even when a worker never observes the empty
        // slot between removal and replacement.
        auto replacement=f.building(sameLocation?10:40,sameLocation?10:40,0);
        REQUIRE(replacement->gid==oldGid);
        // Check before housekeeping, when deferred management work can execute.
        auto observation=c.scopeOwnerObservation();
        REQUIRE(!c.buildings.get_building(oldId));
        REQUIRE(Conditions::BuildingDestroyed(oldId).passes(c)==Conditions::Ready);
        Management::AssignWorkers(7,oldId).modify(c);
        REQUIRE(c.orders.empty());
        GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);([&]{auto observation=c.scopeOwnerObservation();return c.buildings.save(&output);}());
        std::string bytes(backend->getBuffer(),backend->getPosition());
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        Construction::BuildingRegister loaded(&f.player);loaded.bind(c.observation(),c.playerNumber(),c.teamNumber());loaded.load(&input,VERSION_MINOR);
        REQUIRE(!loaded.is_building_found(oldId));
        {auto observation=c.scopeOwnerObservation();c.buildings.tick();} REQUIRE(c.buildings.found().empty());
    }
    // Moving flags and loading live registrations preserve valid identities.
    Fixture f; auto flag=f.building(10,10,0,"warflag");auto& c=f.ai->context;c.initialize();
    int id=f.id(flag);flag->posX=20;flag->posY=20;{auto observation=c.scopeOwnerObservation();c.buildings.tick();}
    auto observation=c.scopeOwnerObservation();
    REQUIRE((c.buildings.get_building(id) && c.buildings.get_building(id)->gid==flag->gid));
    GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);([&]{auto observation=c.scopeOwnerObservation();return c.buildings.save(&output);}());
    std::string bytes(backend->getBuffer(),backend->getPosition());
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    Construction::BuildingRegister loaded(&f.player);loaded.bind(c.observation(),c.playerNumber(),c.teamNumber());loaded.load(&input,VERSION_MINOR);
    REQUIRE((loaded.get_building(id) && loaded.get_building(id)->gid==flag->gid));
}

static void fortificationUsesOnlySpareLabour()
{
    Fixture f;auto& a=*f.ai;
    AIMaximaPlacement::WorldState world;world.reset(16,16);world.profiles=glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_building_profiles();});
    const int towerType=f.game.buildingsTypes.getPlaceableTypeNum("defencetower");
    a.strategy.staffing.construction_large_workers=6;
    a.labour_plan.trainingReserve=2;
    auto count=[&](AIMaximaPlacement::DevelopmentPurpose purpose) {
        int result=0;
        for(const auto& intent:glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_development_intents(world);}))
            if(intent.buildingType==towerType && intent.purpose==purpose)
                ++result;
        return result;
    };
    a.labour_observation.idle=7;
    REQUIRE(count(AIMaximaPlacement::Fortification)==0);
    a.labour_observation.idle=8;
    REQUIRE(count(AIMaximaPlacement::Fortification)==1);
    a.budget.desired_towers=1;
    REQUIRE(count(AIMaximaPlacement::Fortification)==0);
    REQUIRE(count(AIMaximaPlacement::CoreCapacity)==1);
    AIMaximaPlacement::WorldBuilding tower;tower.buildingType=towerType;tower.level=1;
    world.buildings.push_back(tower);
    REQUIRE(count(AIMaximaPlacement::Fortification)==1);
}

static void unifiedHospitalCapacity() {
    using namespace AIMaximaPlacement;
    Fixture f; auto& a=*f.ai;
    a.snapshot.warriors=20;
    a.strategy.military.hospital_beds_per_warrior_percent=50;
    WorldState world;world.profiles=glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_building_profiles();});
    const int hospitalType=f.game.buildingsTypes.getPlaceableTypeNum("hospital");
    WorldBuilding hospital; hospital.id=10;
    hospital.buildingType=hospitalType; hospital.level=1;
    world.buildings.push_back(hospital);
    const auto unmet=[&]() {
        for(const auto& intent:glob2test::withMaximaObservation(a.context,[&]() -> decltype(auto) {return a.collect_development_intents(world);}))
            if(intent.buildingType==hospitalType) return intent.unmetCount;
        return 0;
    };
    REQUIRE(a.committed_hospital_beds(world.buildings)==2);
    REQUIRE(unmet()==4);
    DevelopmentAction upgrade; upgrade.id=1; upgrade.type=UpgradeBuilding;
    upgrade.buildingType=hospitalType;
    upgrade.buildingId=10; upgrade.targetLevel=2; upgrade.state=ParcelReserved;
    a.development_planner.actionMap[1]=upgrade;
    REQUIRE(a.committed_hospital_beds(world.buildings)==5);
    REQUIRE(a.committed_hospital_beds(world.buildings,1)==2);
    REQUIRE(unmet()==3);
    world.buildings[0].level=2; world.buildings[0].site=true;
    REQUIRE(a.committed_hospital_beds(world.buildings)==5); // No double credit.
    DevelopmentAction build; build.id=2; build.type=BuildStandalone;
    build.buildingType=hospitalType;
    build.buildingId=11; build.state=CreateIssued;
    a.development_planner.actionMap[2]=build;
    REQUIRE(a.committed_hospital_beds(world.buildings)==7);
    hospital.id=11; hospital.site=true; world.buildings.push_back(hospital);
    REQUIRE(a.committed_hospital_beds(world.buildings)==7);
    a.development_planner.actionMap[2].state=Completed;
    REQUIRE(a.committed_hospital_beds(world.buildings)==7);
    a.snapshot.warriors=0;
    REQUIRE(unmet()==0); // No demolition when the army shrinks.
    // Existing hospitals upgrade only while the same capacity target is unmet.
    a.development_planner.actionMap.clear();
    f.building(20,20,0,"hospital"); a.context.initialize();
    REQUIRE(f.game.addUnit(4,4,0,WORKER,1,0,0,0));
    a.budget.upgrade_level1_hospital_weight=20;
    a.snapshot.warriors=4;
    REQUIRE(a.collect_development_limits(a.context).upgradePriority(hospitalType,1)==0);
    a.snapshot.warriors=10;
    REQUIRE(a.collect_development_limits(a.context).upgradePriority(hospitalType,1)==20);
    int id=-1;
    for(const auto& entry:a.context.get_building_register().found())
        if(entry.second.type==f.game.buildingsTypes.getTypeNum("hospital",0,false)) id=entry.first;
    REQUIRE(id>=0); upgrade.buildingId=id;
    a.development_planner.actionMap[1]=upgrade;
    REQUIRE(a.collect_development_limits(a.context).upgradePriority(hospitalType,1)==0);
    REQUIRE(a.collect_development_limits(a.context,1).upgradePriority(hospitalType,1)==20);
}

static void proactiveProtection() {
    Fixture f; f.building(10,10,0);auto& a=*f.ai;auto& c=a.context;c.initialize();
    for(int y=0;y<64;++y)f.game.map.paintCell(0, y, WATER);
    f.game.map.setResourceByIndex(3,11,WOOD,5);f.game.map.setResourceByIndex(4,11,WOOD,5);f.game.map.setResourceByIndex(5,11,WOOD,5);
    a.timer=5000;a.budget.farming_enabled=true;a.budget.farming_protection_enabled=true;
    a.budget.farming_minimum_wood_fertility=0;
    a.budget.farming_wood_firebreak_enabled=false;
    a.budget.farming_proactive_clearing_enabled=true;a.budget.farming_allow_proactive_clearing=true;
    a.budget.farming_min_workers_for_clearing=0;a.budget.farming_clearing_cooldown=0;
    a.budget.farming_clearing_duration=1500;a.budget.farming_clearing_quota=8;
    a.update_farming(c);
    for(auto o:c.managementOrders) if(auto p=dynamic_cast<Management::AddArea*>(o.get()))
        if(p->areaType==ForbiddenArea) for(auto xy:p->locations) f.game.map.addForbidden(xy.x,xy.y,f.player.team->teamNumber);
    c.managementOrders.clear();a.manage_land_clearing(c);
    int x=0,y=0;REQUIRE(([&]{auto observation=c.scopeOwnerObservation();return c.get_building_position(a.proactive_clearing_flag,x,y);}()));
    int released=0;
    for(auto o:c.managementOrders) if(auto p=dynamic_cast<Management::RemoveArea*>(o.get()))
        if(p->areaType==ForbiddenArea) for(auto xy:p->locations) {
            if(f.game.map.isForbidden(xy.x,xy.y,f.player.team->me))++released;
            f.game.map.removeForbidden(xy.x,xy.y,f.player.team->teamNumber);
        }
    REQUIRE(released>0);c.managementOrders.clear();a.timer+=64;a.update_farming(c);
    int openWood=0;
    for(int i=0;i<4096;++i) if(f.game.map.getTile(i%64,i/64).resource.type==WOOD
        &&f.game.map.warpDistSquare(x,y,i%64,i/64)<=16) {
        REQUIRE(!a.farm_protection_mask[i]);++openWood;
    }
    REQUIRE(openWood>0);
    // Releasing the flag must allow the ordinary farming policy to resume.
    ([&]{auto observation=c.scopeOwnerObservation();return c.cancel_or_destroy_building(a.proactive_clearing_flag,1u<<WORKER);}());
    c.managementOrders.clear();a.timer+=64;a.update_farming(c);
    int protectedAgain=0;
    for(int i=0;i<4096;++i) if(f.game.map.getTile(i%64,i/64).resource.type==WOOD
        &&f.game.map.warpDistSquare(x,y,i%64,i/64)<=16 && a.farm_protection_mask[i])++protectedAgain;
    REQUIRE(protectedAgain>0);
}
}

TEST_SUITE("Maxima.Director")
{
    TEST_CASE("prestige adaptive capacity uses qualified spare crews") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAdaptiveCapacity(); }
    TEST_CASE("prestige adaptive capacity credits builders and ordinary commitments once") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAdaptiveBacklogAndBuilders(); }
    TEST_CASE("prestige adaptive capacity respects materials and final steps") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAdaptiveMaterialsAndFinish(); }
    TEST_CASE("prestige adaptive capacity releases proven completed ordinary work") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAdaptiveCompletedCommitments(); }
    TEST_CASE("prestige adaptive cap and allocated demand continue after save [save-format]") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAdaptiveContinuation(); }
    TEST_CASE("prestige allocated pipeline is bounded") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAllocatedPipeline(); }
    TEST_CASE("prestige allocation at the building limit saves and reloads [save-format]") { glob2test::HeadlessGlobals globals; director_regressions::prestigeBuildingLimitContinuation(); }
    TEST_CASE("prestige allocated plan continuation") { glob2test::HeadlessGlobals globals; director_regressions::prestigeAllocatedContinuation(); }
    TEST_CASE("release 191 Maxima and ground looks migrate through normal game loading [save-format]") { glob2test::HeadlessGlobals globals; director_regressions::prestigeRelease191GameContinuation(); }
    TEST_CASE("release 190 and ground-looks 191 execution state migrate without reading prestige fields") { glob2test::HeadlessGlobals globals; director_regressions::prestigeLegacyExecutionBoundary(); }
    TEST_CASE("prestige economic decisions and cancellation") { glob2test::HeadlessGlobals globals; director_regressions::prestigeEconomyAndCancellation(); }
    TEST_CASE("prestige counts legacy new construction and repair completions") { glob2test::HeadlessGlobals globals; director_regressions::prestigePendingNewAndRepair(); }
    TEST_CASE("prestige dedicated route requires reversible zero-point origins") { glob2test::HeadlessGlobals globals; director_regressions::prestigeUnsupportedRoutesHold(); }
    TEST_CASE("prestige cancellation cannot trigger a losing positive restoration") { glob2test::HeadlessGlobals globals; director_regressions::prestigeUnsafeRevert(); }
    TEST_CASE("prestige serializes legacy queued and active commitments") { glob2test::HeadlessGlobals globals; director_regressions::prestigeSerializesLegacyCommitments(); }
    TEST_CASE("prestige supply and victory condition order") { glob2test::HeadlessGlobals globals; director_regressions::prestigeSupplyAndAllianceOrder(); }
    TEST_CASE("prestige cancellation resumes its queued command") { glob2test::HeadlessGlobals globals; director_regressions::prestigeCancellationCommandContinuation(); }
    TEST_CASE("prestige continuation and legacy defaults") { glob2test::HeadlessGlobals globals; director_regressions::prestigeContinuation(); }
    TEST_CASE("tactical quarantine budget survives current and legacy execution loads") { glob2test::HeadlessGlobals globals; director_regressions::quarantineBudgetContinuation(); }
	TEST_CASE("reconnaissance waits for eighty percent") { glob2test::HeadlessGlobals globals; director_regressions::reconnaissanceWaitsForEightyPercent(); }
	TEST_CASE("fortification uses only spare labour") { glob2test::HeadlessGlobals globals; director_regressions::fortificationUsesOnlySpareLabour(); }
	TEST_CASE("unified hospital capacity") { glob2test::HeadlessGlobals globals; director_regressions::unifiedHospitalCapacity(); }
	TEST_CASE("cadence") { glob2test::HeadlessGlobals globals; director_regressions::cadence(); }
	TEST_CASE("ally prestige") { glob2test::HeadlessGlobals globals; director_regressions::allyPrestige(); }
	TEST_CASE("third party tower") { glob2test::HeadlessGlobals globals; director_regressions::thirdPartyTower(); }
	TEST_CASE("flying defenders and nonexploring magic threats") { glob2test::HeadlessGlobals globals; director_regressions::flyingArmyAndNonexploringMagicThreat(); }
	TEST_CASE("disconnected army") { glob2test::HeadlessGlobals globals; director_regressions::disconnectedArmy(); }
	TEST_CASE("proactive protection") { glob2test::HeadlessGlobals globals; director_regressions::proactiveProtection(); }
	TEST_CASE("reused own id") { glob2test::HeadlessGlobals globals; director_regressions::reusedOwnId(); }
}
