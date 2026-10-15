// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TurretTargeting.h"
#include "AI.h"
#include "Player.h"
#include "ComputeExecutor.h"
#include "BuildingUtils.h"
#include <random>
#include <set>
#include <map>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <fstream>
#include <BinaryStream.h>
#include "Version.h"
#include <StreamBackend.h>

namespace
{
using namespace TurretTargeting;
bool sameSource(const Source& a,const Source& b)
{
    return a.identity==b.identity && a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height
        && a.range==b.range && a.speed==b.speed && a.damage==b.damage && a.buildingDamage==b.buildingDamage
        && a.unitTeams==b.unitTeams && a.buildingTeams==b.buildingTeams;
}
void compare(const Building& turret,const Observation& observation)
{
    const auto reference=plan(turret,0,0).preferred;
    const auto live=plan(turret,8,2);
    const auto frozen=plan(source(turret),observation,8,2);
    const auto reused=observe(*turret.owner->game,observation.world);
    const auto row=std::make_shared<const std::vector<int>>(source(turret).damage);
    const auto compact=plan(source(turret,row),reused,8,2);
    CHECK(compact.entries==frozen.entries);
    CHECK(compact.preferred.x==frozen.preferred.x);
    CHECK(compact.preferred.y==frozen.preferred.y);
    CHECK(live.preferred.found()==reference.found());
    if (reference.found()) {
        CHECK(live.preferred.x==reference.x);
        CHECK(live.preferred.y==reference.y);
        CHECK(live.preferred.score==reference.score);
        CHECK(live.preferred.ticks==reference.ticks);
        CHECK(live.preferred.priority==reference.priority);
        REQUIRE(!live.entries.empty());
        CHECK(live.entries.front()==live.preferred.entry);
    }
    CHECK(frozen.preferred.entry==live.preferred.entry);
    CHECK(frozen.preferred.x==live.preferred.x);
    CHECK(frozen.preferred.y==live.preferred.y);
    CHECK(frozen.preferred.score==live.preferred.score);
    CHECK(frozen.preferred.ticks==live.preferred.ticks);
    CHECK(frozen.entries==live.entries);
    auto temporaryObservation=observation; temporaryObservation.index.reset();
    const auto temporary=plan(source(turret),temporaryObservation,8,2);
    CHECK(temporary.entries==frozen.entries);
    CHECK(temporary.preferred.entry==frozen.preferred.entry);
    CHECK(temporary.preferred.x==frozen.preferred.x);
    CHECK(temporary.preferred.y==frozen.preferred.y);
    std::set<std::pair<bool,Uint16>> seen;
    for (const auto& e:live.entries) CHECK(seen.emplace(e.kind==Kind::Building,e.gid).second);
    CHECK(live.entries.size()<=8);
}
}
TEST_SUITE("TurretTargeting")
{
    TEST_CASE("reference and frozen head match randomized ground air wrap and alliances")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=3,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",0,0);
        REQUIRE(turret);
        std::vector<Unit*> units;
        for (int n=0;n<50;++n) {
            auto* u=world.addUnit(n%3,4+n%20,4+n/20,1+n%2);
            REQUIRE(u);
            units.push_back(u);
        }
        world.addBuilding("swarm",28,28,0,1);
        std::mt19937 rng(7218);
        for (int trial=0;trial<300;++trial) {
            CAPTURE(trial);
            for (auto* u:units) {
                world.game.map.setGroundUnit(u->posX,u->posY,NOGUID);
                world.game.map.setAirUnit(u->posX,u->posY,NOGUID);
            }
            // At most one occupant per plane; air and ground can coincide.
            std::set<std::tuple<bool,int,int>> occupied;
            for (auto* u:units) {
                const bool air=u->typeNum==EXPLORER;
                int x,y;
                do { x=rng()%32; y=rng()%32; } while (!occupied.emplace(air,x,y).second);
                u->posX=x; u->posY=y; u->performance[FLY]=air ? 8 : 0;
                u->hp=1+int(rng()%250); u->speed=rng()%32; u->delta=rng()%256;
                u->performance[ATTACK_STRENGTH]=rng()%32;
                if (air) world.game.map.setAirUnit(x,y,u->gid);
                else world.game.map.setGroundUnit(x,y,u->gid);
            }
            turret->owner->enemies=trial%3==0 ? 2 : 6;
            turret->owner->sharedVisionExchange=trial%7==0 ? 2 : 0;
            const auto observation=observe(world.game);
            compare(*turret,observation);
            const auto before=world.checksum();
            const auto full=plan(*turret,0,2);
            const auto consumer=firstValid(*turret,full.entries);
            (void)consumer;
            CHECK(world.checksum()==before);
        }
    }
    TEST_CASE("indexed encounter order covers custom footprints repeated wraps and temporary chunks")
    {
        glob2test::HeadlessGlobals globals;
        for (const auto dimensions : {std::pair{3,3},std::pair{5,4},std::pair{6,5}})
            for (const auto footprint : {std::pair{1,1},std::pair{2,2},std::pair{3,1},std::pair{2,3},std::pair{7,5},std::pair{40,35}}) {
                glob2test::HeadlessGame world({.wDec=dimensions.first,.hDec=dimensions.second,.teams=2,.loadDefaultRace=true,.header=true});
                auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
                const auto id=world.game.buildingsTypes.getTypeNum("defencetower",0,false);
                catalog["variants"][id]["properties"]["width"]=footprint.first;
                catalog["variants"][id]["properties"]["height"]=footprint.second;
                catalog["variants"][id]["semantics"]["occupiesGround"]=false;
                world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
                auto* turret=world.addBuilding("defencetower",world.game.map.getW()-1,world.game.map.getH()-1);
                turret->owner->enemies=2;
                std::mt19937 random(173);
                std::set<std::pair<int,int>> occupied;
                for (unsigned n=0;n<32;++n) {
                    int x,y;
                    do { x=random()%world.game.map.getW(); y=random()%world.game.map.getH(); } while (!occupied.emplace(x,y).second);
                    auto* unit=world.addUnit(WORKER,x,y,1); unit->hp=100; unit->speed=0;
                }
                for (int range : {1,2,5,17,35}) {
                    CAPTURE(dimensions.first); CAPTURE(dimensions.second); CAPTURE(footprint.first); CAPTURE(footprint.second); CAPTURE(range);
                    // The independent original selector remains the head oracle.
                    auto changed=catalog;
                    changed["variants"][id]["properties"]["shootingRange"]=range;
                    world.game.buildingsTypes.loadSnapshotJson(changed.dump()); world.game.configureBuildingCatalog();
                    turret->bindType(id);
                    const auto observed=observe(world.game);
                    compare(*turret,observed);
                    std::vector<Entry> expected;
                    std::set<Uint16> seen;
                    const auto visit=[&](int x,int y) {
                        const auto gid=world.game.map.getGroundUnit(x,y);
                        if (gid==NOGUID || Unit::GIDtoTeam(gid)!=1 || !seen.insert(gid).second) return;
                        const auto* unit=world.game.resolveUnit(Game::refOf(world.game.teams[1]->myUnits[Unit::GIDtoID(gid)]));
                        REQUIRE(unit); expected.push_back({Kind::Unit,gid,unit->scriptIdentity});
                    };
                    // Test-only geometry oracle: all stationary equal-HP workers
                    // tie in the same tier, so first encounter determines order.
                    if (footprint==std::pair{2,2}) {
                        for (int ring=0;ring<=range;++ring) for (int offset=0;offset<=ring;++offset) for (int octant=0;octant<8;++octant) {
                            int x,y; BuildingUtils::turretScanTile(turret->posX,turret->posY,ring,offset,octant,x,y); visit(x,y);
                        }
                    } else {
                        const int sx=turret->posX,sy=turret->posY,w=footprint.first,h=footprint.second;
                        for (int y=sy;y<sy+h;++y) for (int x=sx;x<sx+w;++x) visit(x,y);
                        for (int ring=1;ring<=range;++ring) {
                            for (int x=sx-ring;x<=sx+w-1+ring;++x) { visit(x,sy-ring); visit(x,sy+h-1+ring); }
                            for (int y=sy-ring+1;y<sy+h-1+ring;++y) { visit(sx-ring,y); visit(sx+w-1+ring,y); }
                        }
                    }
                    const auto result=plan(source(*turret),observed,0);
                    CHECK(result.entries==expected);
                    auto temporary=observed; temporary.index.reset();
                    CHECK(plan(source(*turret),temporary,0).entries==expected);
                    world.game.map.paintCell(0,0,BOULDERS);
                    compare(*turret,observe(world.game));
                    world.game.map.paintCell(0,0,GRASS);
                }
            }
    }
    TEST_CASE("ground movement return suppresses air only in preferred fold")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8);
        auto* ground=world.addUnit(WARRIOR,10,8,1);
        auto* air=world.addUnit(EXPLORER,10,8,1);
        REQUIRE(turret); REQUIRE(ground); REQUIRE(air);
        ground->speed=32; ground->delta=255; air->speed=0; air->performance[FLY]=8;
        world.game.map.setGroundUnit(10,8,ground->gid);
        world.game.map.setAirUnit(10,8,air->gid);
        turret->owner->enemies=2;
        const auto planned=plan(*turret,4);
        CHECK_FALSE(planned.preferred.found());
        REQUIRE(!planned.entries.empty());
        CHECK(firstValid(*turret,planned.entries).entry.gid==air->gid);
        compare(*turret,observe(world.game));
    }
    TEST_CASE("empty range has one hundred armed turrets and no clearance work")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=2,.loadDefaultRace=true,.header=true});
        std::vector<Building*> turrets;
        for (int i=0;i<100;++i) {
            auto* b=world.addBuilding("defencetower",4+(i%10)*10,4+(i/10)*10);
            REQUIRE(b); b->bullets=10; b->owner->enemies=2; turrets.push_back(b);
        }
        const auto observation=observe(world.game);
        for (auto* b:turrets) {
            compare(*b,observation);
            const auto planned=plan(*b,4);
            CHECK(planned.metrics.visits==0);
            CHECK(planned.metrics.occupied==0);
            CHECK(planned.metrics.clearance==0);
            CHECK(planned.entries.empty());
        }
    }
    TEST_CASE("custom footprints blockers building offsets and frozen combat facts")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const auto id=world.game.buildingsTypes.getTypeNum("defencetower",0,false);
        catalog["variants"][id]["properties"]["width"]=3;
        catalog["variants"][id]["properties"]["height"]=1;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        auto* turret=world.addBuilding("defencetower",61,0);
        auto* building=world.addBuilding("swarm",3,0,0,1);
        REQUIRE(turret); REQUIRE(building); turret->owner->enemies=2;
        compare(*turret,observe(world.game));
        const auto buildingPlan=plan(*turret,4);
        REQUIRE(buildingPlan.preferred.found());
        CHECK(buildingPlan.preferred.entry.kind==Kind::Building);
        CHECK(firstValid(*turret,buildingPlan.entries).entry==buildingPlan.preferred.entry);
        world.game.map.paintCell(2,0,BOULDERS);
        compare(*turret,observe(world.game));
        auto* enemy=world.addUnit(WARRIOR,0,2,1);
        REQUIRE(enemy); enemy->speed=0;
        const auto frozen=observe(world.game);
        const auto capturedSource=source(*turret);
        const auto before=plan(capturedSource,frozen,8);
        enemy->performance[ATTACK_STRENGTH]+=100;
        enemy->hp=1;
        CHECK(plan(capturedSource,frozen,8).entries==before.entries);
        compare(*turret,observe(world.game));
    }
    TEST_CASE("observing and shadow consumption preserve complete save bytes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8);
        auto* enemy=world.addUnit(WARRIOR,10,8,1);
        REQUIRE(turret); REQUIRE(enemy); enemy->speed=0;
        turret->bullets=10; turret->owner->enemies=2;
        const auto save=[&] {
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);
            world.game.save(&output,false,"turret shadow purity"); output.flush();
            return backend->takeContents();
        };
        // The first save initializes the map-header offset used by SHA1.
        // Establish a stable save boundary before checking observation purity.
        (void)save();
        const auto before=save();
        REQUIRE(save()==before);
        const auto checksum=world.checksum();
        const auto observed=observe(world.game);
        const auto planned=plan(source(*turret),observed,16,2);
        CHECK(firstValid(*turret,planned.entries).found());
        CHECK(world.checksum()==checksum);
        CHECK(save()==before);
    }
    TEST_CASE("shared executor team jobs agree across participants and overlapping generations")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=8,.loadDefaultRace=true,.header=true});
        std::array<std::vector<Source>,8> teams;
        for (unsigned team=0;team<8;++team) for (unsigned n=0;n<12;++n) {
            const int x=4+10*n,y=4+12*team;
            auto* b=world.addBuilding("defencetower",x,y,0,team);
            b->owner->enemies=1u<<((team+1)%8);
            auto* u=world.addUnit(WARRIOR,x+3,y,(team+1)%8); u->speed=0;
            teams[team].push_back(source(*b));
        }
        const auto observed=observe(world.game);
        struct Job {
            Observation observation;
            std::vector<Source> sources;
            unsigned capacity,margin,perturb;
            std::vector<Plan> output;
            static void run(void* context,std::size_t) {
                auto& job=*static_cast<Job*>(context);
                for (unsigned n=0;n<job.perturb;++n) std::this_thread::yield();
                for (const auto& source:job.sources) job.output.push_back(plan(source,job.observation,job.capacity,job.margin));
            }
        };
        std::array<std::array<std::vector<Plan>,8>,4> controls;
        for (unsigned generation=0;generation<4;++generation) for (unsigned team=0;team<8;++team)
            for (const auto& source:teams[team]) controls[generation][team].push_back(plan(source,observed,generation%2 ? 16 : 8,generation%3));
        // Change live combat facts after capture; jobs must use only immutable inputs.
        for (int team=0;team<8;++team) for (auto* unit:world.game.teams[team]->liveUnits.entries()) if (unit) unit->hp=1;
        for (unsigned participants : {1,2,4,8}) for (bool reverse : {false,true}) {
            ComputeExecutor executor; executor.configure(participants);
            std::array<std::array<Job,8>,4> jobs;
            std::array<ComputeExecutor::Batch,4> pending;
            for (unsigned generation=0;generation<4;++generation) {
                std::vector<ComputeExecutor::Group> groups;
                for (unsigned ordinal=0;ordinal<8;++ordinal) {
                    const unsigned team=reverse ? 7-ordinal : ordinal;
                    auto& job=jobs[generation][team];
                    job={observed,teams[team],generation%2 ? 16u : 8u,generation%3,(team+generation)%8,{}};
                    groups.push_back({1,{Job::run,&job}});
                }
                pending[generation]=executor.submit(groups,ComputeExecutor::advanceDue(generation+4));
            }
            for (int generation=3;generation>=0;--generation) {
                executor.join(pending[generation]);
                for (unsigned team=0;team<8;++team) {
                    const auto& output=jobs[generation][team].output;
                    REQUIRE(output.size()==controls[generation][team].size());
                    for (unsigned n=0;n<output.size();++n) {
                        CHECK(output[n].entries==controls[generation][team][n].entries);
                        CHECK(output[n].preferred.entry==controls[generation][team][n].preferred.entry);
                        CHECK(output[n].preferred.x==controls[generation][team][n].preferred.x);
                        CHECK(output[n].preferred.y==controls[generation][team][n].preferred.y);
                    }
                }
            }
        }
    }
    TEST_CASE("captured offense preserves glass cannon saturation and active area modifiers")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const auto id=world.game.buildingsTypes.getTypeNum("stonewall",0,false);
        catalog["variants"][id]["semantics"]["areaEffects"]={{"radius",4},{"attackBuffBps",5000}};
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        REQUIRE(world.addBuilding("stonewall",8,8,0,1));
        auto* unit=world.addUnit(WARRIOR,9,8,1); REQUIRE(unit);
        world.game.areaEffects.beginTick(world.game);
        REQUIRE(world.game.areaEffects.enabled());
        for (unsigned level : {0,1,2}) for (int strength : {-1,0,17,INT_MAX}) for (int experience : {0,7,INT_MAX}) {
            world.game.gameHeader.setGlassCannonLevel(level);
            unit->performance[ATTACK_STRENGTH]=strength; unit->experienceLevel=experience;
            const auto original=observe(world.game);
            const auto& entities=*original.world.entities;
            const auto slot=Unit::GIDtoTeam(unit->gid)*Unit::MAX_COUNT+Unit::GIDtoID(unit->gid);
            const auto& captured=entities.units.at(entities.unitSlotIndices.at(slot));
            CHECK(captured.realAttackStrength==unit->getRealAttackStrength());
        }
    }
    TEST_CASE("cooldown admission includes exact serving boundary and empty ammunition")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned delay : {1,2,4}) for (unsigned refresh : {1,2,4}) {
            if (delay+refresh-1>4) continue;
            for (unsigned readyOffset : {0,1,2,3,4}) {
                glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
                auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
                turret->owner->addToStaticAbilitiesLists(turret);
                turret->bullets=0;
                turret->shootingCooldown=readyOffset*turret->runtime->shootRhythm;
                world.game.configureTurretTargeting(delay,refresh,8,1);
                const bool admitted=readyOffset<=delay+refresh-1;
                auto* pipeline=world.game.turretTargeting();
                world.game.observeCompletedWorld();
                pipeline->drain();
                CHECK(pipeline->statistics().sources==unsigned(admitted));
                world.game.stepCounter=delay;
                pipeline->publish(world.game);
                CHECK(pipeline->statistics().publications==unsigned(admitted));
            }
        }
    }
    TEST_CASE("forecast refresh arrives before the first ready step after zero crossing")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
        turret->owner->addToStaticAbilitiesLists(turret);
        auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
        turret->shootingCooldown=turret->runtime->shootRhythm;
        world.game.configureTurretTargeting(1,1,8,1);
        auto* pipeline=world.game.turretTargeting();
        world.game.observeCompletedWorld();
        CHECK(pipeline->statistics().sources==1);
        CHECK_FALSE(turret->tickShootingCooldown()); // Step at counter zero.
        CHECK(turret->shootingCooldown<=0);
        world.game.stepCounter=1;
        pipeline->publish(world.game);
        REQUIRE(!pipeline->entries(*turret).empty());
        CHECK(turret->tickShootingCooldown()); // First ready step at counter one.
        CHECK(firstValid(*turret,pipeline->entries(*turret)).found());
    }
    TEST_CASE("shared completed boundary captures once and publishes only at deadlines")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1,2,4,8}) for (unsigned delay : {1,2,4}) {
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
            world.game.map.computeExecutor().configure(participants);
            auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
            turret->owner->addToStaticAbilitiesLists(turret);
            turret->shootingCooldown=0; // This fixture tests admission/publication, not cooling.
            auto* enemy=world.addUnit(WARRIOR,11,8,1); enemy->speed=0;
            world.game.configureTurretTargeting(delay,1,8,1);
            auto* pipeline=world.game.turretTargeting();
            const auto before=world.game.snapshots().metrics.captures;
            const auto shared=world.game.observeCompletedWorld();
            CHECK(world.game.snapshots().metrics.captures==before+1);
            const auto job=shared.turretView();
            CHECK(job.entities==shared.entities); CHECK(job.occupancy==shared.occupancy);
            CHECK(job.terrain==shared.terrain); CHECK(job.catalogs==shared.catalogs);
            CHECK(job.teams==shared.teams); CHECK(job.effects==shared.effects);
            const auto aiReader=world.game.observeCompletedWorld();
            CHECK(aiReader.entities==shared.entities);
            world.game.admitAIOrders({},true,{},shared);
            world.game.deliverAIOrders({},true);
            CHECK(world.game.snapshots().metrics.captures==before+1);
            CHECK(pipeline->statistics().admissions==1);
            world.game.observeCompletedWorld();
            CHECK(pipeline->statistics().admissions==1);
            pipeline->drain();
            CHECK(pipeline->entries(*turret).empty()); // Save/pause drain never publishes early.
            for (unsigned t=1;t<=delay;++t) {
                world.game.stepCounter=t;
                const auto before=world.game.snapshots().metrics.captures;
                world.game.observeCompletedWorld();
                CHECK(world.game.snapshots().metrics.captures==before+1);
                pipeline->publish(world.game);
                if (t<delay) CHECK(pipeline->entries(*turret).empty());
            }
            REQUIRE(!pipeline->entries(*turret).empty());
            CHECK(firstValid(*turret,pipeline->entries(*turret)).entry==plan(*turret,8,1).preferred.entry);
            // Edits keep the list; firing revalidates every entry against live state.
            ++turret->posX;
            CHECK_FALSE(pipeline->entries(*turret).empty());
            pipeline->drain();
        }
    }
    TEST_CASE("overlapping publication preserves other sources through upgrade and incarnation changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=7,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        std::vector<Building*> turrets;
        for (int n=0;n<16;++n) {
            auto* turret=world.addBuilding("defencetower",8+(n%8)*12,8+(n/8)*20);
            REQUIRE(turret); turret->owner->enemies=2; turret->shootingCooldown=0;
            turret->owner->addToStaticAbilitiesLists(turret);
            auto* unit=world.addUnit(WARRIOR,turret->posX+3,turret->posY,1);
            REQUIRE(unit); unit->speed=0;
            turrets.push_back(turret);
        }
        world.game.configureTurretTargeting(4,1,8,1);
        auto* pipeline=world.game.turretTargeting();
        auto* changed=turrets[7]; const auto* original=changed->runtime;
        const auto originalEntries=plan(*changed,8,1).entries;
        REQUIRE_FALSE(originalEntries.empty());
        world.game.observeCompletedWorld();
        auto upgraded=*original; ++upgraded.shootingRange; changed->runtime=&upgraded;
        for (unsigned tick=1;tick<=5;++tick) {
            world.game.stepCounter=tick;
            world.game.observeCompletedWorld();
            pipeline->publish(world.game);
            if (tick==4) {
                // The T0 result belongs to this incarnation even after an
                // upgrade. Firing revalidates it against the live weapon;
                // increasing range leaves the original preferred target valid.
                CHECK(pipeline->entries(*changed)==originalEntries);
                CHECK(firstValid(*changed,pipeline->entries(*changed)).entry==plan(*changed,8,1).preferred.entry);
                for (auto* turret:turrets) if (turret!=changed) {
                    CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
                    CHECK(firstValid(*turret,pipeline->entries(*turret)).entry==plan(*turret,8,1).preferred.entry);
                }
            }
        }
        for (auto* turret:turrets) CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
        const auto incarnation=changed->scriptIdentity;
        changed->scriptIdentity+=100000;
        CHECK(pipeline->entries(*changed).empty());
        pipeline->publish(world.game); // Remove the old incarnation in the middle of the table.
        for (auto* turret:turrets) if (turret!=changed) CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
        world.game.stepCounter=6;
        world.game.observeCompletedWorld();
        pipeline->drain();
        world.game.stepCounter=10;
        pipeline->publish(world.game);
        CHECK(pipeline->entries(*changed)==plan(*changed,8,1).entries);
        changed->runtime=original; changed->scriptIdentity=incarnation;
    }
    TEST_CASE("overlapping generations reuse the previous logical index before publication")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1,2,4,8}) {
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
            world.game.map.computeExecutor().configure(participants);
            for (int team=0;team<2;++team) {
                auto* turret=world.addBuilding("defencetower",8+24*team,8,0,team);
                turret->owner->enemies=1u<<(1-team);
                turret->owner->addToStaticAbilitiesLists(turret);
                turret->shootingCooldown=0; // This fixture tests admission/publication, not cooling.
                auto* target=world.addUnit(WARRIOR,turret->posX+3,8,1-team); REQUIRE(target); target->speed=0;
            }
            world.game.configureTurretTargeting(4,1,8,1);
            auto* pipeline=world.game.turretTargeting();
            std::size_t chunks=0;
            for (unsigned tick=0;tick<4;++tick) {
                world.game.stepCounter=tick;
                const auto captured=world.game.observeCompletedWorld();
                chunks=captured.occupancy->stamps.chunks.size();
            }
            pipeline->drain();
            CHECK(pipeline->statistics().teamJobs==8);
            world.game.stepCounter=7;
            pipeline->publish(world.game);
            for (int team=0;team<2;++team) for (auto* turret:world.game.teams[team]->turrets)
                CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
        }
    }
    TEST_CASE("published slot reuse replaces records by incarnation")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1,4}) {
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
            world.game.map.computeExecutor().configure(participants);
            auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
            turret->owner->addToStaticAbilitiesLists(turret);
            turret->shootingCooldown=0; // This fixture tests admission/publication, not cooling.
            auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
            world.game.configureTurretTargeting(1,1,8,1);
            auto* pipeline=world.game.turretTargeting();
            const auto identity=turret->scriptIdentity;
            for (unsigned reuse=0;reuse<100;++reuse) {
                turret->scriptIdentity=identity+reuse;
                if (reuse) CHECK(pipeline->entries(*turret).empty());
                world.game.observeCompletedWorld();
                pipeline->drain();
                ++world.game.stepCounter;
                pipeline->publish(world.game);
                CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
            }
            turret->scriptIdentity=identity;
        }
    }
    TEST_CASE("turret admission requires due teams and survives paused repeated boundaries")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8,0,1); turret->owner->enemies=1;
        turret->owner->addToStaticAbilitiesLists(turret);
        turret->shootingCooldown=0; // This fixture tests admission/publication, not cooling.
        world.game.configureTurretTargeting(1,2,8,1);
        auto* pipeline=world.game.turretTargeting();
        auto shared=world.game.observeCompletedWorld();
        CHECK(shared.entities);
        world.game.stepCounter=1;
        // Team 1 is due at T1. Authoritative world work is admitted once even
        // on a paused replica; pausing AI polling is a separate responsibility.
        world.game.observeCompletedWorld();
        CHECK(pipeline->statistics().admissions==1);
        world.game.observeCompletedWorld();
        CHECK(pipeline->statistics().admissions==1);
        world.game.invalidateObservation(); // A paused edit does not readmit this logical tick.
        world.game.observeCompletedWorld();
        CHECK(pipeline->statistics().admissions==1);
        world.game.stepCounter=2;
        pipeline->drain(); CHECK(pipeline->statistics().publications==0);
        pipeline->publish(world.game); CHECK(pipeline->statistics().publications==1);
        CHECK_THROWS_AS(world.game.configureTurretTargeting(4,2,8,1),std::invalid_argument);
    }
    TEST_CASE("engine-style stepping reuses admitted boundary after orders mutate occupancy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
        turret->owner->addToStaticAbilitiesLists(turret);
        turret->shootingCooldown=0; // This fixture tests admission/publication, not cooling.
        world.game.configureTurretTargeting(1,1,8,1);
        const auto shared=world.game.observeCompletedWorld();
        const auto captures=world.game.snapshots().metrics.captures;
        REQUIRE(world.addUnit(WARRIOR,11,8,1)); // Represents an order after completed capture.
        world.game.setWaitingOnMask(0);
        world.game.advanceLiveSimulation(0);
        CHECK(world.game.snapshots().metrics.captures==captures); // Orders never reconstruct the published observation.
        CHECK(world.game.turretTargeting()->statistics().admissions==1);
        CHECK(world.game.turretTargeting()->statistics().publications==0);
        world.game.observeCompletedWorld();
        CHECK(world.game.snapshots().metrics.captures==captures+1);
        CHECK(world.game.turretTargeting()->statistics().admissions==2);
    }
    TEST_CASE("shared turret worker failures are reported at publication after draining")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1,2,8}) {
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
            world.game.map.computeExecutor().configure(participants);
            auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
            turret->owner->addToStaticAbilitiesLists(turret);
            turret->shootingCooldown=0; // This fixture tests admission/publication, not cooling.
            world.game.configureTurretTargeting(1,1,8,1);
            turret->posX=-1; // Corrupt source geometry forces the immutable worker to fail.
            world.game.observeCompletedWorld();
            auto* pipeline=world.game.turretTargeting();
            CHECK_NOTHROW(pipeline->drain());
            CHECK(pipeline->statistics().publications==0);
            turret->posX=8; world.game.stepCounter=1;
            CHECK_THROWS_AS(pipeline->publish(world.game),std::invalid_argument);
            CHECK(pipeline->entries(*turret).empty());
        }
    }
    TEST_CASE("pinned large building keeps its original unwrapped aim offset")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const auto turretId=world.game.buildingsTypes.getTypeNum("defencetower",0,false);
        const auto targetId=world.game.buildingsTypes.getTypeNum("swarm",0,false);
        catalog["variants"][turretId]["semantics"]["occupiesGround"]=false;
        catalog["variants"][targetId]["properties"]["width"]=64;
        catalog["variants"][targetId]["properties"]["height"]=64;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        auto* turret=world.addBuilding("defencetower",31,31);
        world.addBuilding("swarm",0,0,0,1);
        auto* air=world.addUnit(EXPLORER,31,31,1,3);
        REQUIRE(turret); REQUIRE(air); turret->owner->enemies=2;
        air->performance[FLY]=8; air->speed=32; air->delta=255;
        world.game.map.setGroundUnit(31,31,NOGUID); world.game.map.setAirUnit(31,31,air->gid);
        const auto planned=plan(*turret,4);
        REQUIRE(planned.preferred.found());
        REQUIRE(planned.preferred.entry.kind==Kind::Building);
        CHECK(planned.preferred.x==32);
        CHECK(planned.entries.front().offsetX==32);
        CHECK(firstValid(*turret,planned.entries).x==planned.preferred.x);
        compare(*turret,observe(world.game));
    }
    TEST_CASE("saved target lists retain admissions deadlines and ordered entries")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1,2,4,8}) for (unsigned delay : {1,2,4}) {
            glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
            auto& game=world.game; game.map.configureCompute(participants);
            auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
            turret->owner->addToStaticAbilitiesLists(turret); turret->shootingCooldown=0; turret->bullets=0;
            auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
            const unsigned refresh=delay==4 ? 1 : 2;
            game.configureTurretTargeting(delay,refresh,3,1);
            for (unsigned phase=0;phase<=delay;++phase) {
                game.stepCounter=phase;
                game.observeCompletedWorld(); game.turretTargeting()->publish(game);
                const auto entries=game.turretTargeting()->entries(*turret);
                auto* backend=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream out(backend); game.turretTargeting()->save(game,&out); out.flush();
                const auto bytes=backend->takeContents();
                game.configureTurretTargeting(delay,refresh,3,1);
                GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
                in.seekFromStart(0);
                REQUIRE(game.turretTargeting()->load(game,&in,VERSION_MINOR));
                CHECK(game.turretTargeting()->entries(*turret)==entries);
                CHECK_FALSE(game.turretTargeting()->admissionPending(game,false));
            }
            game.stepCounter+=delay; game.turretTargeting()->publish(game);
            REQUIRE(!game.turretTargeting()->entries(*turret).empty());
            CHECK(firstValid(*turret,game.turretTargeting()->entries(*turret)).found());
            const auto ordered=game.turretTargeting()->entries(*turret);
            turret->owner->enemies=0;
            CHECK(game.turretTargeting()->entries(*turret)==ordered);
            CHECK_FALSE(firstValid(*turret,game.turretTargeting()->entries(*turret)).found());
            turret->owner->enemies=2;
            CHECK(firstValid(*turret,game.turretTargeting()->entries(*turret)).found());
        }
    }
    TEST_CASE("historical saves start with a blank turret cache and retain current resave continuation [save-format]")
    {
        glob2test::HeadlessGlobals globals;
        const auto bytes=glob2test::readFile(glob2test::inflated("team-stats/version88.game.gz"));
        GameGUI historical;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        REQUIRE(historical.game.load(&input));
        REQUIRE(historical.game.mapHeader.getVersionMinor()==88);
        auto* pipeline=historical.game.turretTargeting();
        CHECK(pipeline->statistics().admissions==0);
        for (int team=0;team<historical.game.teamsCount();++team)
            for (const auto* turret:historical.game.teams[team]->turrets)
                CHECK(pipeline->entries(*turret).empty());
        auto* checkpoint=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(checkpoint);
        historical.game.save(&out,false,"historical empty turret cache");
        out.flush();
        GameGUI resumed;
        GAGCore::BinaryInputStream saved(new GAGCore::MemoryStreamBackend(*checkpoint));
        saved.seekFromStart(0);
        REQUIRE(resumed.game.load(&saved));
        const auto state=[](Game& game) {
            std::vector<Uint32> fields,buildings,units;
            game.checkSum(&fields,&buildings,&units,true);
            fields.erase(fields.begin()); // Historical versus current header format.
            return std::tuple(fields,buildings,units);
        };
        for (int tick=0;tick<32;++tick) {
            REQUIRE(state(historical.game)==state(resumed.game));
            CHECK(glob2test::stepAI(historical.game)==glob2test::stepAI(resumed.game));
        }
    }
    TEST_CASE("new cached saves roundtrip and unsupported formats are rejected")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
        auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
        glob2test::prepareTurretTargets(*turret);
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(backend); world.game.save(&out,false,"cached format boundary"); out.flush();
        const auto bytes=backend->takeContents(); const auto checksum=world.game.checkSum();
        glob2test::HeadlessGame resumed({.teams=2,.loadDefaultRace=true,.header=true});
        GAGCore::BinaryInputStream current(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        current.seekFromStart(0);
        REQUIRE(resumed.game.load(&current)); CHECK(resumed.game.checkSum()==checksum);
        GAGCore::BinaryInputStream prefix(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        prefix.seekFromStart(0);
        prefix.readText("mapName"); prefix.readSint32("versionMajor"); const auto minorOffset=prefix.getPosition();
        REQUIRE(prefix.readSint32("versionMinor")==VERSION_MINOR);
        auto* patchBackend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream patch(patchBackend); patch.writeSint32(MINIMUM_VERSION_MINOR-1,"versionMinor"); patch.flush();
        const auto encoded=patchBackend->takeContents(); auto older=bytes;
        std::copy(encoded.begin(),encoded.end(),older.begin()+minorOffset);
        GAGCore::BinaryInputStream rejected(new GAGCore::MemoryStreamBackend(older.data(),older.size()));
        rejected.seekFromStart(0);
        CHECK_FALSE(resumed.game.load(&rejected));
        CHECK(MINIMUM_VERSION_MINOR==58); // Maps and saves share the durable floor.
    }
    TEST_CASE("bounded capacities are prefixes of uncapped tier ranking")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",20,20);
        REQUIRE(turret); turret->owner->enemies=2;
        for (int n=0;n<24;++n) {
            auto* u=world.addUnit(WORKER,22+n%8,20+n/8,1);
            REQUIRE(u); u->hp=n+1; u->speed=0;
        }
        world.addBuilding("swarm",24,24,0,1);
        const auto observed=observe(world.game);
        const auto full=plan(source(*turret),observed,0,2);
        REQUIRE(full.entries.size()>16);
        REQUIRE(full.preferred.found());
        CHECK(full.entries.front()==full.preferred.entry);
        bool sawBuilding=false,sawGuard=false;
        for (const auto& e:full.entries) {
            if (e.kind==Kind::Building) { CHECK_FALSE(sawGuard); sawBuilding=true; }
            else {
                const auto* u=world.game.resolveUnit({e.gid,e.incarnation});
                REQUIRE(u);
                if (u->posX>26) { REQUIRE(sawBuilding); sawGuard=true; }
                else CHECK_FALSE(sawBuilding);
            }
        }
        CHECK(sawBuilding); CHECK(sawGuard);
        for (unsigned capacity : {2,3,4,8,16,32}) {
            const auto limited=plan(source(*turret),observed,capacity,2);
            const auto size=std::min<std::size_t>(capacity,full.entries.size());
            CHECK(limited.entries==std::vector<Entry>(full.entries.begin(),full.entries.begin()+size));
        }
    }
    TEST_CASE("index shares unchanged chunks and refreshes changed occupancy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",0,0);
        REQUIRE(turret); turret->owner->enemies=2;
        const auto first=observe(world.game);
        REQUIRE(first.index);
        CHECK(first.rebuiltChunks==16);
        const auto second=observe(world.game,&first);
        CHECK(second.index==first.index);
        CHECK(second.rebuiltChunks==0);
        CHECK(second.sharedChunks==16);
        world.addUnit(WARRIOR,2,0,1);
        const auto third=observe(world.game,&second);
        CHECK(third.rebuiltChunks==1);
        CHECK(third.sharedChunks==15);
        compare(*turret,third);
        CHECK(plan(source(*turret),first,4).entries.empty());
        CHECK_FALSE(plan(source(*turret),third,4).entries.empty());
        auto* target=world.game.teams[1]->liveUnits.entries().front();
        REQUIRE(target);
        target->hp=1; target->experienceLevel=7;
        const auto fourth=observe(world.game,&third);
        CHECK(fourth.index==third.index);
        // Reusing occupancy must still rank against fresh combat facts.
        compare(*turret,fourth);
    }
    TEST_CASE("consumer rechecks incarnation occupancy alliance movement and cooldown boundaries")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8);
        auto* enemy=world.addUnit(WARRIOR,10,8,1);
        REQUIRE(turret); REQUIRE(enemy);
        enemy->speed=0; turret->owner->enemies=2;
        const auto planned=plan(*turret,4);
        REQUIRE(firstValid(*turret,planned.entries).found());
        enemy->isDead=true;
        CHECK_FALSE(firstValid(*turret,planned.entries).found());
        enemy->isDead=false;
        const int hp=enemy->hp; enemy->hp=0;
        CHECK_FALSE(firstValid(*turret,planned.entries).found());
        enemy->hp=hp;
        auto wrong=planned.entries; ++wrong.front().incarnation;
        CHECK_FALSE(firstValid(*turret,wrong).found());
        world.game.map.setGroundUnit(enemy->posX,enemy->posY,NOGUID);
        CHECK_FALSE(firstValid(*turret,planned.entries).found());
        world.game.map.setGroundUnit(enemy->posX,enemy->posY,enemy->gid);
        turret->owner->enemies=0;
        CHECK_FALSE(firstValid(*turret,planned.entries).found());
        turret->owner->enemies=2;
        enemy->speed=32; enemy->delta=255;
        CHECK_FALSE(firstValid(*turret,planned.entries).found());
        enemy->speed=0;
        CHECK(firstValid(*turret,planned.entries).found());
        enemy->performance[FLY]=8;
        CHECK_FALSE(firstValid(*turret,planned.entries).found());
        world.game.map.setGroundUnit(enemy->posX,enemy->posY,NOGUID);
        world.game.map.setAirUnit(enemy->posX,enemy->posY,enemy->gid);
        CHECK(firstValid(*turret,planned.entries).found());
        enemy->performance[FLY]=0;
        world.game.map.setAirUnit(enemy->posX,enemy->posY,NOGUID);
        world.game.map.setGroundUnit(enemy->posX,enemy->posY,enemy->gid);
        CHECK(firstValid(*turret,planned.entries).found());
        CHECK_FALSE(firstValid(*turret,{}).found());
        for (unsigned rhythm : {1,2,8,128}) for (int cooldown : {-1,0,1,2,127,128,129,256}) {
            turret->shootingCooldown=cooldown;
            auto runtime=*turret->runtime; runtime.shootRhythm=rhythm;
            const auto* original=turret->runtime; turret->runtime=&runtime;
            Uint64 steps=0;
            while (!turret->tickShootingCooldown()) ++steps;
            turret->runtime=original;
            CHECK(steps==stepsUntilReady(cooldown,rhythm));
        }
        CHECK(stepsUntilReady(1,0)==std::numeric_limits<Uint64>::max());
        for (int cooldown : {-1,0,1,INT_MAX})
            for (unsigned rhythm : {0u,1u,128u,std::numeric_limits<unsigned>::max()})
                for (unsigned delay : {0u,1u,4u,std::numeric_limits<unsigned>::max()})
                    for (unsigned refresh : {0u,1u,4u,std::numeric_limits<unsigned>::max()})
                        CHECK(refreshNeeded(cooldown,rhythm,delay,refresh)==
                            (stepsUntilReady(cooldown,rhythm)<=Uint64(delay)+refresh-1));
        for (unsigned delay : {1,2,4}) for (unsigned refresh : {1,2,4}) {
            if (delay+refresh-1>4) continue;
            for (unsigned rhythm : {1,2,128,1700}) for (int cooldown : {-1,0,1,128,1700,3400,65536}) {
                auto runtime=*turret->runtime; runtime.shootRhythm=rhythm;
                const auto* original=turret->runtime; turret->runtime=&runtime;
                turret->shootingCooldown=cooldown;
                bool ready=false;
                for (unsigned offset=0;offset<=delay+refresh-1;++offset) {
                    const bool attempt=turret->tickShootingCooldown();
                    if (offset>=delay) ready |= attempt;
                }
                turret->runtime=original;
                CHECK(refreshNeeded(cooldown,rhythm,delay,refresh)==ready);
            }
        }
    }
}
