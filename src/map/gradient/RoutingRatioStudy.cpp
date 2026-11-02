// SPDX-License-Identifier: GPL-3.0-or-later
#include "RoutingRatioStudy.h"
#include "GradientRuntime.h"
#include "Map.h"
#include "Game.h"
#include "Unit.h"
#include "UnitTiming.h"
#include "MapInternal.h"
#include "SharedBuildingGradient.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <bit>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
double processCpuSeconds() {
#if defined(_WIN32)
    FILETIME created,exited,kernel,user;
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))throw std::runtime_error("Cannot read process CPU time");
    const auto k=(std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime;
    const auto u=(std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime;
    return double(k+u)/10000000.0;
#elif defined(__EMSCRIPTEN__)
    return std::numeric_limits<double>::quiet_NaN();
#elif defined(CLOCK_PROCESS_CPUTIME_ID)
    timespec time{};
    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&time)!=0)throw std::runtime_error("Cannot read process CPU time");
    return double(time.tv_sec)+double(time.tv_nsec)/1000000000.0;
#else
    const auto value=std::clock();
    return value==std::clock_t(-1)?std::numeric_limits<double>::quiet_NaN():double(value)/CLOCKS_PER_SEC;
#endif
}
struct Trace { double distance=0,time=0;unsigned steps=0;int first=-1;std::size_t target=0;const char* status="truncated"; };
double edgeTime(const Map& map,std::size_t target,int dx,int dy,int walk,int swim) {
    const auto& terrain=map.terrainPropertiesAt(target);
    const int action=terrain.swimmable && swim>0 ? SWIM : WALK;
    const int speed=unitTerrainMovementSpeed(action==SWIM?swim:walk,terrain.groundSpeedQ8);
    const int advance=unitActionStepSpeed(speed,action,dx,dy);
    return advance>0 ? 256.0/advance : std::numeric_limits<double>::infinity();
}
int direction(const Map& map,const std::vector<Uint16>& seed,const std::vector<Uint16>& field,std::size_t at,int movement,EntityRandom* random=nullptr) {
    if(field[at]<=GRADIENT_UNREACHABLE || (seed[at]>GRADIENT_UNREACHABLE && seed[at]==field[at]))return -1;
    const int x=at%map.getW(),y=at/map.getW();int best=-1,score=-1,sidesteps[8],sidestepCount=0;
    for(int d=0;d<8;++d) {
        const int dx=tabClose[d][0],dy=tabClose[d][1];const auto next=map.coordToIndex(x+dx,y+dy);
        if(seed[next]==GRADIENT_FORBIDDEN || field[next]<=GRADIENT_UNREACHABLE)continue;
        if(field[next]==field[at]){sidesteps[sidestepCount++]=d;continue;}
        if(field[next]<field[at])continue;
        const int candidate=int(field[next])-map.stepCost(dx,dy,next,movement);
        if(candidate>score){score=candidate;best=d;}
    }
    if(best<0 && random && sidestepCount)best=sidesteps[random->nextU32()%sidestepCount];
    return best;
}
Trace trace(const Map& map,const std::vector<Uint16>& seed,const std::vector<Uint16>& field,
            std::size_t at,int movement,int walk,int swim,EntityRandom random) {
    Trace out;std::set<std::size_t> visited;
    if(field[at]<=GRADIENT_UNREACHABLE){out.status="unreachable";return out;}
    for(unsigned step=0;step<8192;++step) {
        if(seed[at]>GRADIENT_UNREACHABLE && seed[at]==field[at]){out.target=at;out.status="goal";return out;}
        if(!visited.insert(at).second){out.status="loop";return out;}
        const int d=direction(map,seed,field,at,movement,&random);
        if(d<0){out.status="no_direction";return out;}
        if(!out.steps)out.first=d;
        const int dx=tabClose[d][0],dy=tabClose[d][1];
        at=map.coordToIndex(int(at%map.getW())+dx,int(at/map.getW())+dy);
        out.distance+=dx&&dy?std::sqrt(2.0):1.0;out.time+=edgeTime(map,at,dx,dy,walk,swim);++out.steps;
    }
    return out;
}
double shortestTime(const Map& map,const std::vector<Uint16>& seed,std::size_t start,int walk,int swim) {
    const auto n=seed.size();const double infinity=std::numeric_limits<double>::infinity();
    std::vector<double> distance(n,infinity);
    using Entry=std::pair<double,std::size_t>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    for(std::size_t i=0;i<n;++i)if(seed[i]>GRADIENT_UNREACHABLE){distance[i]=0;queue.push({0,i});}
    while(!queue.empty()) {
        const auto [cost,at]=queue.top();queue.pop();if(cost!=distance[at])continue;
        if(at==start)return cost;
        const int x=at%map.getW(),y=at/map.getW();
        for(int d=0;d<8;++d) {
            const int dx=tabClose[d][0],dy=tabClose[d][1];const auto previous=map.coordToIndex(x-dx,y-dy);
            if(seed[previous]==GRADIENT_FORBIDDEN)continue;
            const auto candidate=cost+edgeTime(map,at,dx,dy,walk,swim);
            if(candidate<distance[previous]){distance[previous]=candidate;queue.push({candidate,previous});}
        }
    }
    return infinity;
}
}
RoutingRatioStudy::RoutingRatioStudy(const std::string& prefix,bool routeCollection):
 counts(prefix+".queries.csv"),state(prefix+".state.csv"),routes(prefix+".routes.csv"),process(prefix+".process.csv"),work(prefix+".work.csv"),ticks(prefix+".ticks.csv"),kernel(prefix+".kernel.csv"),model(prefix+".model.csv"),points(prefix+".points.csv"),collectRoutes(routeCollection) {
    cpuStarted=processCpuSeconds();
    if(!counts || !state || !routes || !process || !work || !ticks || !kernel || !model || !points)throw std::runtime_error("Cannot open routing ratio study");
    process<<"tick,completed_ticks,process_cpu_seconds,elapsed_seconds,rss_bytes,peak_rss_bytes,route_eligible,route_candidates,route_capped\n";
    work<<"tick,family,reason,requests,duplicates,admissions,publications,initialized_cells,prepared_searches,owner_resumes,queue_entries,expanded_cells\n";
    points<<"window,team,physical,selected,queries,reachable\n";
    model<<"window,family,team,selected,evaluations,above_threshold,prediction_sum,age_sum,q32_sum,q128_sum,added_sum,removed_sum,added_fraction_sum,removed_fraction_sum,invalid_goal_sum,no_move_sum,movement_queries_sum\n";
    kernel<<"tick,family,team,selected,builds,initialized_cells,queue_entries,expanded_cells\n";
    ticks<<"window,bin_lower_ns,count\n";
    for(auto* stream:{&counts,&state,&routes,&process,&work,&ticks,&kernel,&model,&points}) {
        stream->exceptions(std::ios::badbit|std::ios::failbit);stream->precision(17);
    }
    counts<<"window,family,team,physical,selected,planning,queries\n";
    state<<"tick,team,selected,known,walk_mode,swim_mode,population,buildings,class0,class1,class2,class3,class4,class5,class6,class7,class8,field_arrays_bytes,search_queue_bytes,idle_buffer_bytes,pending_output_reservation_bytes,shared_terrain_bytes\n";
    routes<<"sample,tick,family,team,unit,physical,selected,walk,swim,x,y,baseline_status,shared_status,baseline_distance,shared_distance,baseline_ticks,shared_ticks,first_direction_changed,equal_first_direction_cost,live_first_direction_changed,target_changed,uniform_comparable,uniform_different,actual_shortest_ticks\n";
}
RoutingRatioStudy::~RoutingRatioStudy(){flush();}
void RoutingRatioStudy::endTick() {
    ++completedTicks;
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-tickStarted).count();
    const auto bin=std::min<unsigned>(63,std::bit_width(std::uint64_t(std::max<std::int64_t>(0,elapsed))));
    ++tickHistogram[bin];
}
void RoutingRatioStudy::flush() {
    for(const auto& [key,n]:queries) {
        const auto [family,team,physical,selected,planning]=key;
        counts<<window<<','<<family<<','<<team<<','<<physical<<','<<selected<<','<<planning<<','<<n<<'\n';
    }
    queries.clear();
    for(const auto& [key,n]:modelTotals) {
        model<<window<<','<<key/(32*9)<<','<<(key/9)%32<<','<<key%9<<','<<n.evaluations<<','<<n.above<<','<<n.predictionSum;
        for(auto value:n.inputSum)model<<','<<value;
        for(auto value:n.countSum)model<<','<<value;
        model<<'\n';
    }
    modelTotals.clear();
    for(const auto& [key,n]:pointTotals) {
        const auto [team,physical,selected]=key;
        points<<window<<','<<team<<','<<physical<<','<<selected<<','<<n.first<<','<<n.second<<'\n';
    }
    pointTotals.clear();
    for(unsigned bin=0;bin<64;++bin)if(tickHistogram[bin])ticks<<window<<','<<(bin?std::uint64_t(1)<<(bin-1):0)<<','<<tickHistogram[bin]<<'\n';
    tickHistogram.fill(0);
    counts.flush();state.flush();routes.flush();process.flush();work.flush();ticks.flush();kernel.flush();model.flush();points.flush();
}
void RoutingRatioStudy::sample(Map& map) {
    const unsigned tick=map.game->stepCounter;
    if(window!=tick/512){flush();window=tick/512;windowSamples.fill(0);windowRouteSamples=0;}
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    process<<tick<<','<<completedTicks<<',';
    const auto cpu=processCpuSeconds()-cpuStarted;if(std::isfinite(cpu))process<<cpu;
    process<<','<<elapsed<<',';
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");std::uint64_t pages=0,resident=0;statm>>pages>>resident;
    struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
    process<<resident*std::uint64_t(sysconf(_SC_PAGESIZE))<<','<<std::uint64_t(usage.ru_maxrss)*1024;
#else
    process<<","; // Unavailable is not zero.
#endif
    process<<','<<eligible<<','<<candidates<<','<<capped<<'\n';
    if(auto& stats=map.gradientRuntime->workStats) {
        stats->flush();
        for(unsigned family=0;family<7;++family)for(unsigned reason=0;reason<8;++reason) {
            const auto& n=stats->totals[family][reason];
            const std::array<std::uint64_t,9> current{n.requests,n.duplicates,n.admissions,n.publications,n.fieldCells,n.preparedSearches,n.partialResumes,n.queuedEntries,n.expandedCells};
            work<<tick<<','<<family<<','<<reason;
            for(unsigned i=0;i<current.size();++i)work<<','<<current[i]-previousWork[family][reason][i];
            work<<'\n';previousWork[family][reason]=current;
        }
    }
    if(auto sink=map.gradientRuntime->workSink) {
        for(unsigned f=0;f<7;++f)for(int team=0;team<map.game->teamsCount();++team)for(unsigned c=0;c<MOVEMENT_CLASS_COUNT;++c) {
            auto& n=sink->fields[GradientKernelWork::key(f,team,c)];
            const auto builds=n.builds.exchange(0),cells=n.initializedCells.exchange(0),queued=n.queued.exchange(0),expanded=n.expanded.exchange(0);
            if(builds || cells || queued || expanded)kernel<<tick<<','<<f<<','<<team<<','<<c<<','<<builds<<','<<cells<<','<<queued<<','<<expanded<<'\n';
        }
    }
    std::set<const void*> arrays,terrainInputs;std::uint64_t searches=0,terrainBytes=0;
    const auto terrain=[&](const void* identity,std::size_t bytes){if(terrainInputs.insert(identity).second)terrainBytes+=bytes;};
    if(map.waterSnapshot)terrain(map.waterSnapshot.get(),sizeof(*map.waterSnapshot)+map.waterSnapshot->capacity());
    for(const auto& profiles:map.terrainMovementSnapshots)if(profiles) {
        const auto& m=profiles->movement;
        terrain(profiles.get(),sizeof(*profiles)+profiles->cells.capacity()+m.entries.capacity()*sizeof(m.entries[0])+
            m.profiles.capacity()*sizeof(m.profiles[0])+m.profileIds.capacity()+m.steps.capacity()*sizeof(unsigned));
    }
    for(int t=0;t<map.game->teamsCount();++t) {
        for(int c=0;c<MOVEMENT_CLASS_COUNT;++c) {
            for(int material=0;material<MaterialCount;++material) {
                if(auto* field=map.materialGradients[t][material][c])arrays.insert(field);
                if(auto* field=map.marketMaterialGradients[t][material][c])arrays.insert(field);
            }
            for(auto* field:{map.forbiddenGradient[t][c],map.guardAreasGradient[t][c],map.clearAreasGradient[t][c]})if(field)arrays.insert(field);
        }
        for(auto* building:map.game->teams[t]->liveBuildings.entries())
            for(int slot=0;slot<BUILDING_GRADIENT_COUNT;++slot) {
                auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
                if(auto* field=building->globalGradient[slot])arrays.insert(field);
                if(auto& search=building->globalGradientSearch[slot]){searches+=search->retainedBytes();search->visitRetainedTerrain(terrain);}
            }
    }
    for(const auto& [key,field]:map.gradientRuntime->materialFields)if(field.published)arrays.insert(field.published);
    const auto arrayBytes=arrays.size()*map.size*sizeof(Uint16);
    const auto idle=map.idleGradientBufferCount*map.size*sizeof(Uint16);
    const auto pending=(map.gradientRuntime->pipeline.pendingCount()+map.gradientRuntime->buildings.pendingCount())*map.size*sizeof(Uint16);
    for(int t=0;t<map.game->teamsCount();++t) {
        const auto& team=*map.game->teams[t];std::array<unsigned,MOVEMENT_CLASS_COUNT> population{};
        for(const auto* unit:team.liveUnits.entries())if(!unit->isDead)++population[unit->movementClass()];
        const auto& r=team.routingRatio;
        state<<tick<<','<<t<<','<<r.selected<<','<<r.known<<','<<r.walk<<','<<r.swim<<','<<std::accumulate(population.begin(),population.end(),0u)<<','<<team.liveBuildings.size();
        for(auto n:population)state<<','<<n;
        state<<','<<arrayBytes<<','<<searches<<','<<idle<<','<<pending<<','<<terrainBytes<<'\n';
    }
    flush();
}
void RoutingRatioStudy::observe(Map& map,unsigned family,int team,int physical,int x,int y,bool planning,
                               int material,Building* building,BuildingRoute route) {
    const unsigned tick=map.game->stepCounter;
    if(window!=tick/512){flush();window=tick/512;windowSamples.fill(0);windowRouteSamples=0;}
    if(family==Consumer && building) {
        const auto modes=map.materialSupplyModesSlot(building,material);
        const unsigned supplied=((modes&1)?building->runtime->suppliesStockMask:0)|((modes&2)?building->runtime->suppliesDirectStockMask:0);
        const bool excludesSelf=building->runtime->has(BuildingRuntimeTraits::SharedStock)||(supplied&(1u<<material));
        family=((modes&2)||((modes&1)&&excludesSelf))?Consumer:(modes&1)?Markets:Materials;
    }
    const auto selected=map.routingClass(team,physical);
    ++queries[{family,unsigned(team),unsigned(physical),unsigned(selected),planning}];++ordinal;
    const Unit* unit=RoutingRatioReader::current;
    if(!collectRoutes || planning || !unit || (unit->owner->teamNumber!=team && family<Consumer) || !RoutingRatioState::amphibious(physical) || family>=Point)return;
    // Fixed 1/1024 hash sample of eligible queries. The independent observer
    // hash neither reads nor advances any gameplay random stream.
    std::uint64_t hash=++eligible+0x9e3779b97f4a7c15ull;
    hash=(hash^(hash>>30))*0xbf58476d1ce4e5b9ull;
    hash=(hash^(hash>>27))*0x94d049bb133111ebull;hash^=hash>>31;
    if((hash&1023)!=0)return;
    ++candidates;
    if(windowSamples[family]>=64 || windowRouteSamples>=8 || routeSamples>=2048){++capped;return;}
    ++windowSamples[family];++windowRouteSamples;++routeSamples;
    constexpr auto requirements=SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs)|SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain)|SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)|SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy)|SimulationSnapshot::bit(SimulationSnapshot::Component::Areas)|SimulationSnapshot::bit(SimulationSnapshot::Component::Visibility)|SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)|SimulationSnapshot::bit(SimulationSnapshot::Component::Teams)|SimulationSnapshot::bit(SimulationSnapshot::Component::Rules);
    const auto snapshot=SimulationSnapshot::capture(*map.game,SimulationSnapshot::captureCatalog(*map.game),requirements);
    gradient_preparation::CrowdingScratch seeds;seeds.materials.disabled=true;
    GradientWorkspace workspace;gradient_kernel::CPUSelectionScope cpu(gradient_kernel::Backend::CPU);
    const auto build=[&](int movement,std::vector<Uint16>& seeded,std::vector<Uint16>& field) {
        seeded.resize(map.size);
        if(family==BuildingField) {
            auto seed=gradient_preparation::captureBuildingSeed(*building,movement,route);
            gradient_preparation::seedBuilding(seed,snapshot,seeded.data());field=seeded;
            BuildingGradientSearch search;search.begin(BuildingGradientSearch::Inputs::ofSnapshot(snapshot,movement),field.data(),movement,map.getW(),map.getH());search.finish();
        } else {
            gradient_preparation::Request request;
            request.kind=family==Consumer?gradient_preparation::Kind::Markets:gradient_preparation::Kind(family);
            request.team=team;request.material=material;request.swim=movement;request.allies=map.game->teams[team]->allies;
            request.crowding=map.game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing);request.terrainBuckets=map.movementQueueBuckets(movement);
            if(family==Consumer && building){request.consumer=building->gid;request.modes=map.materialSupplyModesSlot(building,material);request.excludesShared=building->usesTeamMaterials();}
            gradient_preparation::seed(request,snapshot,seeded.data(),seeds);field=seeded;
            gradient_preparation::propagateCPU(request,snapshot,field.data(),workspace);
        }
    };
    std::vector<Uint16> baselineSeed,baseline,sharedSeed,shared;
    build(physical,baselineSeed,baseline);build(selected,sharedSeed,shared);
    const auto start=map.coordToIndex(x,y);const int walk=unit->performance[WALK],swim=unit->performance[SWIM];
    const auto a=trace(map,baselineSeed,baseline,start,physical,walk,swim,unit->entityRandom),b=trace(map,sharedSeed,shared,start,selected,walk,swim,unit->entityRandom);
    unsigned comparable=0,different=0;
    for(std::size_t i=(hash>>32)%256;i<map.size;i+=256) {
        const int da=direction(map,baselineSeed,baseline,i,physical),db=direction(map,sharedSeed,shared,i,selected);
        if(da>=0 && db>=0){++comparable;different+=da!=db;}
    }
    bool equal=false;
    if(a.first>=0 && b.first>=0) {
        const auto score=[&](int d) {const int dx=tabClose[d][0],dy=tabClose[d][1];const auto next=map.coordToIndex(x+dx,y+dy);return int(baseline[next])-map.stepCost(dx,dy,next,physical);};
        equal=score(a.first)==score(b.first);
    }
    routes<<routeSamples<<','<<tick<<','<<family<<','<<team<<','<<unit->gid<<','<<physical<<','<<selected<<','<<walk<<','<<swim<<','<<x<<','<<y<<','<<a.status<<','<<b.status<<','<<a.distance<<','<<b.distance<<','<<a.time<<','<<b.time<<','<<(a.first!=b.first)<<','<<equal<<',';
    if(family<=Clear) {
        GradientQueryContext ca{{GradientFieldFamily(family),Uint8(team),Uint8(physical),family<=Markets?Uint8(material):GradientFieldKey::NoMaterial},x,y,unit->entityRandom};
        auto cb=ca;cb.key.swim=selected;
        const auto da=map.selectGradientQuery(ca,baseline.data()),db=map.selectGradientQuery(cb,shared.data());
        routes<<(da.dx!=db.dx || da.dy!=db.dy || da.moved!=db.moved);
    }
    routes<<','<<(a.target!=b.target)<<','<<comparable<<','<<different<<',';
    if((routeSamples%16)==0 && map.size<=65536) routes<<shortestTime(map,baselineSeed,start,walk,swim);
    routes<<'\n';
}
void Map::configureRoutingRatioStudy(const std::string& prefix,bool routes) {
    std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
    gradientRuntime->ratioStudy=std::make_unique<RoutingRatioStudy>(prefix,routes);
    sampleRoutingRatioStudy();
}
void Map::sampleRoutingRatioStudy(){if(auto& study=gradientRuntime->ratioStudy)study->sample(*this);}
void Map::observeRoutingRatioQuery(unsigned family,int team,int physical,int x,int y,bool planning,int material,Building* building,BuildingRoute route) {
    if(auto& study=gradientRuntime->ratioStudy)study->observe(*this,family,team,physical,x,y,planning,material,building,route);
}

void Map::beginRoutingRatioTick(){if(auto& study=gradientRuntime->ratioStudy)study->beginTick();}
void Map::endRoutingRatioTick(){if(auto& study=gradientRuntime->ratioStudy)study->endTick();}

std::shared_ptr<GradientKernelWork::Sink> Map::gradientWorkSink() const {return gradientRuntime->workSink;}
