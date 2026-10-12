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
const SimulationSnapshot::BuildingView* building(const SimulationSnapshot::Handle& world, BuildingRef ref)
{
    if (ref.empty()) return nullptr;
    for (const auto& b : world.entities->buildings) if (b.identity == ref) return &b;
    return nullptr;
}
}
Task taskOf(const SimulationSnapshot::UnitView& u)
{
    return {u.identity,{},u.attached,u.target,u.team,u.posX,u.posY,u.targetX,u.targetY,u.destinationPurpose,
        u.activity,u.medical,u.displacement,u.jobPurpose,u.capabilityFlags,u.performance[WALK],u.performance[SWIM],u.performance[FLY]};
}
std::optional<Mode> modeOf(const UnitState& u)
{
    if ((!u.performance[WALK] && !u.performance[SWIM] && !u.performance[FLY]) || u.isDead || u.displacement==UnitState::DIS_INSIDE || u.displacement==UnitState::DIS_ENTERING_BUILDING
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
std::optional<Uint32> completionTicks(const UnitState& u)
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
bool legal(const SimulationSnapshot::Handle& world, const Task& task, int x, int y, bool occupancy)
{
    const auto view = world.view();
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
        if (!legal(world,r.task,x,y,false)) return;
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
            if (!legal(world,r.task,nx,ny,false)) continue;
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
    if (request.mode==Mode::Explore) {
        if (!world.exploration || world.exploration->teams.at(request.task.team).empty())
            throw std::invalid_argument("exploration planner needs captured exploration");
        const auto& explored=world.exploration->teams[request.task.team];
        Sint64 sx=0,sy=0;
        for (int range=1;range<=std::min(world.width,world.height)/2;range*=2)
            for (int d=-3;d<=3;++d) {
                sx+=explored[view.index(request.task.x-4*range,request.task.y+d*range)]-explored[view.index(request.task.x+4*range,request.task.y+d*range)];
                sy+=explored[view.index(request.task.x+d*range,request.task.y-4*range)]-explored[view.index(request.task.x+d*range,request.task.y+4*range)];
            }
        targetX=view.normalizeX(request.task.x+(sx>0)-(sx<0));
        targetY=view.normalizeY(request.task.y+(sy>0)-(sy<0));
        result.destination=Destination{targetX,targetY,{}};
    }
    const bool geometric=request.mode==Mode::Explore || (request.task.fly && !world.terrain->airConstraints
        && request.mode!=Mode::Resource && request.mode!=Mode::Clearing && request.mode!=Mode::Idle);
    const bool wander=request.mode==Mode::Idle && request.task.displacement!=UnitState::DIS_ATTACKING_AROUND;
    std::array<std::pair<unsigned,unsigned>,8> ranks;
    ranks.fill({2,Unknown});
    if (wander || geometric) {
        const auto oldDistance=distance(request.task.x,request.task.y,targetX,targetY,world.width,world.height);
        for (unsigned i=0;i<8;++i) {
            const auto d=Directions[i]; const int x=view.normalizeX(request.task.x+d[0]), y=view.normalizeY(request.task.y+d[1]);
            if (!legal(world,request.task,x,y,false)) continue;
            const auto remaining=unsigned(distance(x,y,targetX,targetY,world.width,world.height));
            if (wander) {
                const auto& terrain=view.terrainProperties(view.index(x,y));
                const bool hazard=(request.task.fly ? terrain.airHealthQ8 : terrain.groundHealthQ8)<0;
                const bool tower=request.task.fly && towerThreat(world,request.task,x,y);
                const bool guard=world.areas->cells[here].guard & (1u<<request.task.team);
                const bool leavesGuard=guard && !(world.areas->cells[view.index(x,y)].guard & (1u<<request.task.team));
                ranks[i]={hazard?2u:tower || leavesGuard?1u:0u,0};
            } else {
                if(request.mode==Mode::Explore && towerThreat(world,request.task,x,y))continue;
                ranks[i]={remaining<unsigned(oldDistance)?0u:remaining==unsigned(oldDistance)?1u:2u,remaining};
            }
        }
        result.disposition=geometric && oldDistance==0 ? Disposition::GoalReached : Disposition::Travel;
    } else if (request.mode==Mode::Resource && world.resourceFields
        && request.task.material>=0 && request.task.material<int(MaterialCount)
        && world.resourceFields->find(MapState::planeKey(request.task.team,request.task.material,swimClass(request.task),false))
        && (!building(world,request.task.attached) ||
            !(world.catalogs->typeDefinitions->at(building(world,request.task.attached)->typeNum).runtimeFetchesStockMask
              | world.catalogs->typeDefinitions->at(building(world,request.task.attached)->typeNum).runtimeFetchesDirectStockMask))) {
        const auto& plane=*world.resourceFields->find(MapState::planeKey(request.task.team,request.task.material,swimClass(request.task),false))->values;
        const auto old=plane[here];
        result.disposition=old==GRADIENT_AT_GOAL?Disposition::GoalReached:
            old<=GRADIENT_UNREACHABLE?Disposition::NoRoute:Disposition::Travel;
        ++metrics.cacheHits; // borrowed, immutable published plane; no lazy expansion
        if(MapState::hasMaterialSlot(view,view.index(targetX,targetY),request.task.material))
            result.destination=Destination{targetX,targetY,{}};
        for(unsigned i=0;i<8;++i) {
            const auto d=Directions[i]; const auto next=view.index(request.task.x+d[0],request.task.y+d[1]);
            const auto value=plane[next];
            if(value<=GRADIENT_UNREACHABLE || !legal(world,request.task,request.task.x+d[0],request.task.y+d[1],false))continue;
            ranks[i]={value>old?0u:value==old?1u:2u,unsigned(GRADIENT_AT_GOAL-value)+step(world,request.task,next,d[0]&&d[1])};
        }
    } else {
        Task sharedTask=request.task;
        sharedTask.unit={}; sharedTask.x=sharedTask.y=0;
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
            if(remaining==Unknown || !legal(world,request.task,request.task.x+d[0],request.task.y+d[1],false))continue;
            const unsigned group=remaining<old?0u:remaining==old?1u:2u;
            ranks[i]={group,remaining+step(world,request.task,next,d[0]&&d[1])};
        }
    }
    // Only preferred groups sort by cost. The rest retains randomized order,
    // even if a backward neighbor happens to have a cheaper known route.
    std::stable_sort(result.directions.begin(),result.directions.end(),[&](auto a,auto b) {
        const auto ra=ranks[a].first<2?ranks[a]:std::pair<unsigned,unsigned>{2,0};
        const auto rb=ranks[b].first<2?ranks[b]:std::pair<unsigned,unsigned>{2,0};
        return ra<rb;
    });
    for(auto d:result.directions) if(ranks[d].first<2)++result.preferred;
    return result;
}
}
