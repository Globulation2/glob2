// SPDX-License-Identifier: GPL-3.0-or-later
#include "MovementPlanner.h"
#include "OwnerRandom.h"
#include "field/TerrainMovementCosts.h"
#include "field/GradientConstants.h"
#include "TerrainLine.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

namespace MovementPlanning
{
namespace
{
constexpr Uint32 Unknown = std::numeric_limits<Uint32>::max();
Uint64 mix(Uint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
unsigned swimClass(const Task& task) { return gradient_kernel::movementSwimClass(task.walk, task.swim); }
unsigned step(const SimulationSnapshot::Handle& world, const Task& task, std::size_t index, bool diagonal)
{
    const auto& rule = (*world.terrain->rules)[world.terrain->cellRules[index]];
    if (task.fly) return rule.airRouteCost * (diagonal ? GRADIENT_DIAGONAL_STEP : GRADIENT_STEP) / GRADIENT_STEP;
    const auto costs = rule.ground[swimClass(task)];
    return diagonal ? costs.diagonal : costs.cardinal;
}
int distance(int x, int y, int tx, int ty, int w, int h)
{
    const int dx = std::abs(x-tx), dy = std::abs(y-ty);
    const int wx = std::min(dx, w-dx), wy = std::min(dy, h-dy);
    return std::max(wx,wy)*GRADIENT_STEP + std::min(wx,wy)*(GRADIENT_DIAGONAL_STEP-GRADIENT_STEP);
}
unsigned heading(int dx,int dy,int width,int height)
{
    if(dx>width/2)dx-=width;else if(dx<-width/2)dx+=width;
    if(dy>height/2)dy-=height;else if(dy<-height/2)dy+=height;
    if(Sint64(100)*std::abs(Sint64(dx))>Sint64(241)*std::abs(Sint64(dy)))dy=0;
    else if(Sint64(100)*std::abs(Sint64(dy))>Sint64(241)*std::abs(Sint64(dx)))dx=0;
    const int x=(dx>0)-(dx<0),y=(dy>0)-(dy<0);
    for(unsigned i=0;i<8;++i)if(Directions[i][0]==x && Directions[i][1]==y)return i;
    return 8;
}
const SimulationSnapshot::BuildingView* building(const SimulationSnapshot::Handle& world, BuildingRef ref)
{
    if (ref.empty()) return nullptr;
    for (const auto& b : world.entities->buildings) if (b.identity == ref) return &b;
    return nullptr;
}
}
Task taskOf(const SimulationSnapshot::UnitCoreView& u)
{
    return {u.identity,{},u.attached,u.target,u.team,u.core.posX,u.core.posY,u.core.targetX,u.core.targetY,u.core.destinationPurpose,
        u.core.activity,u.core.medical,u.core.displacement,u.core.jobPurpose,u.core.capabilityFlags,u.performance(WALK),u.performance(SWIM),u.performance(FLY),u.core.direction};
}
std::optional<Mode> modeOf(const UnitCoreState& u)
{
    if ((!u.performance(WALK) && !u.performance(SWIM) && !u.performance(FLY)) || u.isDead || u.displacement==UnitState::DIS_INSIDE || u.displacement==UnitState::DIS_ENTERING_BUILDING
        || u.displacement==UnitState::DIS_EXITING_BUILDING || u.displacement==UnitState::DIS_HARVESTING
        || u.displacement==UnitState::DIS_FILLING_BUILDING) return {};
    switch (u.displacement) {
    case UnitState::DIS_GOING_TO_RESOURCE: return Mode::Resource;
    case UnitState::DIS_GOING_TO_FLAG:
    case UnitState::DIS_GOING_TO_BUILDING: return Mode::Building;
    case UnitState::DIS_REMOVING_BLACK_AROUND: return Mode::Explore;
    case UnitState::DIS_CLEARING_RESOURCES: return Mode::Clearing;
    case UnitState::DIS_ATTACKING_AROUND:
        return u.validTarget && (u.movement==UnitState::MOV_GOING_TARGET || u.movement==UnitState::MOV_FLYING_TARGET) ? Mode::Pursuit : Mode::Idle;
    case UnitState::DIS_RANDOM: return Mode::Idle;
    default: return {};
    }
}
std::optional<Uint32> completionTicks(const UnitCoreState& u)
{
    const int advance = unitActionStepSpeed(u.speed,u.action,u.dx,u.dy,u.displacement==UnitState::DIS_INSIDE);
    if (advance <= 0) return {};
    // syncStep completes only when delta > MAX - advance; one action at most.
    const Sint64 remaining = Sint64(UNIT_DELTA_QUANTUM)-u.delta;
    return Uint32(std::max<Sint64>(1,(remaining+advance-1)/advance));
}
bool validPermutation(const Suggestion& s)
{
    if (s.preferred > 8) return false;
    unsigned mask = 0;
    for (auto direction : s.directions) { if (direction >= 8 || (mask & (1u<<direction))) return false; mask |= 1u<<direction; }
    return mask == 255;
}
EntityRandom requestRandom(Uint32 seed, const Request& r)
{
    const Uint64 identity = (Uint64(r.task.unit.generation)<<16)|r.task.unit.gid;
    EntityRandom result;
    result.seed(mix(Uint64(seed)^mix(identity)^mix(r.sequence)),
        mix(identity^mix(r.sequence)^Uint64(RandomDomain::MovementPlanning)));
    return result;
}
bool towerThreat(const SimulationSnapshot::Handle& world, const Task& task, int x, int y)
{
    if(!world.teams)return false;
    const auto view=world.view(); x=view.normalizeX(x); y=view.normalizeY(y);
    bool blocking=false;
    for(std::size_t i=0;i<world.terrain->rules->size();++i)
        blocking |= (*world.terrain->rules)[i].properties.projectileBlocks;
    for(const auto& b:world.entities->buildings) {
        if(!(world.teams->values.at(task.team).enemies & (1u<<b.team)))continue;
        const auto& type=world.catalogs->typeDefinitions->at(b.typeNum);
        if(type.shootingRange<=0)continue;
        const int ax=std::abs(x-b.posX),ay=std::abs(y-b.posY);
        if(std::max(std::min(ax,world.width-ax),std::min(ay,world.height-ay))>type.shootingRange+1)continue;
        if(!blocking)return true;
        if(view.terrainProperties(view.index(x,y)).projectileBlocks)continue;
        // Match the integer firing trajectory, including the four-pixel bullet
        // offset. TerrainLine is the live projectile supercover kernel.
        const int ox=b.posX*32+type.width*16,oy=b.posY*32+type.height*16;
        int dx=x*32+12-ox,dy=y*32+12-oy;
        if(dx>world.width*16)dx-=world.width*32;
        if(dx<-world.width*16)dx+=world.width*32;
        if(dy>world.height*16)dy-=world.height*32;
        if(dy<-world.height*16)dy+=world.height*32;
        const int maximum=std::max(std::abs(dx),std::abs(dy));
        int sx=0,sy=0,ticks=0;
        if(maximum) {
            sx=int(Sint64(dx)*type.shootSpeed/(Sint64(maximum)*256));
            sy=int(Sint64(dy)*type.shootSpeed/(Sint64(maximum)*256));
            const int axis=std::abs(dx)>std::abs(dy)?sx:sy;
            if(!axis)throw std::invalid_argument("invalid captured tower projectile speed");
            ticks=std::abs(maximum/axis);
        }
        if(terrainSegmentClear(ox,oy,ox+sx*ticks,oy+sy*ticks,[&](Sint64 tx,Sint64 ty) {
            return view.terrainProperties(view.index(int(tx),int(ty))).projectileBlocks;
        }))return true;
    }
    return false;
}
bool legal(const MapState::View& view, const Task& task, int x, int y, bool occupancy)
{
    const auto index = view.index(x,y);
    const auto& terrain = view.terrainProperties(index);
    const auto& cell = view.occupancy[index];
    if (task.fly) return terrain.flyable && !MapState::resourceBlocksAir(view,index)
        && (!occupancy || cell.airUnit==0xffff || cell.airUnit==task.unit.gid);
    return (task.walk>0 || task.swim>0) && gradient_kernel::terrainAllowsGround(terrain,swimClass(task))
        && !(view.areas[index].forbidden & (1u<<task.team))
        && cell.building==0xffff && !MapState::resourceBlocksGround(view,index)
        && (!occupancy || cell.groundUnit==0xffff || cell.groundUnit==task.unit.gid);
}
bool legal(const SimulationSnapshot::Handle& world, const Task& task, int x, int y, bool occupancy)
{
    return legal(world.view(),task,x,y,occupancy);
}
Uint64 Planner::retainedBytes() const
{
    Uint64 result = cache.capacity()*sizeof(Field);
    for (const auto& field : cache) result += (field.costs.capacity()+field.destinations.capacity())*sizeof(Uint32);
    return result;
}
Planner::Field Planner::build(const SimulationSnapshot::Handle& world, const Request& r, const Key& key)
{
    ++metrics.fields;
    const auto n = std::size_t(world.width)*world.height;
    // Two result arrays plus a bounded indexed heap; no duplicate queue nodes.
    if (budget < sizeof(Field) || n > (budget-sizeof(Field))/(4*sizeof(Uint32))) throw std::invalid_argument("movement field exceeds its team memory quota");
    Field field{key,std::vector<Uint32>(n,Unknown),std::vector<Uint32>(n,Unknown)};
    std::vector<Uint32> heap; heap.reserve(n);
    std::vector<Uint32> positions(n,Unknown);
    const auto before = [&](Uint32 a, Uint32 b) { return std::tie(field.costs[a],a) < std::tie(field.costs[b],b); };
    const auto swap = [&](std::size_t a,std::size_t b) { std::swap(heap[a],heap[b]); positions[heap[a]]=Uint32(a); positions[heap[b]]=Uint32(b); };
    const auto enqueue = [&](Uint32 index) {
        std::size_t p = positions[index];
        if (p==Unknown) { p=heap.size(); positions[index]=Uint32(p); heap.push_back(index); }
        while (p && before(heap[p],heap[(p-1)/2])) { swap(p,(p-1)/2); p=(p-1)/2; }
    };
    const auto view = world.view();
    const auto seedAt = [&](int x,int y,Uint32 destination) {
        if (!legal(view,r.task,x,y,false)) return;
        const auto i = Uint32(view.index(x,y));
        if (field.costs[i]==0) { field.destinations[i]=std::min(field.destinations[i],destination); return; }
        field.costs[i]=0; field.destinations[i]=destination; enqueue(i);
    };
    const auto adjacent = [&](int x,int y,Uint32 destination) {
        for (auto d : Directions) seedAt(x+d[0],y+d[1],destination);
    };
    if (r.mode==Mode::Resource) {
        for (std::size_t i=0;i<n;++i)
            if (MapState::hasMaterialSlot(view,i,r.task.material)) adjacent(int(i%world.width),int(i/world.width),Uint32(i));
        // Published market planes retain their existing age; stock goals below
        // are captured values, never reservations or supplier subscriptions.
        const auto* home = building(world,r.task.attached);
        if (home && r.task.material>=0 && r.task.material<int(MaterialCount))
            for (const auto& b : world.entities->buildings)
                if (b.team==r.task.team && b.identity!=r.task.attached
                    && (b.availableSupplyMask & (1u<<r.task.material))
                    && ((world.catalogs->typeDefinitions->at(home->typeNum).runtimeFetchesStockMask | world.catalogs->typeDefinitions->at(home->typeNum).runtimeFetchesDirectStockMask) & (1u<<r.task.material)))
                    for (int y=0;y<world.catalogs->typeDefinitions->at(b.typeNum).height;++y) for (int x=0;x<world.catalogs->typeDefinitions->at(b.typeNum).width;++x)
                        adjacent(b.posX+x,b.posY+y,Uint32(view.index(b.posX+x,b.posY+y)));
    } else if (r.mode==Mode::Clearing || (r.mode==Mode::Idle && r.task.displacement==UnitState::DIS_ATTACKING_AROUND)) {
        for (std::size_t i=0;i<n;++i) {
            const auto areas = view.areas[i];
            if ((r.mode==Mode::Clearing ? areas.clear : areas.guard) & (1u<<r.task.team))
                seedAt(int(i%world.width),int(i/world.width),Uint32(i));
        }
    } else if (const auto* b = building(world,r.task.target)) {
        // Real buildings are approached at their perimeter; virtual flags have
        // no occupied footprint and remain point destinations.
        if (world.catalogs->typeDefinitions->at(b->typeNum).isVirtual) seedAt(b->posX,b->posY,Uint32(view.index(b->posX,b->posY)));
        else for (int y=0;y<world.catalogs->typeDefinitions->at(b->typeNum).height;++y) for (int x=0;x<world.catalogs->typeDefinitions->at(b->typeNum).width;++x)
            adjacent(b->posX+x,b->posY+y,Uint32(view.index(b->posX+x,b->posY+y)));
    } else seedAt(r.task.targetX,r.task.targetY,Uint32(view.index(r.task.targetX,r.task.targetY)));
    if (heap.empty()) ++metrics.noGoals;
    const unsigned limit = r.mode==Mode::Pursuit ? GOING_TARGET_MAX_PATH_LENGTH*GRADIENT_STEP : gradient_kernel::COST_LIMIT;
    while (!heap.empty()) {
        const auto current = heap.front();
        swap(0,heap.size()-1); heap.pop_back(); positions[current]=Unknown;
        std::size_t p=0;
        while (2*p+1 < heap.size()) {
            std::size_t child=2*p+1;
            if (child+1<heap.size() && before(heap[child+1],heap[child])) ++child;
            if (!before(heap[child],heap[p])) break;
            swap(child,p); p=child;
        }
        ++metrics.visited;
        const int x=int(current%world.width), y=int(current/world.width);
        for (auto d : Directions) {
            const int nx=view.normalizeX(x+d[0]),ny=view.normalizeY(y+d[1]);
            if (!legal(view,r.task,nx,ny,false)) continue;
            const auto next=Uint32(view.index(nx,ny));
            // Reverse arc u<-v pays entry(v), as in the forward u->v move.
            const auto candidate=field.costs[current]+step(world,r.task,current,d[0]&&d[1]);
            if (candidate>limit) { field.truncated=true; continue; }
            if (candidate < field.costs[next]) {
                field.costs[next]=candidate; field.destinations[next]=field.destinations[current]; enqueue(next);
            }
        }
    }
    metrics.peakBytes=std::max<Uint64>(metrics.peakBytes,retainedBytes()+16*n);
    return field;
}
Suggestion Planner::plan(const SimulationSnapshot::Handle& world, const Request& request, Uint32 seed)
{
    if (!world.terrain || !world.resources || !world.occupancy || !world.areas || !world.entities || !world.catalogs)
        throw std::invalid_argument("movement planner needs complete immutable inputs");
    Suggestion result; result.request=request;
    auto random = requestRandom(seed,request);
    // Fisher-Yates uses this request's stream only; ordering is portable.
    for (unsigned i=8;i>1;--i) std::swap(result.directions[i-1],result.directions[random.nextU32()%i]);
    const auto view=world.view();
    const auto here=view.index(request.task.x,request.task.y);
    auto targetX=request.task.targetX, targetY=request.task.targetY;
    std::optional<unsigned> explorationHeading;
    if (request.mode==Mode::Explore) {
        if (!world.exploration || world.exploration->teams.at(request.task.team).empty())
            throw std::invalid_argument("exploration planner needs captured exploration");
        const auto& explored=world.exploration->teams[request.task.team];
        const auto simplified=[&](int x,int y) { return heading(x-request.task.x,y-request.task.y,world.width,world.height); };
        if(const auto* attached=building(world,request.task.attached)) {
            const auto d=simplified(attached->posX,attached->posY);
            if(d<8) {
                const auto v=Directions[d];const auto look=view.index(request.task.x+4*v[0],request.task.y+4*v[1]);
                const bool discovered=world.visibility && (world.visibility->discovered[look]&world.teams->values.at(request.task.team).otherVision);
                explorationHeading=discovered ? (d+2)%8 : d;
            }
        } else {
            // Same local contour test as the reference, before global steering.
            constexpr int xs[8]={-4,-2,2,4,4,2,-2,-4},ys[8]={-2,-4,-4,-2,2,4,4,2};
            int samples[8];
            for(int i=0;i<8;++i)samples[i]=towerThreat(world,request.task,request.task.x+xs[i],request.task.y+ys[i])?1:explored[view.index(request.task.x+xs[i],request.task.y+ys[i])];
            for(unsigned i=0;i<8;++i) {
                const unsigned d=(i+request.task.heading+4)%8;
                if(samples[d]>0 && samples[(d+1)%8]==0 && samples[(d+2)%8]==0){explorationHeading=(d+1)%8;break;}
            }
        }
        if(!explorationHeading) {
            Sint64 sx=0,sy=0;
            for(int range=1;range<=std::min(world.width,world.height)/2;range*=2)
                for(int d=-3;d<=3;++d) {
                    sx+=explored[view.index(request.task.x-4*range,request.task.y+d*range)]-explored[view.index(request.task.x+4*range,request.task.y+d*range)];
                    sy+=explored[view.index(request.task.x+d*range,request.task.y-4*range)]-explored[view.index(request.task.x+d*range,request.task.y+4*range)];
                }
            explorationHeading=simplified(request.task.x+int(sx),request.task.y+int(sy));
        }
        if(*explorationHeading<8){const auto d=Directions[*explorationHeading];targetX=view.normalizeX(request.task.x+d[0]);targetY=view.normalizeY(request.task.y+d[1]);}
        else {targetX=request.task.x;targetY=request.task.y;}
        result.destination=Destination{targetX,targetY,{}};
    }
    const bool geometric=request.mode==Mode::Explore || (request.task.fly && !world.terrain->airConstraints
        && request.mode!=Mode::Resource && request.mode!=Mode::Clearing && request.mode!=Mode::Idle);
    bool guardGoal=false;
    if(request.mode==Mode::Idle && request.task.displacement==UnitState::DIS_ATTACKING_AROUND)
        for(const auto& a:world.areas->cells)guardGoal|=bool(a.guard & (1u<<request.task.team));
    const auto* patrolBuilding=building(world,request.task.attached);
    const bool patrol=request.mode==Mode::Idle && request.task.displacement==UnitState::DIS_ATTACKING_AROUND
        && (patrolBuilding || (world.areas->cells[here].guard & (1u<<request.task.team)));
    result.patrol=patrol;
    const bool wander=(request.mode==Mode::Idle && (!guardGoal || request.task.displacement!=UnitState::DIS_ATTACKING_AROUND || patrol))
        || (explorationHeading && *explorationHeading==8);
    const unsigned geometricHeading=heading(targetX-request.task.x,targetY-request.task.y,world.width,world.height);
    result.steering=wander?Steering::Wander:request.mode==Mode::Explore?Steering::Exploration:geometric?Steering::Geometric:Steering::Route;
    std::array<std::pair<unsigned,unsigned>,8> ranks;
    ranks.fill({2,Unknown});
    if (wander || geometric) {
        const auto oldDistance=distance(request.task.x,request.task.y,targetX,targetY,world.width,world.height);
        for (unsigned i=0;i<8;++i) {
            const auto d=Directions[i]; const int x=view.normalizeX(request.task.x+d[0]), y=view.normalizeY(request.task.y+d[1]);
            if (!legal(view,request.task,x,y,false)) continue;
            const auto remaining=unsigned(distance(x,y,targetX,targetY,world.width,world.height));
            if (wander) {
                const auto& terrain=view.terrainProperties(view.index(x,y));
                const bool hazard=(request.task.fly ? terrain.airHealthQ8 : terrain.groundHealthQ8)<0;
                const bool tower=request.task.fly && towerThreat(world,request.task,x,y);
                const bool guard=world.rules && world.rules->configuration->hasExperiment(ExperimentId::GuardAreaBalancing)
                    && (request.task.capabilities & UnitRuntimeTraits::GuardIdle) && (world.areas->cells[here].guard & (1u<<request.task.team));
                const bool leavesGuard=guard && !(world.areas->cells[view.index(x,y)].guard & (1u<<request.task.team));
                ranks[i]={hazard?2u:tower || leavesGuard?1u:0u,0};
                if(patrol && world.exploration) {
                    bool inside=bool(world.areas->cells[view.index(x,y)].guard & (1u<<request.task.team));
                    if(patrolBuilding) {
                        const int ax=std::abs(x-view.normalizeX(patrolBuilding->posX)),ay=std::abs(y-view.normalizeY(patrolBuilding->posY));
                        const int wx=std::min(ax,world.width-ax),wy=std::min(ay,world.height-ay);
                        inside=Sint64(wx)*wx+Sint64(wy)*wy<=Sint64(patrolBuilding->unitStayRange)*patrolBuilding->unitStayRange;
                    }
                    const auto& e=world.exploration->teams.at(request.task.team);
                    const unsigned explored=e[view.index(request.task.x+2*d[0],request.task.y+2*d[1])]
                        +e[view.index(request.task.x+2*d[0]-d[1],request.task.y+2*d[1]+d[0])]
                        +e[view.index(request.task.x+2*d[0]+d[1],request.task.y+2*d[1]-d[0])];
                    ranks[i]={inside?0u:2u,explored};
                }
            } else {
                if(request.mode==Mode::Explore && towerThreat(world,request.task,x,y))continue;
                ranks[i]={remaining<unsigned(oldDistance)?0u:remaining==unsigned(oldDistance)?1u:2u,remaining+step(world,request.task,view.index(x,y),d[0]&&d[1])};
                if(explorationHeading && *explorationHeading<8)ranks[i]={i==*explorationHeading?0u:2u,0};
            }
        }
        result.disposition=geometric && !wander && oldDistance==0 ? Disposition::GoalReached : Disposition::Travel;
    } else if (request.mode==Mode::Resource && world.resourceFields
        && request.task.material>=0 && request.task.material<int(MaterialCount)
        && world.resourceFields->find(MapState::planeKey(request.task.team,request.task.material,swimClass(request.task),false))
        && (!building(world,request.task.attached) ||
            !(world.catalogs->typeDefinitions->at(building(world,request.task.attached)->typeNum).runtimeFetchesStockMask
              | world.catalogs->typeDefinitions->at(building(world,request.task.attached)->typeNum).runtimeFetchesDirectStockMask))) {
        const auto& plane=*world.resourceFields->find(MapState::planeKey(request.task.team,request.task.material,swimClass(request.task),false))->values;
        const auto old=plane[here];
        if(old>GRADIENT_UNREACHABLE)result.originCost=GRADIENT_AT_GOAL-old;
        result.disposition=old==GRADIENT_AT_GOAL?Disposition::GoalReached:
            old<=GRADIENT_UNREACHABLE?Disposition::NoRoute:Disposition::Travel;
        ++metrics.cacheHits; // borrowed, immutable published plane; no lazy expansion
        if(MapState::hasMaterialSlot(view,view.index(targetX,targetY),request.task.material))
            result.destination=Destination{targetX,targetY,{}};
        for(unsigned i=0;i<8;++i) {
            const auto d=Directions[i]; const auto next=view.index(request.task.x+d[0],request.task.y+d[1]);
            const auto value=plane[next];
            if(value<=GRADIENT_UNREACHABLE || !legal(view,request.task,request.task.x+d[0],request.task.y+d[1],false))continue;
            ranks[i]={value>old?0u:value==old?1u:2u,unsigned(GRADIENT_AT_GOAL-value)+step(world,request.task,next,d[0]&&d[1])};
            result.stepCosts[i]=ranks[i].second;
        }
    } else {
        // Route fields depend on travel capabilities and goal semantics, not
        // each caller's medical/activity state, assignment or incarnation.
        // Keep the full task on the suggestion for authoritative validation.
        Task sharedTask;
        sharedTask.team=request.task.team;
        sharedTask.walk=request.task.walk;
        sharedTask.swim=request.task.swim;
        sharedTask.fly=request.task.fly;
        if(request.mode==Mode::Resource) {
            sharedTask.material=request.task.material;
            sharedTask.attached=request.task.attached; // supplier/home semantics
        } else if(request.mode!=Mode::Clearing && request.mode!=Mode::Idle) {
            if(building(world,request.task.target))sharedTask.target=request.task.target;
            else { sharedTask.targetX=request.task.targetX;sharedTask.targetY=request.task.targetY; }
        }
        Uint64 structures=0;
        for (const auto& b:world.entities->buildings)
            structures=mix(structures^mix(Uint64(b.identity.generation)<<16|b.identity.gid)^mix(Uint64(b.posX)<<32|Uint32(b.posY))^mix(Uint64(b.typeNum)<<32|b.availableSupplyMask));
        Key key{sharedTask,request.mode,world.worldIdentity,world.mapGenerations[0],world.mapGenerations[1],world.mapGenerations[3],world.configurationRevision,structures};
        auto found=std::find_if(cache.begin(),cache.end(),[&](const Field& f){return f.key==key;});
        if (found==cache.end()) {
            const auto required=std::size_t(world.width)*world.height*16+sizeof(Field);
            while (!cache.empty() && retainedBytes()+required>budget) cache.erase(cache.begin());
            auto field=build(world,request,key);
            cache.push_back(std::move(field)); found=cache.end()-1;
        } else ++metrics.cacheHits;
        const auto old=found->costs[here];
        result.originCost=old;
        if (old==0) result.disposition=Disposition::GoalReached;
        else if (old==Unknown) { result.disposition=Disposition::NoRoute; if(found->truncated)++metrics.unknown; }
        else result.disposition=Disposition::Travel;
        if (found->destinations[here]!=Unknown) {
            const auto goal=found->destinations[here];
            Destination destination{int(goal%world.width),int(goal/world.width),{}};
            const auto slot=world.occupancy->cells[goal].building;
            if(slot!=0xffff) for(const auto& b:world.entities->buildings) if(b.identity.gid==slot) { destination.supplier=b.identity;break; }
            result.destination=destination;
        }
        for (unsigned i=0;i<8;++i) {
            const auto d=Directions[i]; const auto next=view.index(request.task.x+d[0],request.task.y+d[1]);
            const auto remaining=found->costs[next];
            if(remaining==Unknown || !legal(view,request.task,request.task.x+d[0],request.task.y+d[1],false))continue;
            const unsigned group=remaining<old?0u:remaining==old?1u:2u;
            ranks[i]={group,remaining+step(world,request.task,next,d[0]&&d[1])};
            result.stepCosts[i]=ranks[i].second;
        }
    }
    // Only preferred groups sort by cost. The rest retains randomized order,
    // even if a backward neighbor happens to have a cheaper known route.
    std::stable_sort(result.directions.begin(),result.directions.end(),[&](auto a,auto b) {
        const auto ra=ranks[a].first<2?ranks[a]:std::pair<unsigned,unsigned>{2,0};
        const auto rb=ranks[b].first<2?ranks[b]:std::pair<unsigned,unsigned>{2,0};
        if(ra!=rb)return ra<rb;
        // The reference gradient kernel resolves improving-cost ties in index
        // order. Sidesteps and the unused suffix retain private randomness.
        if(result.steering==Steering::Route && ra.first==0)return a<b;
        if(result.steering==Steering::Geometric && geometricHeading<8) {
            constexpr unsigned order[8]={0,1,3,5,7,6,4,2};
            return order[(a+8-geometricHeading)%8]<order[(b+8-geometricHeading)%8];
        }
        if(patrol && ra.first==0)return (a+8-request.task.heading%8)%8<(b+8-request.task.heading%8)%8;
        return false;
    });
    for(auto d:result.directions) if(ranks[d].first<2)++result.preferred;
    for(unsigned d=0;d<8;++d) {
        const auto v=Directions[d];
        if(legal(view,request.task,request.task.x+v[0],request.task.y+v[1],false))result.structuralMask|=1u<<d;
        const auto& cell=view.occupancy[view.index(request.task.x+v[0],request.task.y+v[1])];
        const auto occupant=request.task.fly?cell.airUnit:cell.groundUnit;
        if(occupant!=0xffff && occupant!=request.task.unit.gid)result.occupiedMask|=1u<<d;
    }
    return result;
}
}
