// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TurretTargeting.h"
#include "AI.h"
#include "Player.h"
#include "ComputeExecutor.h"
#include "BuildingUtils.h"
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_START_INSTRUMENTATION ((void)0)
#define CALLGRIND_STOP_INSTRUMENTATION ((void)0)
#define CALLGRIND_ZERO_STATS ((void)0)
#define CALLGRIND_TOGGLE_COLLECT ((void)0)
#define CALLGRIND_DUMP_STATS_AT(label) ((void)0)
#endif
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
    const auto reference=turret.targetingReference();
    const auto live=plan(turret,8,2);
    const auto frozen=plan(source(turret),observation,8,2);
    const auto reused=observe(*turret.owner->game,observation.world,nullptr,true);
    CHECK(reused.offense==observation.offense);
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
        CHECK(live.preferred.priority==int(reference.type));
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
    TEST_CASE("retained index exhaustion rebuilds temporary chunks without changing results")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=2,.loadDefaultRace=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const auto tower=world.game.buildingsTypes.getTypeNum("defencetower",0,false);
        const auto swarm=world.game.buildingsTypes.getTypeNum("swarm",0,false);
        catalog["variants"][tower]["semantics"]["occupiesGround"]=false;
        catalog["variants"][swarm]["properties"]["width"]=64;
        catalog["variants"][swarm]["properties"]["height"]=64;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        for (int y : {0,64}) for (int x : {0,64}) world.addBuilding("swarm",x,y,0,1);
        auto* turret=world.addBuilding("defencetower",127,127); turret->owner->enemies=2;
        std::vector<Observation> held;
        bool rebuilt=false;
        for (unsigned generation=0;generation<280 && !rebuilt;++generation) {
            // Force index churn with valid unchanged occupancy; private stamps
            // are derived data and do not influence the selector or its digest.
            world.game.map.occupancyChanges.markAll();
            if (!held.empty()) { held.back().world={}; held.back().offense.clear(); }
            held.push_back(observe(world.game,held.empty() ? nullptr : &held.back()));
            CHECK(retainedIndexBytes(held.back())<=32u*1024u*1024u);
            const auto result=plan(source(*turret),held.back(),8,2);
            auto temporary=held.back(); temporary.index.reset();
            CHECK(result.entries==plan(source(*turret),temporary,8,2).entries);
            rebuilt=result.metrics.temporaryChunks>0;
        }
        REQUIRE(rebuilt);
        compare(*turret,held.back());
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
    TEST_CASE("Numbi and Castor orders checksums and saves agree with shadow queries [artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        struct Result {
            std::vector<std::vector<std::string>> orders;
            std::vector<Uint32> checksums;
            std::string save;
        };
        const auto run=[&](bool shadow) {
            setSyncRandSeed(713);
            Result result;
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=713});
            GameHeader header;
            header.setNumberOfPlayers(2); header.setRandomSeed(713);
            header.getBasePlayer(0)=BasePlayer(0,"Numbi",0,BasePlayer::playerTypeFromImplementationID(AI::NUMBI));
            header.getBasePlayer(1)=BasePlayer(1,"Castor",1,BasePlayer::playerTypeFromImplementationID(AI::CASTOR));
            world.game.setGameHeader(header,true); world.game.setWaitingOnMask(0);
            for (int team=0;team<2;++team) {
                const int offset=team*32;
                auto* owner=world.game.teams[team];
                owner->startPosX=4+offset; owner->startPosY=4+offset;
                owner->startPosSet=Team::START_POS_FROM_UNIT; owner->enemies=1u<<(1-team);
                auto* swarm=world.addBuilding("swarm",4+offset,4+offset,0,team);
                auto* inn=world.addBuilding("inn",10+offset,4+offset,0,team);
                for (auto* b : {swarm,inn}) { b->materials[WHEAT]=b->type->maxMaterial[WHEAT]; b->update(); }
                auto* turret=world.addBuilding("defencetower",16+offset,4+offset,0,team);
                turret->bullets=10;
                for (int n=0;n<12;++n) world.addUnit(n<8 ? WORKER : WARRIOR,4+offset+n,12+offset,team);
                for (int y=18+offset;y<24+offset;++y) for (int x=4+offset;x<20+offset;++x) world.game.map.setResourceByIndex(x,y,WHEAT,1);
                for (auto* b:owner->liveBuildings.entries()) if (b) owner->addToStaticAbilitiesLists(b);
                owner->stats.step(owner);
            }
            world.game.configureTurretTargeting(1,2,3,1);
            Observation previous;
            for (int tick=0;tick<256;++tick) {
                if (shadow) {
                    const std::array<unsigned,2> readers{0,1};
                    auto captured=observe(world.game,world.game.captureReadBoundary(),previous.world.entities ? &previous : nullptr);
                    for (int team=0;team<2;++team) for (const auto* b:world.game.teams[team]->liveBuildings.entries())
                        if (b && b->runtime->shootSpeed>=256) {
                            compare(*b,captured);
                            const auto planned=plan(source(*b),captured,8,2);
                            (void)firstValid(*b,planned.entries);
                        }
                    previous=std::move(captured);
                }
                result.orders.push_back(glob2test::stepAI(world.game));
                result.checksums.push_back(world.checksum());
            }
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);
            world.game.save(&output,false,"Numbi Castor shadow parity"); output.flush();
            const auto bytes=backend->takeContents(); result.save.assign(bytes.begin(),bytes.end());
            return result;
        };
        const auto reference=run(false),shadow=run(true);
        CHECK(shadow.orders==reference.orders);
        CHECK(shadow.checksums==reference.checksums);
        CHECK(shadow.save==reference.save);
        const auto retain=[&](const char* name,const Result& result) {
            const auto directory=glob2test::artifactDir();
            std::ofstream saved(directory/(std::string(name)+".game"),std::ios::binary);
            saved.write(result.save.data(),result.save.size()); saved.flush(); REQUIRE(saved.good());
            std::ofstream trace(directory/(std::string(name)+"-checksums.csv"));
            trace << "completed_tick,checksum\n";
            for (std::size_t t=0;t<result.checksums.size();++t) trace << t+1 << ',' << result.checksums[t] << '\n';
            trace.flush(); REQUIRE(trace.good());
            std::ofstream orders(directory/(std::string(name)+"-orders.csv"));
            orders << "tick,player,order_hex\n";
            constexpr char digits[]="0123456789abcdef";
            for (std::size_t t=0;t<result.orders.size();++t) for (std::size_t player=0;player<result.orders[t].size();++player) {
                orders << t << ',' << player << ',';
                for (unsigned char byte:result.orders[t][player]) orders << digits[byte>>4] << digits[byte&15];
                orders << '\n';
            }
            orders.flush(); REQUIRE(orders.good());
        };
        retain("reference",reference); retain("shadow",shadow);
    }
    TEST_CASE("Numbi Castor stock combat measures delayed shadow matrix [artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        const bool pipelineOnly=std::getenv("TURRET_PIPELINE_ONLY")!=nullptr;
        const unsigned actualCapacity=std::getenv("TURRET_CAPACITY") ? std::stoul(std::getenv("TURRET_CAPACITY")) : 3;
        const unsigned actualRefresh=std::getenv("TURRET_REFRESH") ? std::stoul(std::getenv("TURRET_REFRESH")) : 2;
        const unsigned actualDelay=std::getenv("TURRET_DELAY") ? std::stoul(std::getenv("TURRET_DELAY")) : 1;
        const std::string actualVariant=std::getenv("TURRET_VARIANT") ? std::getenv("TURRET_VARIANT") : "combined";
        struct Window { bool active=false; unsigned start=0; Entry reference; };
        struct Lists { Source source; bool initialized=false; std::vector<Entry> published; std::map<unsigned,std::vector<Entry>> pending; Window window; };
        struct Configuration {
            unsigned k,r,l,margin;
            std::map<std::pair<Uint16,Uint32>,Lists> turrets;
            unsigned opportunities=0,missed=0,censored=0,agreement=0,represented=0,success=0;
            std::vector<unsigned> delays;
            Metrics metrics;
        };
        struct Research {
            Game* game;
            struct Actual {
                std::map<std::pair<Uint16,Uint32>,Window> windows;
                unsigned opportunities=0,agreement=0,missed=0,censored=0;
                std::vector<unsigned> delays;
            } actual;
            std::vector<Configuration> matrix;
            unsigned eligible=0,shots=0,found=0,unitShots=0,buildingShots=0,combatEligible=0,combatFound=0;
            static void attempt(void* opaque,const Building& turret) {
                auto& self=*static_cast<Research*>(opaque);
                const unsigned t=self.game->stepCounter;
                const auto fresh=plan(turret,0,2);
                const auto reference=fresh.preferred;
                const auto oracle=turret.targetingReference();
                CHECK(reference.found()==oracle.found());
                if (oracle.found()) { CHECK(reference.x==oracle.x); CHECK(reference.y==oracle.y); CHECK(reference.ticks==oracle.ticks); }
                const bool fires=turret.shootingCooldown<=0 && turret.bullets>0 && reference.found()
                    && turret.computeFiringSolution(reference.x,reference.y).ticksLeft<reference.ticks;
                if (turret.shootingCooldown<=0 && turret.bullets>0) { ++self.eligible; self.found+=reference.found(); if (t<256) { ++self.combatEligible; self.combatFound+=reference.found(); } }
                if (fires) {
                    ++self.shots; self.unitShots+=reference.entry.kind==Kind::Unit; self.buildingShots+=reference.entry.kind==Kind::Building;
                    const auto control=firstValid(turret,fresh.entries);
                    REQUIRE(control.found()); CHECK(control.entry==reference.entry); CHECK(control.x==reference.x); CHECK(control.y==reference.y);
                }
                auto& actual=self.actual;
                auto& window=actual.windows[{turret.gid,turret.scriptIdentity}];
                const auto candidate=firstValid(turret,self.game->turretTargeting()->entries(turret));
                if (window.active && !firstValid(turret,{window.reference}).found()) { ++actual.missed; window.active=false; }
                if (fires) {
                    if (window.active) { ++actual.censored; window.active=false; }
                    ++actual.opportunities;
                    actual.agreement+=candidate.found() && candidate.entry==reference.entry && candidate.x==reference.x && candidate.y==reference.y;
                    window={true,t,reference.entry};
                }
                if (window.active && candidate.found()) { actual.delays.push_back(t-window.start); window.active=false; }
                for (auto& c:self.matrix) {
                    auto& lists=c.turrets[{turret.gid,turret.scriptIdentity}];
                    // Orders and team stepping can change the source after the
                    // completed-world capture, before this exact firing attempt.
                    const auto currentSource=source(turret);
                    if (lists.initialized && !sameSource(lists.source,currentSource)) {
                        lists.published.clear(); lists.pending.clear();
                        if (lists.window.active) { ++c.censored; lists.window.active=false; }
                    }
                    lists.source=currentSource; lists.initialized=true;
                    const auto candidate=firstValid(turret,lists.published,&c.metrics);
                    auto& window=lists.window;
                    if (window.active && !firstValid(turret,{window.reference}).found()) {
                        ++c.missed; window.active=false;
                    }
                    if (fires) {
                        // Every actual original shot begins a first-shot opportunity.
                        // Follow its target's remaining shootable window on this same
                        // reference trajectory, independently of the ensuing cooldown.
                        if (window.active) { ++c.censored; window.active=false; }
                        ++c.opportunities;
                        window={true,t,reference.entry};
                        c.success+=candidate.found();
                        c.agreement+=candidate.found() && candidate.entry==reference.entry
                            && candidate.x==reference.x && candidate.y==reference.y;
                        c.represented+=std::find(lists.published.begin(),lists.published.end(),reference.entry)!=lists.published.end();
                    }
                    if (window.active && candidate.found()) {
                        c.delays.push_back(t-window.start); window.active=false;
                    }
                }
            }
        };
        for (bool fortified : {false,true}) for (unsigned seed : {713,1919,4201,7919,104729,15485863,32452843,49979687,67867967,86028121}) {
            const auto fixture=std::string(fortified ? "fortified" : "skirmish");
            struct Result { std::vector<std::vector<std::string>> orders; std::vector<Uint32> checksums; std::string save; };
            const auto run=[&](bool shadow) {
                setSyncRandSeed(seed);
                glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=seed});
                GameHeader header; header.setNumberOfPlayers(2); header.setRandomSeed(seed);
                header.getBasePlayer(0)=BasePlayer(0,"Numbi",0,BasePlayer::playerTypeFromImplementationID(AI::NUMBI));
                header.getBasePlayer(1)=BasePlayer(1,"Castor",1,BasePlayer::playerTypeFromImplementationID(AI::CASTOR));
                world.game.setGameHeader(header,true); world.game.setWaitingOnMask(0);
                for (int team=0;team<2;++team) {
                    auto* owner=world.game.teams[team];
                    const int home=team ? 38 : 6;
                    owner->startPosX=home; owner->startPosY=6; owner->startPosSet=Team::START_POS_FROM_UNIT;
                    owner->enemies=1u<<(1-team);
                    for (const char* kind : {"swarm","inn"}) {
                        auto* b=world.addBuilding(kind,home,kind==std::string("swarm") ? 6 : 12,0,team);
                        REQUIRE(b); b->materials[WHEAT]=b->type->maxMaterial[WHEAT]; b->update();
                    }
                    for (int n=0;n<2;++n) {
                        auto* b=world.addBuilding("defencetower",team ? 30 : 16,20+n*12,0,team);
                        REQUIRE(b); b->bullets=b->type->maxBullets;
                        b->materials[STONE]=b->type->maxMaterial[STONE]; b->update();
                    }
                    if (fortified) {
                        auto* outpost=world.addBuilding("inn",team ? 20 : 26,team ? 36 : 14,0,team);
                        REQUIRE(outpost); outpost->materials[WHEAT]=outpost->type->maxMaterial[WHEAT]; outpost->update();
                    }
                    for (int n=0;n<12;++n) REQUIRE(world.addUnit(WORKER,home+n%4,17+n/4,team));
                    // Stock ground and airborne forces start between the colonies;
                    // all subsequent movement, combat and production is simulation/AI.
                    for (int n=0;n<24;++n) REQUIRE(world.addUnit(WARRIOR,team ? 20+n%3 : 27+n%3,18+n/3,team));
                    for (int n=0;n<12;++n) REQUIRE(world.addUnit(EXPLORER,team ? 20+n%3 : 27+n%3,29+n/3,team));
                    for (int y=40;y<46;++y) for (int x=home;x<home+12;++x) world.game.map.setResourceByIndex(x,y,WHEAT,1);
                    for (auto* b:owner->liveBuildings.entries()) if (b) owner->addToStaticAbilitiesLists(b);
                owner->stats.step(owner);
                }
                Research research{&world.game,{}, {}};
                if (shadow && !pipelineOnly) for (unsigned k : {1,2,4}) for (unsigned r : {1,2,4})
                    for (unsigned l : {2,3,4,8,16,32,0}) for (unsigned margin : {0,1,2})
                        research.matrix.push_back({k,r,l,margin});
                ScopedAttemptObserver observer(shadow ? Research::attempt : nullptr,&research);
                world.game.configureTurretTargeting(actualDelay,actualRefresh,actualCapacity,1,
                    actualVariant=="forecast" || actualVariant=="combined",actualVariant=="preparation" || actualVariant=="combined");
                Observation previous;
                Result result;
                for (unsigned t=0;t<1024;++t) {
                    if (shadow && !pipelineOnly) {
                        const std::array<unsigned,2> readers{0,1};
                        const auto observed=observe(world.game,world.game.captureReadBoundary(),previous.world.entities ? &previous : nullptr);
                        for (auto& c:research.matrix) for (auto& [identity,lists]:c.turrets) {
                            if (lists.window.active && !world.game.resolveBuilding({identity.first,identity.second})) { ++c.missed; lists.window.active=false; }
                        }
                        for (int team=0;team<2;++team) for (const auto* b:world.game.teams[team]->liveBuildings.entries()) {
                            if (!b || b->runtime->shootSpeed<256) continue;
                            const auto currentSource=source(*b);
                            std::array<std::vector<Entry>,3> controls;
                            for (unsigned margin=0;margin<3;++margin) controls[margin]=plan(source(*b),observed,0,margin).entries;
                            for (auto& c:research.matrix) {
                                auto& lists=c.turrets[{b->gid,b->scriptIdentity}];
                                if (lists.initialized && !sameSource(lists.source,currentSource)) {
                                    lists.published.clear(); lists.pending.clear();
                                    if (lists.window.active) { ++c.censored; lists.window.active=false; }
                                }
                                lists.source=currentSource; lists.initialized=true;
                                if (auto it=lists.pending.find(t);it!=lists.pending.end()) { lists.published=std::move(it->second); lists.pending.erase(it); }
                                if (t%c.r==unsigned(team)%c.r) {
                                    auto entries=controls[c.margin];
                                    if (c.l && entries.size()>c.l) entries.resize(c.l);
                                    lists.pending.emplace(t+c.k,std::move(entries));
                                }
                            }
                        }
                        previous=observed;
                    }
                    result.orders.push_back(glob2test::stepAI(world.game));
                    result.checksums.push_back(world.checksum());
                }
                auto* backend=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream output(backend); world.game.save(&output,false,"stock combat shadow"); output.flush();
                const auto bytes=backend->takeContents(); result.save.assign(bytes.begin(),bytes.end());
                if (shadow) {
                    REQUIRE(research.shots>0);
                    REQUIRE(research.eligible>0);
                    if (fortified) CHECK(10*research.combatFound>=research.combatEligible);
                    std::ofstream csv(glob2test::artifactDir()/(fixture+"-"+std::to_string(seed)+"-matrix.csv"));
                    csv << "fixture,seed,K,R,L,margin,age_eligible,eligible,found,combat_eligible,combat_found,shots,unit_shots,building_shots,opportunities,success,agreement,represented,missed,censored,p95_added_delay,examined,rejected,delays\n";
                    for (auto& c:research.matrix) {
                        for (auto& [identity,lists]:c.turrets) if (lists.window.active) ++c.censored;
                        std::sort(c.delays.begin(),c.delays.end());
                        const unsigned complete=c.delays.size()+c.missed;
                        const unsigned rank=complete ? (95*complete+99)/100 : 0;
                        const std::string p95=rank && rank<=c.delays.size() ? std::to_string(c.delays[rank-1]) : "missed-window";
                        csv << fixture << ',' << seed << ',' << c.k << ',' << c.r << ',' << c.l << ',' << c.margin << ',' << (c.k+c.r-1<=4)
                            << ',' << research.eligible << ',' << research.found << ',' << research.combatEligible << ',' << research.combatFound << ',' << research.shots << ',' << research.unitShots << ',' << research.buildingShots << ',' << c.opportunities << ',' << c.success << ',' << c.agreement
                            << ',' << c.represented << ',' << c.missed << ',' << c.censored << ',' << p95 << ',' << c.metrics.examined << ',' << c.metrics.rejected << ',';
                        for (unsigned delay:c.delays) csv << delay << ';';
                        csv << '\n';
                    }
                    csv.flush(); REQUIRE(csv.good());
                    std::ofstream actual(glob2test::artifactDir()/(fixture+"-"+std::to_string(seed)+"-producer.csv"));
                    for (const auto& [identity,window]:research.actual.windows) if (window.active) ++research.actual.censored;
                    actual << "fixture,seed,L,variant,opportunities,agreement,missed,censored,delays,K,R\n";
                    actual << fixture << ',' << seed << ',' << actualCapacity << ',' << actualVariant << ',' << research.actual.opportunities << ',' << research.actual.agreement << ',' << research.actual.missed << ',' << research.actual.censored << ',';
                    for (unsigned delay:research.actual.delays) actual << delay << ';';
                    actual << ',' << actualDelay << ',' << actualRefresh << '\n'; actual.flush(); REQUIRE(actual.good());
                    std::printf("stock_combat,fixture=%s,seed=%u,eligible=%u,found=%u,shots=%u,unit_shots=%u,building_shots=%u\n",fixture.c_str(),seed,research.eligible,research.found,research.shots,research.unitShots,research.buildingShots);
                }
                const auto prefix=fixture+"-"+std::to_string(seed)+(shadow ? "-shadow" : "-reference");
                std::ofstream save(glob2test::artifactDir()/(prefix+".game"),std::ios::binary); save.write(result.save.data(),result.save.size());
                save.flush(); REQUIRE(save.good());
                std::ofstream trace(glob2test::artifactDir()/(prefix+"-checksums.csv"));
                for (unsigned t=0;t<result.checksums.size();++t) trace << t+1 << ',' << result.checksums[t] << '\n';
                trace.flush(); REQUIRE(trace.good());
                std::ofstream orders(glob2test::artifactDir()/(prefix+"-orders.csv"));
                orders << "tick,player,order_hex\n";
                constexpr char digits[]="0123456789abcdef";
                for (unsigned t=0;t<result.orders.size();++t) for (unsigned player=0;player<result.orders[t].size();++player) {
                    orders << t << ',' << player << ',';
                    for (unsigned char byte:result.orders[t][player]) orders << digits[byte>>4] << digits[byte&15];
                    orders << '\n';
                }
                orders.flush(); REQUIRE(orders.good());
                return result;
            };
            const auto reference=run(false),shadow=run(true);
            CHECK(reference.orders==shadow.orders); CHECK(reference.checksums==shadow.checksums); CHECK(reference.save==shadow.save);
        }
    }
    TEST_CASE("stock birth and approaching warrior opportunities cover admission phases [artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        std::ofstream csv(glob2test::artifactDir()/"stock-opportunities.csv");
        csv << "scenario,phase,K,R,L,margin,age_eligible,reference_first,cached_first,added_delay,first_window_missed\n";
        for (bool approaching : {false,true}) for (unsigned phase=0;phase<8;++phase) {
            glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true,.seed=713+phase});
            auto* turret=world.addBuilding("defencetower",8,8);
            turret->owner->addToStaticAbilitiesLists(turret);
            turret->owner->enemies=2; turret->bullets=turret->type->maxBullets; turret->shootingCooldown=0;
            struct Configuration { unsigned k,r,l,margin,first=UINT_MAX; std::vector<Entry> published; std::map<unsigned,std::vector<Entry>> pending; };
            std::vector<Configuration> matrix;
            for (unsigned k : {1,2,4}) for (unsigned r : {1,2,4}) for (unsigned l : {2,3,4,8,16,32,0}) for (unsigned margin : {0,1,2}) matrix.push_back({k,r,l,margin});
            unsigned referenceFirst=UINT_MAX,windowEnd=UINT_MAX; Entry windowTarget;
            Unit* enemy=nullptr;
            Observation previous;
            for (unsigned t=0;t<128;++t) {
                if (t==8+phase) enemy=world.addUnit(WARRIOR,approaching ? 15 : 11,8,1);
                const auto observed=observe(world.game,previous.world.entities ? &previous : nullptr);
                const auto reference=turret->targetingReference();
                if (referenceFirst==UINT_MAX && turret->shootingCooldown<=0 && turret->bullets>0 && reference.found()
                    && turret->computeFiringSolution(reference.x,reference.y).ticksLeft<reference.ticks) { referenceFirst=t; windowTarget=plan(*turret,0,2).preferred.entry; }
                if (referenceFirst!=UINT_MAX && t>referenceFirst && windowEnd==UINT_MAX && !firstValid(*turret,{windowTarget}).found()) windowEnd=t;
                std::array<std::vector<Entry>,3> controls;
                for (unsigned margin=0;margin<3;++margin) controls[margin]=plan(source(*turret),observed,0,margin).entries;
                for (auto& c:matrix) {
                    if (auto it=c.pending.find(t);it!=c.pending.end()) { c.published=std::move(it->second); c.pending.erase(it); }
                    if (t%c.r==0) { auto list=controls[c.margin]; if (c.l && list.size()>c.l) list.resize(c.l); c.pending.emplace(t+c.k,std::move(list)); }
                    if (referenceFirst!=UINT_MAX && c.first==UINT_MAX && firstValid(*turret,c.published).found()) c.first=t;
                }
                world.step();
                previous=observed;
            }
            REQUIRE(enemy); if (!approaching) REQUIRE(referenceFirst!=UINT_MAX);
            for (const auto& c:matrix) {
                if (referenceFirst!=UINT_MAX && c.k==1 && c.r==1) { CHECK(c.first!=UINT_MAX); CHECK(c.first-referenceFirst<=4); CHECK(c.first<windowEnd); }
                csv << (approaching ? "approaching" : "birth") << ',' << phase << ',' << c.k << ',' << c.r << ',' << c.l << ',' << c.margin << ',' << (c.k+c.r-1<=4)
                    << ',' << referenceFirst << ',' << c.first << ',';
                if (referenceFirst==UINT_MAX) csv << "no-reference"; else if (c.first==UINT_MAX) csv << "missed-window"; else csv << c.first-referenceFirst;
                csv << ',' << (referenceFirst!=UINT_MAX && (c.first==UINT_MAX || (windowEnd!=UINT_MAX && c.first>=windowEnd)));
                csv << '\n';
            }
        }
        csv.flush(); REQUIRE(csv.good());
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
    TEST_CASE("indexed planning callgrind regions [benchmark][callgrind]")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned fixture=0;fixture<4;++fixture) {
            const bool dense=fixture==2;
            glob2test::HeadlessGame world({.wDec=dense ? 5 : 7,.hDec=dense ? 5 : 7,.teams=2,.loadDefaultRace=true,.header=true});
            std::vector<Source> sources;
            if (dense) {
                auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
                const auto id=world.game.buildingsTypes.getTypeNum("defencetower",0,false);
                catalog["variants"][id]["properties"]["width"]=3;
                catalog["variants"][id]["properties"]["height"]=1;
                catalog["variants"][id]["properties"]["shootingRange"]=20;
                world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
                auto* b=world.addBuilding("defencetower",30,30); b->owner->enemies=2;
                sources.push_back(source(*b));
                for (int n=0;n<60;++n) { auto* u=world.addUnit(WARRIOR,4+n%12,4+n/12,1); u->speed=0; }
            } else for (unsigned n=0;n<100;++n) {
                auto* b=world.addBuilding("defencetower",4+n%10*10,4+n/10*10); b->owner->enemies=2;
                sources.push_back(source(*b));
                if (fixture) { auto* u=world.addUnit(WARRIOR,b->posX+3,b->posY,1); u->speed=0; }
            }
            auto observed=observe(world.game);
            if (fixture==3) observed.index.reset(); // Same algorithm, no retained chunks.
            for (const auto& source:sources) (void)plan(source,observed,8,2);
            const char* label=fixture==0 ? "empty" : fixture==1 ? "combat" : fixture==2 ? "custom-wrap" : "temporary";
            unsigned success=0,queries=0;
            CALLGRIND_START_INSTRUMENTATION;
            CALLGRIND_ZERO_STATS;
            CALLGRIND_TOGGLE_COLLECT;
            for (unsigned repeat=0;repeat<(dense ? 2000u : 20u);++repeat) for (const auto& source:sources) {
                const auto result=plan(source,observed,8,2);
                success+=result.preferred.found(); ++queries;
            }
            CALLGRIND_TOGGLE_COLLECT;
            CALLGRIND_DUMP_STATS_AT(label);
            CALLGRIND_STOP_INSTRUMENTATION;
            REQUIRE(queries==2000);
            CHECK(success==(fixture ? queries : 0));
            std::printf("turret_callgrind,%s,queries=%u,success=%u\n",label,queries,success);
        }
    }
    TEST_CASE("paired targeting batches profile empty and sustained combat [benchmark]")
    {
        glob2test::HeadlessGlobals globals;
        using Clock=std::chrono::steady_clock;
        std::printf("turret_cost,fixture,repeat,path,ns,searches,successes,visits,occupied,clearance,scored,index_bytes\n");
        for (bool combat : {false,true}) {
            glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=2,.loadDefaultRace=true,.header=true});
            std::vector<Building*> turrets;
            for (int i=0;i<100;++i) {
                auto* b=world.addBuilding("defencetower",4+(i%10)*10,4+(i/10)*10);
                REQUIRE(b); b->bullets=10; b->owner->enemies=2; turrets.push_back(b);
                if (combat) {
                    auto* u=world.addUnit(WARRIOR,b->posX+2,b->posY,1);
                    REQUIRE(u); u->speed=0;
                }
            }
            const auto observed=observe(world.game);
            std::vector<Source> sources;
            std::vector<std::vector<Entry>> lists;
            for (auto* b:turrets) { sources.push_back(source(*b)); lists.push_back(plan(*b,8,2).entries); }
            // One untimed warmup, followed by ten paired batches. Rotate all
            // four paths each repetition; retain every sample, including outliers.
            for (unsigned repeat=0;repeat<11;++repeat) for (unsigned j=0;j<4;++j) {
                const auto path=(j+repeat)%4;
                Metrics metrics;
                unsigned success=0;
                const auto start=Clock::now();
                for (int batch=0;batch<16;++batch) for (unsigned n=0;n<turrets.size();++n) {
                    if (path==0) success+=turrets[n]->targetingReference().found();
                    else if (path==3) success+=firstValid(*turrets[n],lists[n]).found();
                    else {
                        auto p=path==1 ? plan(*turrets[n],8,2) : plan(sources[n],observed,8,2);
                        success+=p.preferred.found();
                        metrics.visits+=p.metrics.visits; metrics.occupied+=p.metrics.occupied;
                        metrics.clearance+=p.metrics.clearance; metrics.scored+=p.metrics.scored;
                    }
                }
                const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
                if (repeat) std::printf("turret_cost,%s,%u,%s,%lld,1600,%u,%llu,%llu,%llu,%llu,%zu\n",
                    combat ? "combat" : "empty",repeat,
                    path==0 ? "original" : path==1 ? "temporary_index" : path==2 ? "retained_index" : "consumer",
                    static_cast<long long>(duration),success,
                    static_cast<unsigned long long>(metrics.visits),static_cast<unsigned long long>(metrics.occupied),
                    static_cast<unsigned long long>(metrics.clearance),static_cast<unsigned long long>(metrics.scored),retainedIndexBytes(observed));
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
            const auto optimized=observe(world.game,original.world,nullptr,true);
            CHECK(original.offense==optimized.offense);
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
                world.game.configureTurretTargeting(delay,refresh,8,1,true,true);
                const bool admitted=readyOffset<=delay+refresh-1;
                auto* pipeline=world.game.turretTargeting();
                CHECK((pipeline->pendingRequirements(world.game,false)!=0)==admitted);
                world.game.prepareReadBoundary(false);
                pipeline->drain();
                CHECK(pipeline->statistics().sources==unsigned(admitted));
                CHECK(pipeline->statistics().observations==unsigned(admitted));
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
        world.game.configureTurretTargeting(1,1,8,1,true,true);
        auto* pipeline=world.game.turretTargeting();
        world.game.prepareReadBoundary(false);
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
            auto* enemy=world.addUnit(WARRIOR,11,8,1); enemy->speed=0;
            world.game.configureTurretTargeting(delay,1,8,1,false,false);
            auto* pipeline=world.game.turretTargeting();
            const auto before=world.game.snapshots().metrics.captures;
            const auto shared=world.game.prepareReadBoundary(false);
            const auto job=pipeline->pendingObservation();
            CHECK(world.game.snapshots().metrics.captures==before+1);
            CHECK(job.entities==shared.entities); CHECK(job.occupancy==shared.occupancy);
            CHECK(job.terrain==shared.terrain); CHECK(job.catalogs==shared.catalogs);
            CHECK(job.teams==shared.teams); CHECK(job.effects==shared.effects);
            const auto aiReader=world.game.prepareReadBoundary(false);
            CHECK(aiReader.entities==shared.entities);
            world.game.prepareAIOrders({},true,{},&shared);
            CHECK(world.game.snapshots().metrics.captures==before+1);
            CHECK(pipeline->statistics().admissions==1);
            world.game.prepareReadBoundary(true);
            CHECK(pipeline->statistics().admissions==1);
            pipeline->drain();
            CHECK(pipeline->entries(*turret).empty()); // Save/pause drain never publishes early.
            CHECK(pipeline->pendingObservation().requirements==0); // Completed outputs release leases.
            for (unsigned t=1;t<=delay;++t) {
                world.game.stepCounter=t;
                const auto before=world.game.snapshots().metrics.captures;
                world.game.prepareReadBoundary(false);
                CHECK(world.game.snapshots().metrics.captures==before+1);
                pipeline->publish(world.game);
                if (t<delay) CHECK(pipeline->entries(*turret).empty());
            }
            REQUIRE(!pipeline->entries(*turret).empty());
            CHECK(firstValid(*turret,pipeline->entries(*turret)).entry==plan(*turret,8,1).preferred.entry);
            CHECK(pipeline->statistics().pendingPeak<=delay+1);
            CHECK(pipeline->statistics().shortlistBytes<=32u*1024u*1024u);
            CHECK(pipeline->statistics().indexBytes<=32u*1024u*1024u);
            ++turret->posX;
            CHECK(pipeline->entries(*turret).empty());
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
        world.game.prepareReadBoundary(false);
        auto* changed=turrets[7]; const auto* original=changed->runtime;
        auto upgraded=*original; ++upgraded.shootingRange; changed->runtime=&upgraded;
        for (unsigned tick=1;tick<=5;++tick) {
            world.game.stepCounter=tick;
            world.game.prepareReadBoundary(false);
            pipeline->publish(world.game);
            if (tick==4) {
                CHECK(pipeline->entries(*changed).empty()); // The old source output is incompatible.
                for (auto* turret:turrets) if (turret!=changed) CHECK_FALSE(pipeline->entries(*turret).empty());
            }
        }
        for (auto* turret:turrets) CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
        const auto incarnation=changed->scriptIdentity;
        changed->scriptIdentity+=100000;
        CHECK(pipeline->entries(*changed).empty());
        pipeline->publish(world.game); // Remove the old incarnation in the middle of the table.
        for (auto* turret:turrets) if (turret!=changed) CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
        world.game.stepCounter=6;
        world.game.prepareReadBoundary(false);
        pipeline->drain();
        world.game.stepCounter=10;
        pipeline->publish(world.game);
        CHECK(pipeline->entries(*changed)==plan(*changed,8,1).entries);
        CHECK(pipeline->statistics().shortlistBytes<=32u*1024u*1024u);
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
                auto* target=world.addUnit(WARRIOR,turret->posX+3,8,1-team); REQUIRE(target); target->speed=0;
            }
            world.game.configureTurretTargeting(4,1,8,1,false,true);
            auto* pipeline=world.game.turretTargeting();
            std::size_t chunks=0;
            for (unsigned tick=0;tick<4;++tick) {
                world.game.stepCounter=tick;
                const auto captured=world.game.prepareReadBoundary(false);
                chunks=captured.occupancy->stamps.chunks.size();
            }
            pipeline->drain();
            CHECK(pipeline->statistics().teamJobs==8);
            CHECK(pipeline->statistics().rebuiltChunks==chunks);
            CHECK(pipeline->statistics().sharedChunks==3*chunks);
            CHECK(pipeline->pendingObservation().requirements==0);
            world.game.stepCounter=7;
            pipeline->publish(world.game);
            for (int team=0;team<2;++team) for (auto* turret:world.game.teams[team]->turrets)
                CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
        }
    }
    TEST_CASE("published slot reuse stays bounded without a per-tick cleanup scan")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1,4}) {
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
            world.game.map.computeExecutor().configure(participants);
            auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
            turret->owner->addToStaticAbilitiesLists(turret);
            auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
            world.game.configureTurretTargeting(1,1,8,1,false,true);
            auto* pipeline=world.game.turretTargeting();
            const auto identity=turret->scriptIdentity;
            std::size_t initialBytes=0;
            for (unsigned reuse=0;reuse<100;++reuse) {
                turret->scriptIdentity=identity+reuse;
                if (reuse) CHECK(pipeline->entries(*turret).empty());
                world.game.prepareReadBoundary(false);
                pipeline->drain();
                ++world.game.stepCounter;
                pipeline->publish(world.game);
                CHECK(pipeline->entries(*turret)==plan(*turret,8,1).entries);
                if (!reuse) initialBytes=pipeline->statistics().shortlistBytes;
                CHECK(pipeline->statistics().shortlistBytes==initialBytes);
                CHECK(pipeline->statistics().indexBytes<=32u*1024u*1024u);
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
        world.game.configureTurretTargeting(1,2,8,1,false,false);
        auto* pipeline=world.game.turretTargeting();
        CHECK(pipeline->pendingRequirements(world.game,false)==0); // Only team one has turrets.
        auto shared=world.game.prepareReadBoundary(false);
        CHECK(shared.entities); CHECK(pipeline->statistics().observations==0);
        world.game.stepCounter=1;
        CHECK(pipeline->pendingRequirements(world.game,true)==0);
        world.game.prepareReadBoundary(true);
        CHECK(pipeline->statistics().admissions==0);
        CHECK(pipeline->pendingRequirements(world.game,false)==requirements);
        world.game.prepareReadBoundary(false);
        CHECK(pipeline->statistics().admissions==1);
        world.game.snapshots().invalidateBoundary(); // A paused edit does not readmit this logical tick.
        world.game.prepareReadBoundary(false);
        CHECK(pipeline->statistics().admissions==1);
        world.game.stepCounter=2;
        pipeline->drain(); CHECK(pipeline->statistics().publications==0);
        pipeline->publish(world.game); CHECK(pipeline->statistics().publications==1);
        CHECK_THROWS_AS(world.game.configureTurretTargeting(4,2,8,1,false,false),std::invalid_argument);
    }
    TEST_CASE("engine-style stepping reuses admitted boundary after orders mutate occupancy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.wDec=6,.hDec=6,.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
        turret->owner->addToStaticAbilitiesLists(turret);
        world.game.configureTurretTargeting(1,1,8,1,false,false);
        const auto shared=world.game.prepareReadBoundary(false);
        world.game.map.preparePendingWorld(shared);
        const auto captures=world.game.snapshots().metrics.captures;
        REQUIRE(world.addUnit(WARRIOR,11,8,1)); // Represents an order after completed capture.
        world.game.setWaitingOnMask(0);
        world.game.syncStep(0,Game::PreparationCompletion::Deferred);
        CHECK(world.game.snapshots().metrics.captures==captures+1); // Standard view reflects the order; no turret readmission.
        CHECK(world.game.turretTargeting()->statistics().admissions==1);
        CHECK(world.game.turretTargeting()->statistics().publications==0);
        world.game.prepareReadBoundary(false);
        CHECK(world.game.snapshots().metrics.captures==captures+2);
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
            world.game.configureTurretTargeting(1,1,8,1,false,false);
            turret->posX=-1; // Corrupt source geometry forces the immutable worker to fail.
            world.game.prepareReadBoundary(false);
            auto* pipeline=world.game.turretTargeting();
            CHECK_NOTHROW(pipeline->drain());
            CHECK(pipeline->statistics().publications==0);
            CHECK(pipeline->statistics().failedGenerations==0);
            turret->posX=8; world.game.stepCounter=1;
            CHECK_THROWS_AS(pipeline->publish(world.game),std::invalid_argument);
            CHECK(pipeline->statistics().failedGenerations==1);
            // Failed outputs/reservations are gone; only the conservatively
            // charged logical index-preparation descriptor remains cached.
            CHECK(pipeline->statistics().shortlistBytes==256);
            CHECK(pipeline->entries(*turret).empty());
        }
    }
    TEST_CASE("paired shared targeting cycles measure incremental union capture and team jobs [benchmark][pipeline-cost]")
    {
        glob2test::HeadlessGlobals globals;
        using Clock=std::chrono::steady_clock;
        const auto elapsed=[](auto start) { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(); };
        std::printf("turret_shared,fixture,repeat,path,cycles,queries,successes,total_ns,cpu_ns,capture_submit_ns,publish_ns,consume_ns,captures,bytes_copied,index_ns,index_bytes,shortlist_bytes\n");
        for (bool combat : {false,true}) {
            glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=8,.loadDefaultRace=true,.header=true});
            std::vector<Building*> turrets;
            for (unsigned n=0;n<100;++n) {
                const unsigned team=n%8;
                auto* b=world.addBuilding("defencetower",4+(n%10)*10,4+(n/10)*10,0,team);
                REQUIRE(b); b->bullets=10; b->owner->enemies=0xffu & ~(1u<<team);
                b->owner->addToStaticAbilitiesLists(b); turrets.push_back(b);
                if (combat) { auto* u=world.addUnit(WARRIOR,b->posX+3,b->posY,(team+1)%8); REQUIRE(u); u->speed=0; }
            }
            // Castor's existing core observation requirements. Every measured
            // path captures this reader's data, so turret capture is incremental.
            constexpr auto existing=SimulationSnapshot::Simulation;
            for (unsigned repeat=0;repeat<11;++repeat) for (unsigned order=0;order<5;++order) {
                const unsigned path=(order+repeat)%5;
                world.game.clearAI(); // Outside timing; drains and clears trial state.
                world.game.map.computeExecutor().configure(path ? 1u<<(path-1) : 1u);
                if (path) world.game.configureTurretTargeting(1,1,8,1,false,false);
                auto* pipeline=world.game.turretTargeting();
                auto initial=world.game.prepareReadBoundary(false);
                if (pipeline) pipeline->drain();
                initial={};
                const auto captureBefore=world.game.snapshots().metrics;
                const auto indexBefore=pipeline ? pipeline->statistics().indexNs : 0;
                Uint64 captureNs=0,publishNs=0,consumeNs=0;
                unsigned success=0;
                if (repeat==1) { CALLGRIND_START_INSTRUMENTATION; CALLGRIND_ZERO_STATS; CALLGRIND_TOGGLE_COLLECT; }
                const auto cpuStart=std::clock(); const auto start=Clock::now();
                for (unsigned cycle=0;cycle<16;++cycle) {
                    ++world.game.stepCounter;
                    auto phase=Clock::now();
                    const auto shared=world.game.prepareReadBoundary(false);
                    captureNs+=elapsed(phase);
                    phase=Clock::now(); if (pipeline) pipeline->publish(world.game);
                    publishNs+=elapsed(phase);
                    phase=Clock::now();
                    for (const auto* b:turrets) success+=pipeline ? firstValid(*b,pipeline->entries(*b)).found() : b->targetingReference().found();
                    consumeNs+=elapsed(phase);
                }
                if (pipeline) pipeline->drain(); // Include completion cost, preserve future deadline.
                const auto totalNs=elapsed(start);
                const auto cpuNs=Uint64(double(std::clock()-cpuStart)*1e9/CLOCKS_PER_SEC);
                if (repeat==1) {
                    const auto label=std::string(combat ? "shared-combat-" : "shared-empty-")+(path ? std::to_string(1u<<(path-1)) : "original");
                    CALLGRIND_TOGGLE_COLLECT; CALLGRIND_DUMP_STATS_AT(label.c_str()); CALLGRIND_STOP_INSTRUMENTATION;
                }
                const auto captured=world.game.snapshots().metrics.captures-captureBefore.captures;
                const auto copied=world.game.snapshots().metrics.bytesCopied-captureBefore.bytesCopied;
                CHECK(captured==16); CHECK(success==(combat ? 1600u : 0u));
                if (repeat) {
                    const auto name=path ? "pool_"+std::to_string(1u<<(path-1)) : "original";
                    std::printf("turret_shared,%s,%u,%s,16,1600,%u,%lld,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%zu\n",
                        combat ? "combat" : "empty",repeat,name.c_str(),success,static_cast<long long>(totalNs),
                        static_cast<unsigned long long>(cpuNs),static_cast<unsigned long long>(captureNs),static_cast<unsigned long long>(publishNs),
                        static_cast<unsigned long long>(consumeNs),static_cast<unsigned long long>(captured),static_cast<unsigned long long>(copied),
                        static_cast<unsigned long long>(pipeline ? pipeline->statistics().indexNs-indexBefore : 0),
                        pipeline ? pipeline->statistics().indexBytes : 0,pipeline ? pipeline->statistics().shortlistBytes : 0);
                }
            }
        }
    }
    TEST_CASE("serial shadow matrix exposes one tick newly created firing windows [artifacts]")
    {
        // Controlled custom-footprint birth case: a new fast airborne unit is
        // born above the turret. Zero-tick arrival is legal in the original
        // routine; at delta=150 its tile residence truncates to zero and the
        // opportunity ends. Prior observations cannot contain a new incarnation.
        // Delta, occupancy and births are prescribed fixture inputs; this is
        // a targeting opportunity test, not a complete battle simulation.
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const auto id=world.game.buildingsTypes.getTypeNum("defencetower",0,false);
        catalog["variants"][id]["properties"]["width"]=1;
        catalog["variants"][id]["properties"]["height"]=1;
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
        auto* turret=world.addBuilding("defencetower",8,8);
        REQUIRE(turret); turret->shootingCooldown=0; turret->owner->enemies=2;
        turret->owner->addToStaticAbilitiesLists(turret);
        Unit* enemy=nullptr;
        unsigned births=0;
        struct Configuration {
            unsigned k,r,l,margin,windows=0,missed=0;
            std::vector<Entry> published;
            std::map<unsigned,std::vector<Entry>> pending;
        };
        std::vector<Configuration> matrix;
        for (unsigned k : {1,2,4}) for (unsigned r : {1,2,4})
            for (unsigned l : {2,3,4,8,16,32,0}) for (unsigned margin : {0,1,2})
                matrix.push_back({k,r,l,margin});
        bool wasAvailable=false;
        std::vector<bool> hit(matrix.size(),false);
        Observation previous;
        for (unsigned t=0;t<1024;++t) {
            const unsigned phase=t%64;
            if (phase==0) turret->bullets=10;
            // Every configuration observes the same birth and movement inputs.
            if (phase==8) {
                enemy=world.addUnit(EXPLORER,8,8,1,3);
                REQUIRE(enemy); enemy->performance[FLY]=150; enemy->speed=150; enemy->delta=0;
                world.game.map.setGroundUnit(8,8,NOGUID);
                world.game.map.setAirUnit(8,8,enemy->gid);
                ++births;
            } else if (phase==9) {
                REQUIRE(enemy); enemy->delta=150;
            } else if (phase==10) {
                // Retire this controlled opportunity outside range and guard.
                world.game.map.setAirUnit(8,8,NOGUID);
                enemy->posX=20+int((births-1)%8); enemy->posY=20+int((births-1)/8);
                world.game.map.setAirUnit(enemy->posX,enemy->posY,enemy->gid);
            }
            world.game.stepCounter=t;
            const auto observed=observe(world.game,previous.world.entities ? &previous : nullptr);
            const auto reference=turret->targetingReference();
            const auto actual=turret->computeFiringSolution(reference.x,reference.y);
            const bool available=turret->shootingCooldown<=0 && turret->bullets>0 && reference.found() && actual.ticksLeft<reference.ticks;
            if (available) {
                // Same-observation control succeeds; loss comes solely from age.
                const auto control=plan(source(*turret),observed,0,2);
                REQUIRE(firstValid(*turret,control.entries).found());
            }
            for (std::size_t n=0;n<matrix.size();++n) {
                auto& c=matrix[n];
                if (auto it=c.pending.find(t);it!=c.pending.end()) { c.published=std::move(it->second); c.pending.erase(it); }
                if (t%c.r==unsigned(turret->owner->teamNumber)%c.r)
                    c.pending.emplace(t+c.k,plan(source(*turret),observed,c.l,c.margin).entries);
                const auto consumer=firstValid(*turret,c.published);
                if (available && !wasAvailable) { ++c.windows; hit[n]=false; }
                if (available && consumer.found()) hit[n]=true;
                if (!available && wasAvailable && !hit[n]) ++c.missed;
            }
            const int ammunition=turret->bullets;
            world.game.prepareReadBoundary(false); world.game.turretTargeting()->publish(world.game);
            turret->turretStep(t); // Delayed lists miss these one-tick windows.
            CHECK(turret->bullets==ammunition);
            wasAvailable=available;
            previous=observed;
        }
        std::printf("turret_shadow,K,R,L,margin,trial_age_eligible,windows,missed,p95_added_delay,gate_pass\n");
        for (const auto& c:matrix) {
            CHECK(c.windows==16);
            CHECK(c.missed==c.windows);
            std::printf("turret_shadow,%u,%u,%u,%u,%d,%u,%u,missed-window,0\n",c.k,c.r,c.l,c.margin,c.k+c.r-1<=4,c.windows,c.missed);
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
                game.prepareReadBoundary(false); game.turretTargeting()->publish(game);
                const auto checksum=game.turretTargeting()->checkSum(game);
                const auto entries=game.turretTargeting()->entries(*turret);
                auto* backend=new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream out(backend); game.turretTargeting()->save(game,&out); out.flush();
                const auto bytes=backend->takeContents();
                CHECK(game.turretTargeting()->checkSum(game)==checksum); // Draining is not publication.
                game.configureTurretTargeting(delay,refresh,3,1);
                GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
                in.seekFromStart(0);
                REQUIRE(game.turretTargeting()->load(game,&in));
                CHECK(game.turretTargeting()->checkSum(game)==checksum);
                CHECK(game.turretTargeting()->entries(*turret)==entries);
                CHECK_FALSE(game.turretTargeting()->admissionPending(game,false));
                CHECK(game.turretTargeting()->statistics().shortlistBytes<=32u*1024u*1024u);
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
    TEST_CASE("new cached saves roundtrip and earlier saved matches are rejected")
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
        GAGCore::BinaryOutputStream patch(patchBackend); patch.writeSint32(SAVE_MINIMUM_VERSION_MINOR-1,"versionMinor"); patch.flush();
        const auto encoded=patchBackend->takeContents(); auto older=bytes;
        std::copy(encoded.begin(),encoded.end(),older.begin()+minorOffset);
        GAGCore::BinaryInputStream rejected(new GAGCore::MemoryStreamBackend(older.data(),older.size()));
        rejected.seekFromStart(0);
        CHECK_FALSE(resumed.game.load(&rejected));
        CHECK(MINIMUM_VERSION_MINOR==58); // Existing maps keep their separate floor.
    }
    TEST_CASE("canonical pending inputs ignore stamps and retain observed combat facts")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
        auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
        const auto first=observe(world.game);
        world.game.map.markAllChanges();
        const auto stamped=observe(world.game,&first);
        CHECK(stamped.logicalDigest==first.logicalDigest);
        --target->hp;
        const auto changed=observe(world.game,&stamped);
        CHECK(changed.logicalDigest!=stamped.logicalDigest);
        ++target->hp;
        const auto restored=observe(world.game,&changed);
        CHECK(restored.logicalDigest==first.logicalDigest);
    }
    TEST_CASE("weapon catalog changes supersede lists even after reverting the weapon")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
        auto* turret=world.addBuilding("defencetower",8,8); turret->owner->enemies=2;
        turret->owner->addToStaticAbilitiesLists(turret); turret->shootingCooldown=0;
        auto* target=world.addUnit(WARRIOR,11,8,1); REQUIRE(target); target->speed=0;
        auto& game=world.game; game.prepareReadBoundary(false); game.stepCounter=1;
        game.turretTargeting()->publish(game); REQUIRE(!game.turretTargeting()->entries(*turret).empty());
        const auto* original=turret->runtime; auto altered=*original; ++altered.shootingRange;
        const auto epoch=turret->targetingEpoch;
        turret->runtime=&altered; game.turretTargeting()->invalidateChangedSources(game);
        CHECK(turret->targetingEpoch!=epoch);
        turret->runtime=original;
        CHECK(game.turretTargeting()->entries(*turret).empty());
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
        CHECK(retainedIndexBytes(third)<=32u*1024u*1024u);
        compare(*turret,third);
        CHECK(plan(source(*turret),first,4).entries.empty());
        CHECK_FALSE(plan(source(*turret),third,4).entries.empty());
        auto* target=world.game.teams[1]->liveUnits.entries().front();
        REQUIRE(target);
        target->hp=1; target->experienceLevel=7;
        const auto fourth=observe(world.game,&third);
        CHECK(fourth.index==third.index);
        CHECK(retainedIndexBytes(fourth)==retainedIndexBytes(third));
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
