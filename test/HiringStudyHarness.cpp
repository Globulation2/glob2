// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Game.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "HiringPlanner.h"
#include "HiringDiagnostics.h"
#include "ComputeExecutor.h"
#include <chrono>
#include <ctime>
#include <set>
#include <span>
#include <map>
#include <BinaryStream.h>
#include <FileManager.h>
#include <GzipUtil.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
namespace fs = std::filesystem;

static void save(GameGUI& gui, const fs::path& path)
{
    auto* memory = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    gui.save(&output, "Hiring study");
    std::string compressed;
    if (!GAGCore::gzipCompress(memory->takeContents(), 6, compressed)) throw std::runtime_error("Cannot compress fixture");
    std::ofstream file(path, std::ios::binary);
    file.write(compressed.data(), compressed.size());
    if (!file) throw std::runtime_error("Cannot write fixture");
}

int main(int argc, char** argv)
try
{
    if (argc != 4 || std::string(argv[1]) != "--prepare")
        throw std::runtime_error("Usage: HiringStudyHarness --prepare sustainable|contention|opportunities OUTPUT_DIR");
    const std::string kind = argv[2];
    if (kind != "sustainable" && kind != "contention" && kind != "opportunities") throw std::invalid_argument("Unknown fixture kind");
    const fs::path directory = argv[3];
    if (fs::exists(directory)) throw std::runtime_error("Output directory already exists");
    fs::create_directories(directory);
    GlobalContainer globals; globalContainer = &globals;
    globals.runNoX = true; globals.settings.rememberUnit = false;
    globals.buildingsTypes.init(); IntBuildingType::init(); Race::loadDefault();
    GameGUI gui(false); auto& game = gui.game;
    game.map.setSize(7, 7, GRASS); game.map.setGame(&game);
    for (int t=0; t<2; ++t) { game.addTeam(t); game.teams[t]->race.loadDefault(); }
    GameHeader header; header.setNumberOfPlayers(2); header.setRandomSeed(713);
    // Both slots must supply lockstep orders when the save is replayed through
    // the real headless engine. Inactive AI supplies NullOrder without changing
    // demand; two local human slots would wait forever for the second player.
    for (int t=0; t<2; ++t) header.getBasePlayer(t)=BasePlayer(t, "hiring study", t, BasePlayer::playerTypeFromImplementationID(AI::NONE));
    game.setGameHeader(header, true); game.map.setMapDiscovered(); game.setWaitingOnMask(0);
    const int innType = game.buildingsTypes.getTypeNum("inn", 0, false);
    for (int t=0; t<2; ++t)
    {
        game.teams[t]->startPosX=t*64+4; game.teams[t]->startPosY=4;
        for (int n=0; n<24; ++n)
        {
            auto* b=game.addBuilding(t*64+3+(n%8)*7, 40+(n/8)*9, innType, t, 4, 4);
            if (!b) throw std::runtime_error("Cannot place service building");
            b->materials[WHEAT]=b->type->maxMaterial[WHEAT]/2;
            b->priority=kind=="contention" ? (n%3)-1 : 0;
            b->updateCallLists();
        }
        for (int y=85; y<120; ++y) for (int x=t*64+1; x<t*64+60; x+=3)
            for (int amount=0; amount<20; ++amount) game.map.incResourceByIndex(x,y,WHEAT,0);
        for (int n=0; n<600; ++n)
        {
            auto* u=game.addUnit(t*64+1+n%60, 4+n/60, t, WORKER, 0, n%256, 0, 0);
            if (!u) throw std::runtime_error("Cannot place worker");
            // Stagger early medical transitions so services and deliveries are
            // active during the retained window, using the stock hunger rate.
            u->hungry=u->trigHungry+u->hungriness*(16+n%128);
        }
        game.teams[t]->createLists();
    }
    if(kind=="opportunities") {
        // Isolated real-engine hiring opportunities. No simulation ticks run:
        // stock/positions remain fixed, and subscription resets are untimed.
        nlohmann::json rows=nlohmann::json::array();
        ComputeExecutor executor;executor.configure(4);
        const auto reset=[&](unsigned availableStride) {
            for(int t=0;t<2;++t) {
                for(auto* u:game.teams[t]->liveUnits.entries()) {
                    if(u->attachedBuilding)u->attachedBuilding->removeUnitFromWorking(u);
                    u->standardRandomActivity();u->carriedMaterial=-1;
                    u->hungry=u->trigHungry+u->hungriness*4096;
                    u->medical=Unit::GIDtoID(u->gid)%availableStride?Unit::MED_HUNGRY:Unit::MED_FREE;
                }
                for(auto* b:std::span(game.teams[t]->myBuildings,Building::MAX_COUNT))if(b){b->materials[WHEAT]=0;b->updateCallLists();}
            }
        };
        reset(1);
        for(int t=0;t<2;++t)for(auto* b:std::span(game.teams[t]->myBuildings,Building::MAX_COUNT))if(b)
            for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)game.map.buildingGradient(b,swim);
        for(int t=0;t<2;++t)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)game.map.getMaterialGradientSlot(t,WHEAT,swim);
        for(unsigned stride:{1u,4u,8u}) {
            reset(stride);game.snapshotStore().invalidateBoundary();
            auto snapshot=game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
            auto inputs=HiringPlanner::capture(game,snapshot);
            for(int repetition=-1;repetition<10;++repetition)
            for(int offset=0;offset<13;++offset) {
                const int mode=(offset+repetition+14)%13-2;
                HiringPlanner::Options options;options.pruneFetch=mode<8;
                if(mode>=0){options.extras=(mode/2)*2;options.availableOnly=mode%2;}
                if(mode>=6){options.extras=(mode==7?4:2);options.availableOnly=true;options.coordinate=mode!=9;options.pruneFetch=mode<8;}
                HiringPlanner::Cache caches[2];
                {
                    std::uint64_t cpu=0,elapsed=0,filled=0,reference=0,queries=0,visits=0,busy=0,pruned=0,reused=0,retained=0,coordinated=0;
                    for(int iteration=0;iteration<20;++iteration) {
                        reset(stride);
                        const auto start=std::chrono::steady_clock::now();const auto c=std::clock();
                        if(mode==-2){for(int t=0;t<2;++t)game.teams[t]->updateAllBuildingTasks();}
                        else {
                            HiringPlanner::Output outputs[2];
                            if(mode==10) {
                                struct Work {
                                    HiringPlanner::Cache* cache;const HiringPlanner::Inputs* inputs;
                                    HiringPlanner::Options options;HiringPlanner::Output* output;unsigned team;std::exception_ptr error;
                                    static void run(void* context,std::size_t) noexcept {
                                        auto& w=*static_cast<Work*>(context);
                                        try{*w.output=w.cache->rank(*w.inputs,w.team,128,HiringPlanner::Pools::Split,w.options);}
                                        catch(...){w.error=std::current_exception();}
                                    }
                                };
                                Work work[2]={{&caches[0],&inputs,options,&outputs[0],0,{}},{&caches[1],&inputs,options,&outputs[1],1,{}}};
                                ComputeExecutor::Group groups[2]={{1,{Work::run,&work[0]},ComputeExecutor::HiringLaneBase},{1,{Work::run,&work[1]},ComputeExecutor::HiringLaneBase+1}};
                                auto batch=executor.submit(groups);executor.join(batch);
                                for(const auto& w:work)if(w.error)std::rethrow_exception(w.error);
                            }else for(int t=0;t<2;++t)outputs[t]=caches[t].rank(inputs,t,128,HiringPlanner::Pools::Split,options);
                            cpu+=std::uint64_t((std::clock()-c)*1000000000.0/CLOCKS_PER_SEC);
                            elapsed+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                            for(const auto& output:outputs) {
                                queries+=output.metrics.sharedQueries;visits+=output.candidateVisits;busy+=output.metrics.busyEvaluations;pruned+=output.metrics.distanceBoundSkips;reused+=output.metrics.distanceReuses;retained+=output.retainedBytes;coordinated+=output.metrics.coordinatedTeams;
                                std::set<std::pair<Uint16,Uint32>> used;
                                std::vector<unsigned> assigned(output.buildings.size());
                                for(int round=0;round<20;++round)for(unsigned bi=0;bi<output.buildings.size();++bi) {
                                    const auto& building=output.buildings[bi];auto* live=game.resolveBuilding(building.building);
                                    if(assigned[bi]>=unsigned(live->desiredMaxUnitWorking))continue;
                                    bool hired=false;
                                    for(const auto& job:building.jobs)if(job.key==Hiring::Key{Hiring::Job::Fetch,WHEAT}) {
                                        for(const auto* pool:{&job.combined,&job.available,&job.busy})for(const auto& candidate:*pool) {
                                            const auto id=std::pair{candidate.unit.gid,candidate.unit.generation};
                                            if(!hired&&candidate.available&&used.insert(id).second){hired=true;++assigned[bi];++filled;}
                                        }
                                    }
                                }
                            }
                            // Ground truth fills the same persistent opportunities;
                            // comparison includes normal subscription and swaps.
                            for(int t=0;t<2;++t)game.teams[t]->updateAllBuildingTasks();
                        }
                        if(mode==-2){cpu+=std::uint64_t((std::clock()-c)*1000000000.0/CLOCKS_PER_SEC);elapsed+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
                        for(int t=0;t<2;++t)for(auto* b:std::span(game.teams[t]->myBuildings,Building::MAX_COUNT))if(b)reference+=b->unitsWorking.size();
                    }
                    rows.push_back({{"availability_stride",stride},{"mode",mode},{"extras",options.extras},{"available_only",options.availableOnly},{"repetition",repetition},{"iterations",20},{"cpu_ns",cpu},{"elapsed_ns",elapsed},{"private_overlay_hires",filled},{"reference_hires",reference},{"building_queries",queries},{"candidate_visits",visits},{"busy_evaluations",busy},{"bound_skips",pruned},{"distance_reuses",reused},{"retained_bytes_sum",retained},{"coordinated_teams",coordinated}});
                }
            }
            // Controlled scheduling model: assignments remain busy, positions
            // and stock are frozen. Admissions precede same-boundary publication,
            // matching the engine's completed-world read ordering.
            nlohmann::json delays=nlohmann::json::array();
            for(int mode=0;mode<8;++mode) {
                HiringPlanner::Options option{.extras=(mode/2)*2,.availableOnly=bool(mode%2)};
                if(mode>=6){option.extras=(mode-6)*2+2;option.availableOnly=true;option.coordinate=true;}
                HiringPlanner::Cache caches[2];
                std::set<Uint16> used;
                std::map<Uint16,BuildingRef> placements;
                std::vector<unsigned> assigned(inputs.world.entities->buildings.size());
                std::map<unsigned,std::vector<HiringPlanner::Output>> pending;
                std::map<int,HiringPlanner::Output> published;
                std::vector<unsigned> waits;
                for(unsigned tick=0;tick<=40;++tick) {
                    auto observed=inputs;
                    auto entities=std::make_shared<SimulationSnapshot::Entities>(*inputs.world.entities);
                    entities->relationships.clear();
                    for(auto& u:entities->units)if(used.contains(u.gid)){u.activity=Unit::ACT_FILLING;u.attached=placements.at(u.gid);}
                    for(auto& b:entities->buildings) {
                        b.working={Uint32(entities->relationships.size()),0};
                        for(const auto& [gid,target]:placements)if(target==b.identity) {
                            auto u=std::find_if(entities->units.begin(),entities->units.end(),[&](const auto& unit){return unit.gid==gid;});
                            entities->relationships.push_back(u->identity);++b.working.count;
                        }
                    }
                    observed.world.entities=entities;observed.world.tick=tick;
                    for(auto& need:observed.supplies) {
                        for(unsigned bi=0;bi<entities->buildings.size();++bi)if(entities->buildings[bi].identity==need.building) {
                            need.seats=unsigned(entities->buildings[bi].desiredMaxUnitWorking)-assigned[bi];
                            for(auto& n:need.vacancies)n=std::min(n,need.seats);
                        }
                    }
                    for(int team=0;team<2;++team)if(tick%4==unsigned(team))pending[tick+4].push_back(caches[team].rank(observed,team,128,HiringPlanner::Pools::Split,option));
                    for(auto& output:pending[tick])published[output.team]=std::move(output);
                    for(const auto& [team,output]:published)for(int round=0;round<20;++round)for(const auto& building:output.buildings) {
                        unsigned bi=0;while(entities->buildings[bi].identity!=building.building)++bi;
                        if(assigned[bi]>=unsigned(entities->buildings[bi].desiredMaxUnitWorking))continue;
                        bool hired=false;
                        for(const auto& job:building.jobs)if(job.key==Hiring::Key{Hiring::Job::Fetch,WHEAT})
                            for(const auto* pool:{&job.combined,&job.available,&job.busy})for(const auto& candidate:*pool)
                                if(!hired&&candidate.available&&used.insert(candidate.unit.gid).second){hired=true;placements[candidate.unit.gid]=building.building;++assigned[bi];waits.push_back(tick);}
                    }
                    pending.erase(tick);
                }
                std::sort(waits.begin(),waits.end());
                delays.push_back({{"mode",mode},{"hires",waits.size()},{"p95_ticks",waits.empty()?0:waits[(waits.size()*95+99)/100-1]},{"latest_tick",waits.empty()?0:waits.back()}});
            }
            std::ofstream waiting(directory/("controlled-waits-"+std::to_string(stride)+".json"));waiting<<nlohmann::json{{"scope","Frozen positions/stock, fixed availability, assignments remain busy, K4/R4 admissions before publication; not live gameplay p95"},{"results",delays}}.dump(2)<<'\n';
            reset(stride);HiringDiagnostics::configure(true);
            for(int t=0;t<2;++t)game.teams[t]->updateAllBuildingTasks();
            std::ofstream detail(directory/("direct-phases-"+std::to_string(stride)+".json"));
            HiringDiagnostics::write(detail);HiringDiagnostics::configure(false);
        }
        std::ofstream out(directory/"opportunities.json");out<<nlohmann::json{{"scope","Static real-engine 1200-worker/48-building hiring opportunities; planner refresh versus direct fill pass including commitment; not gameplay or tick latency"},{"rows",rows}}.dump(2)<<'\n';
        return 0;
    }
    save(gui, directory/"initial.game.gz");
    int minimum=1200;
    for (int tick=0; tick<4096; ++tick)
    {
        game.syncStep(0);
        int count=0; for (int t=0; t<2; ++t) count+=game.teams[t]->liveUnits.size();
        minimum=std::min(minimum,count);
        if (tick==2047) save(gui,directory/"checkpoint-2048.game.gz");
    }
    save(gui,directory/"final.game.gz");
    nlohmann::json report={{"schema_version",1},{"kind",kind},{"seed",713},{"ticks",4096},{"initial_workers",1200},{"minimum_workers",minimum}};
    std::ofstream summary(directory/"fixture.json"); summary << report.dump(2) << '\n';
    std::cout << report.dump() << '\n';
    if (minimum<1000) throw std::runtime_error("Fixture failed sustained population requirement");
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
