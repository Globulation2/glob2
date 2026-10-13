// SPDX-License-Identifier: GPL-3.0-or-later
#include "TurretTargeting.h"
#include "Building.h"
#include "Game.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include "TerrainLine.h"
#include <algorithm>
#include <map>
#include <tuple>
#include <stdexcept>
#include <atomic>
#include <ctime>

namespace TurretTargeting
{
namespace { thread_local AttemptObserver attemptObserver=nullptr; thread_local void* attemptContext=nullptr; }
ScopedAttemptObserver::ScopedAttemptObserver(AttemptObserver observer,void* context)
    : previous(attemptObserver),previousContext(attemptContext)
{ attemptObserver=observer; attemptContext=context; }
ScopedAttemptObserver::~ScopedAttemptObserver()
{ attemptObserver=previous; attemptContext=previousContext; }
void observeAttempt(const Building& turret)
{ if (attemptObserver) attemptObserver(attemptContext,turret); }
struct IndexBudget { std::atomic<std::size_t> bytes{0}; };
struct OccupiedIndex
{
    struct Cell { int x,y; };
    struct Chunk { Uint32 teams=0; std::vector<Cell> cells; };
    Uint64 worldIdentity=0;
    int width=0,height=0;
    Uint32 teams=0;
    unsigned retainedChunks=0;
    std::vector<Uint64> stamps;
    std::vector<std::shared_ptr<const Chunk>> chunks;
};
namespace
{
constexpr std::size_t IndexLimit=32u*1024u*1024u;
template<class T> std::shared_ptr<T> retain(std::unique_ptr<T> value,std::size_t bytes,const std::shared_ptr<IndexBudget>& budget)
{
    auto used=budget->bytes.load();
    do { if (bytes>IndexLimit-used) return {}; }
    while (!budget->bytes.compare_exchange_weak(used,used+bytes));
    return {value.release(),[budget,bytes](T* p){delete p; budget->bytes.fetch_sub(bytes);}};
}
void fillChunk(OccupiedIndex::Chunk& chunk,const MapState::View& view,std::size_t index)
{
    chunk.teams=0; chunk.cells.clear();
    const int chunksWide=(view.width+15)/16;
    const int x0=int(index%chunksWide)*16,y0=int(index/chunksWide)*16;
    for (int y=y0;y<std::min(y0+16,view.height);++y)
        for (int x=x0;x<std::min(x0+16,view.width);++x) {
            const auto& c=view.occupancy[std::size_t(y)*view.width+x];
            if ((c.groundUnit & c.airUnit & c.building)==0xffff) continue;
            chunk.cells.push_back({x,y});
            for (Uint16 gid : {c.groundUnit,c.airUnit,c.building})
                if (gid!=0xffff) chunk.teams|=1u<<Unit::GIDtoTeam(gid);
        }
}
std::shared_ptr<const OccupiedIndex> buildIndex(Observation& o,const std::shared_ptr<const OccupiedIndex>& old)
{
    const auto& w=o.world;
    const bool reusable=old && old->worldIdentity==w.worldIdentity && old->width==w.width && old->height==w.height;
    // Identical occupancy already has an immutable index. Scalar entity facts
    // are still captured below; no chunk pointers or stamp arrays need rebuilding.
    if (reusable && old->stamps==w.occupancy->stamps.chunks) {
        o.sharedChunks=old->retainedChunks;
        return old;
    }
    MapState::ChunkGeometry geometry;
    geometry.reset(w.width,w.height,std::countr_zero(unsigned(w.width)),w.width-1);
    const auto estimate=sizeof(OccupiedIndex)+geometry.count()*(sizeof(std::shared_ptr<const OccupiedIndex::Chunk>)+sizeof(Uint64))+128;
    if (estimate>IndexLimit-o.indexBudget->bytes.load()) return {};
    auto pending=std::make_unique<OccupiedIndex>();
    pending->worldIdentity=w.worldIdentity; pending->width=w.width; pending->height=w.height;
    pending->stamps=w.occupancy->stamps.chunks;
    pending->chunks.resize(geometry.count());
    const auto bytes=sizeof(OccupiedIndex)+pending->chunks.capacity()*sizeof(pending->chunks[0])+pending->stamps.capacity()*sizeof(Uint64)+128;
    auto index=retain(std::move(pending),bytes,o.indexBudget);
    if (!index) return {};
    for (std::size_t i=0;i<geometry.count();++i) {
        if (reusable && i<old->stamps.size() && old->chunks[i] && old->stamps[i]==index->stamps[i]) {
            index->chunks[i]=old->chunks[i]; index->teams|=old->chunks[i]->teams; ++index->retainedChunks; ++o.sharedChunks; continue;
        }
        ++o.rebuiltChunks;
        auto chunk=std::make_unique<OccupiedIndex::Chunk>();
        fillChunk(*chunk,w.view(),i);
        index->teams|=chunk->teams;
        const auto bytes=sizeof(OccupiedIndex::Chunk)+chunk->cells.capacity()*sizeof(OccupiedIndex::Cell)+128;
        index->chunks[i]=retain(std::move(chunk),bytes,o.indexBudget);
        if (index->chunks[i]) ++index->retainedChunks;
        // Unretained chunks are rebuilt temporarily by the same indexed traversal.
    }
    return index;
}
struct UnitFact { Entry entry; int x, y, hp, type, ticks, priority, offense, attackSpeed; bool alive, flying; };
struct BuildingFact { Entry entry; int x, y, width, height; bool alive; };
struct Live
{
    const Building& turret;
    Map& map() const { return *turret.owner->map; }
    int width() const { return map().getW(); }
    int height() const { return map().getH(); }
    MapState::View cells() const { return map().cellView(); }
    const OccupiedIndex::Chunk* retainedChunk(std::size_t) const { return nullptr; }
    bool hasEligibleCells(Uint32) const { return true; }
    auto occupancy(int x,int y) const { return map().cellView().occupancy[map().coordToIndex(x,y)]; }
    UnitFact unit(Uint16 gid) const {
        const auto* u=turret.owner->game->teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
        return {{Kind::Unit,gid,u->scriptIdentity},u->posX,u->posY,u->hp,u->typeNum,
            u->speed>0 ? (256-u->delta)/u->speed : INT_MAX,
            u->runtimeTraits().turretPriority,u->getRealAttackStrength(),u->performance[ATTACK_SPEED],!u->isDead && u->hp>0,u->performance[FLY]!=0};
    }
    BuildingFact building(Uint16 gid) const {
        const auto* b=turret.owner->game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
        return {{Kind::Building,gid,b->scriptIdentity},b->posX,b->posY,b->runtime->width,b->runtime->height,b->buildingState!=Building::DEAD && b->hp>0};
    }
    bool clear(int x,int y,int,int,int,int) const { return turret.hasClearShotTo(x,y); }
};
struct Frozen
{
    const Observation& observation;
    MapState::View view;
    int width() const { return view.width; }
    int height() const { return view.height; }
    MapState::View cells() const { return view; }
    bool hasEligibleCells(Uint32 teams) const { return !observation.index || (observation.index->teams & teams); }
    const OccupiedIndex::Chunk* retainedChunk(std::size_t i) const {
        return observation.index ? observation.index->chunks[i].get() : nullptr;
    }
    auto occupancy(int x,int y) const { return view.occupancy[view.index(x,y)]; }
    UnitFact unit(Uint16 gid) const {
        const auto& entities=*observation.world.entities;
        const auto slot=Unit::GIDtoTeam(gid)*Unit::MAX_COUNT+Unit::GIDtoID(gid);
        const auto index=entities.unitSlotIndices.at(slot);
        const auto& u=entities.units.at(index);
        return {{Kind::Unit,gid,u.identity.generation},u.posX,u.posY,u.hp,u.typeNum,
            u.speed>0 ? (256-u.delta)/u.speed : INT_MAX,
            observation.world.catalogs->units->runtime(u.typeNum).turretPriority,
            observation.offense.at(index),u.performance[ATTACK_SPEED],!u.isDead && u.hp>0,u.performance[FLY]!=0};
    }
    BuildingFact building(Uint16 gid) const {
        const auto& entities=*observation.world.entities;
        const auto slot=Building::GIDtoTeam(gid)*Building::MAX_COUNT+Building::GIDtoID(gid);
        const auto& b=entities.buildings.at(entities.buildingSlotIndices.at(slot));
        const auto& type=observation.world.catalogs->typeDefinitions->at(b.typeNum);
        return {{Kind::Building,gid,b.identity.generation},b.posX,b.posY,type.width,type.height,b.buildingState!=Building::DEAD && b.hp>0};
    }
    bool clear(int x,int y,int ox,int oy,int sx,int sy) const {
        if (!observation.blocksProjectiles) return true;
        if (std::abs(Sint64(sx)-ox)>Sint64(width())*32 || std::abs(Sint64(sy)-oy)>Sint64(height())*32) return false;
        if (view.terrainProperties(view.index(x,y)).projectileBlocks) return false;
        return terrainSegmentClear(ox,oy,sx,sy,[&](std::int64_t cx,std::int64_t cy) {
            return view.terrainProperties(view.index(int(cx),int(cy))).projectileBlocks;
        });
    }
};
struct Shot { int ox,oy,sx,sy,ticks; bool operator==(const Shot&) const = default; };
template<class World> Shot shot(const Source& s,const World& w,int x,int y)
{
    Shot result{s.x*32+s.width*16,s.y*32+s.height*16,0,0,0};
    int dx=x*32+12-result.ox,dy=y*32+12-result.oy;
    if (dx>w.width()*16) dx-=w.width()*32;
    if (dx<-w.width()*16) dx+=w.width()*32;
    if (dy>w.height()*16) dy-=w.height()*32;
    if (dy<-w.height()*16) dy+=w.height()*32;
    if (!dx && !dy) return result;
    const int distance=std::max(std::abs(dx),std::abs(dy));
    result.sx=int(Sint64(dx)*s.speed/(Sint64(distance)<<8));
    result.sy=int(Sint64(dy)*s.speed/(Sint64(distance)<<8));
    result.ticks=std::abs(distance/(std::abs(dx)>std::abs(dy) ? result.sx : result.sy));
    return result;
}
int score(const Source& s,const UnitFact& u,int ring)
{
    if (u.priority!=3) return -u.hp;
    const int damage=unsigned(u.type)<s.damageCount() ? s.damageAt(u.type) : 0;
    const int weakness=u.hp>0 ? (u.hp<damage ? 512 : 256/u.hp) : 0;
    return int(std::clamp<Sint64>(Sint64(u.offense)*u.attackSpeed+weakness+(ring<=0 ? 512 : 256/ring),0,INT_MAX));
}
bool eligible(Uint16 gid,Uint32 mask) { return gid!=0xffff && (mask & (1u<<Unit::GIDtoTeam(gid))); }
struct Candidate { Target target; unsigned tier; int distance; Uint64 ordinal; bool clear; };
auto key(const Candidate& c) {
    return std::tuple{c.tier,c.tier==2 ? c.distance : 0,c.target.entry.kind==Kind::Building,
        -Sint64(c.target.score),c.ordinal,c.target.entry.gid,c.target.entry.incarnation,int(c.target.entry.kind)};
}
// One occupied-cell traversal for every footprint and wrapped representative.
// Canonical ordinals reproduce the original selector's encounter order without
// walking its empty cells. Stock octant corner duplicates remain distinct.
template<class World,class Visit> void traverse(const Source& s,const World& world,unsigned margin,Metrics& metrics,Visit visit)
{
    const Uint32 teams=s.unitTeams | (s.buildingDamage>0 ? s.buildingTeams : 0);
    if (!teams || !world.hasEligibleCells(teams)) return;
    const int end=s.range+int(margin);
    const int left=s.x-end,right=s.x+s.width-1+end,top=s.y-end,bottom=s.y+s.height-1+end;
    const auto view=world.cells();
    const int chunksWide=(view.width+15)/16,chunksHigh=(view.height+15)/16;
    struct Tile { int x,y,ring; Uint64 ordinal; };
    std::vector<Tile> tiles;
    OccupiedIndex::Chunk temporary;
    struct ChunkSpan { int first,count; };
    const auto chunks=[](int low,int high,int period) {
        const int count=(period+15)/16;
        const Sint64 length=Sint64(high)-low+1;
        if (length>=period) return ChunkSpan{0,count};
        const int canonical=low&(period-1);
        // A nearly full wrapped interval can touch its starting chunk twice.
        // Clamp the count so each canonical chunk is built/queried only once.
        return ChunkSpan{canonical/16,std::min(count,1+int((canonical%16+length-1)/16))};
    };
    const auto xs=chunks(left,right,view.width),ys=chunks(top,bottom,view.height);
    for (int row=0;row<ys.count;++row) for (int column=0;column<xs.count;++column) {
        const int cy=(ys.first+row)&(chunksHigh-1),cx=(xs.first+column)&(chunksWide-1);
        ++metrics.queriedChunks;
        const auto index=std::size_t(cy)*chunksWide+cx;
        const auto* chunk=world.retainedChunk(index);
        if (!chunk) { ++metrics.temporaryChunks; fillChunk(temporary,view,index); chunk=&temporary; }
        if (!(chunk->teams & teams)) continue;
        for (const auto& cell:chunk->cells) {
            const auto& occupancy=view.occupancy[std::size_t(cell.y)*view.width+cell.x];
            if (!eligible(occupancy.groundUnit,s.unitTeams) && !eligible(occupancy.airUnit,s.unitTeams)
                && !(s.buildingDamage>0 && eligible(occupancy.building,s.buildingTeams))) continue;
            const int firstX=left+((cell.x-left)&(view.width-1));
            const int firstY=top+((cell.y-top)&(view.height-1));
            for (int y=firstY;y<=bottom;y+=view.height) for (int x=firstX;x<=right;x+=view.width) {
                const int dx=x-s.x,dy=y-s.y;
                if (s.width==2 && s.height==2) {
                    const int rings[]={-dy,-dy,dy-1,dy-1,-dx,dx-1,-dx,dx-1};
                    const int offsets[]={-dx,dx-1,-dx,dx-1,-dy,-dy,dy-1,dy-1};
                    for (unsigned octant=0;octant<8;++octant) {
                        const int ring=rings[octant],offset=offsets[octant];
                        if (ring<0 || ring>end || offset<0 || offset>ring) continue;
                        tiles.push_back({x,y,ring,4*Uint64(ring)*(ring+1)+8*Uint64(offset)+octant});
                    }
                } else {
                    const int ring=std::max({0,-dx,dx-s.width+1,-dy,dy-s.height+1});
                    Uint64 ordinal;
                    if (!ring) ordinal=Uint64(dy)*s.width+dx;
                    else {
                        const int width=s.width+2*ring;
                        const int ringLeft=s.x-ring,ringTop=s.y-ring,ringBottom=s.y+s.height-1+ring;
                        ordinal=Uint64(s.width)*s.height+Uint64(ring-1)*(2*Uint64(s.width)+2*Uint64(s.height)-4)+4*Uint64(ring)*(ring-1);
                        if (y==ringTop || y==ringBottom) ordinal+=2*Uint64(x-ringLeft)+(y==ringBottom);
                        else ordinal+=2*Uint64(width)+2*Uint64(y-ringTop-1)+(x!=ringLeft);
                    }
                    tiles.push_back({x,y,ring,ordinal});
                }
            }
        }
    }
    std::sort(tiles.begin(),tiles.end(),[](const auto& a,const auto& b){return a.ordinal<b.ordinal;});
    for (const auto& tile:tiles) visit(tile.x,tile.y,tile.ring,
        s.width==2 && s.height==2 ? ((tile.ring<<5)+32)/(s.speed>>8) : -1,tile.ordinal);
}
template<class World> Plan run(const Source& s,const World& world,unsigned capacity,unsigned margin,bool compactOutputs=false)
{
    if (s.width<=0 || s.height<=0 || s.speed<256 || s.range<0 || margin>2 || (capacity && capacity!=2 && capacity!=3 && capacity!=4 && capacity!=8 && capacity!=16 && capacity!=32))
        throw std::invalid_argument("Invalid turret planner configuration");
    const Sint64 end=Sint64(s.range)+margin;
    constexpr Sint64 coordinateLimit=INT_MAX/32;
    if (s.x<0 || s.y<0 || s.x>=world.width() || s.y>=world.height()
        || Sint64(s.x)-end < -coordinateLimit || Sint64(s.y)-end < -coordinateLimit
        || Sint64(s.x)+s.width+end > coordinateLimit || Sint64(s.y)+s.height+end > coordinateLimit)
        throw std::invalid_argument("Turret planner geometry exceeds safe pixel coordinates");
    Plan result;
    std::map<Uint64,Candidate> candidates;
    int previousRing=-1;
    bool stopped=false;
    traverse(s,world,margin,result.metrics,[&](int x,int y,int ring,int ringTicks,Uint64 encounter) {
        ++result.metrics.visits;
        if (ring!=previousRing) { stopped |= result.preferred.priority==4; previousRing=ring; }
        const bool fold=!stopped && ring<=s.range;
        const auto cell=world.occupancy(x,y);
        const bool ground=eligible(cell.groundUnit,s.unitTeams),air=eligible(cell.airUnit,s.unitTeams);
        const bool building=ring<=s.range && s.buildingDamage>0 && eligible(cell.building,s.buildingTeams);
        if (!ground && !air && !building) return;
        ++result.metrics.occupied;
        const auto geometry=shot(s,world,x,y);
        if (ringTicks<0) ringTicks=geometry.ticks;
        ++result.metrics.clearance;
        const bool clear=world.clear(x,y,geometry.ox,geometry.oy,
            geometry.ox+geometry.sx*geometry.ticks,geometry.oy+geometry.sy*geometry.ticks);
        bool tileReturned=!clear;
        const auto apply=[&](const Target& t) { if (t.score>result.preferred.score) result.preferred=t; };
        const auto collect=[&](Candidate candidate) {
            const auto& e=candidate.target.entry;
            // Exact tagged identity in one integer comparison. Its numeric order
            // equals the old (building, gid, incarnation) lexicographic order.
            const Uint64 identity=(Uint64(e.kind==Kind::Building)<<48)|(Uint64(e.gid)<<32)|e.incarnation;
            auto [it,inserted]=candidates.try_emplace(identity,candidate);
            if (!inserted && e.kind==Kind::Building) {
                // Preserve the earliest clear tile, even when arrival rejects it.
                if (!it->second.clear && candidate.clear) it->second=candidate;
            }
        };
        const auto unit=[&](Uint16 gid,bool isGround) {
            if (!eligible(gid,s.unitTeams)) return;
            const auto u=world.unit(gid);
            if (unsigned(u.type)>=s.damageCount() || s.damageAt(u.type)<=0) {
                if (fold && !tileReturned && u.ticks<=ringTicks) tileReturned=true;
                return;
            }
            ++result.metrics.scored;
            Target t{u.entry,x,y,u.ticks,score(s,u,ring),u.priority};
            const bool occupies=((x&(world.width()-1))==(u.x&(world.width()-1))) &&
                ((y&(world.height()-1))==(u.y&(world.height()-1))) && u.flying!=isGround;
            const auto current=shot(s,world,u.x,u.y);
            bool currentClear=clear;
            if (occupies && current!=geometry) {
                ++result.metrics.clearance;
                currentClear=world.clear(u.x,u.y,current.ox,current.oy,
                    current.ox+current.sx*current.ticks,current.oy+current.sy*current.ticks);
            }
            collect({t,unsigned(ring>s.range ? 2 : occupies && u.alive && currentClear && current.ticks<u.ticks ? 0 : 1),ring,encounter,currentClear});
            if (fold && !tileReturned) {
                if (u.ticks<=ringTicks) { tileReturned=true; return; }
                if (!isGround || u.priority!=2 || result.preferred.priority!=3) apply(t);
            }
        };
        if (ground) unit(cell.groundUnit,true);
        if (air) unit(cell.airUnit,false);
        if (building) {
            auto b=world.building(cell.building);
            const auto offset=[](int tile,int origin,int extent,int period) {
                const int relative=tile-origin;
                // Large footprints can cover the same canonical cell twice.
                // Keep an encountered tile already inside the actual footprint.
                return relative>=0 && relative<extent ? relative : relative&(period-1);
            };
            b.entry.offsetX=offset(x,b.x,b.width,world.width());
            b.entry.offsetY=offset(y,b.y,b.height,world.height());
            Target t{b.entry,x,y,256,-ring,1};
            collect({t,unsigned(b.alive && clear && geometry.ticks<256 ? 0 : 1),ring,encounter,clear});
            if (fold && !tileReturned && result.preferred.priority==0) apply(t);
        }
    });
    if (compactOutputs) result.entries.reserve(capacity ? std::min<std::size_t>(capacity,candidates.size()) : candidates.size());
    if (result.preferred.found()) result.entries.push_back(result.preferred.entry);
    std::vector<Candidate> top;
    const auto limit=capacity ? capacity-result.entries.size() : candidates.size();
    top.reserve(std::min(limit,candidates.size()-result.entries.size()));
    for (const auto& [identity,c] : candidates) {
        const auto& e=c.target.entry;
        const auto& head=result.preferred.entry;
        if (head.kind!=Kind::None && (head.kind==Kind::Building)==(e.kind==Kind::Building) && head.gid==e.gid && head.incarnation==e.incarnation) continue;
        const auto pos=std::lower_bound(top.begin(),top.end(),c,[](const auto& a,const auto& b){return key(a)<key(b);});
        if (std::size_t(pos-top.begin())>=limit) continue;
        top.insert(pos,c);
        if (top.size()>limit) top.pop_back();
    }
    for (const auto& c:top) result.entries.push_back(c.target.entry);
    return result;
}
}

Source source(const Building& b,std::shared_ptr<const std::vector<int>> damage)
{
    Source s{Game::refOf(&b),b.posX,b.posY,b.runtime->width,b.runtime->height,
        b.runtime->shootingRange,b.runtime->shootSpeed,
        b.owner->attackableTeams() & ~b.owner->sharedVisionExchange,
        b.owner->attackableTeams(),b.runtime->projectileBuildingDamage,{}};
    s.sharedDamage=std::move(damage);
    if (!s.sharedDamage) {
        s.damage.reserve(b.runtime->unitCount);
        for (unsigned id=0;id<b.runtime->unitCount;++id) s.damage.push_back(b.runtime->damage(id));
    }
    return s;
}
void prepareIndex(Observation& o,const std::shared_ptr<const OccupiedIndex>& previous,bool profileCpu)
{
    const auto start=profileCpu ? threadCpuNs() : 0;
    o.index=buildIndex(o,previous);
    if (profileCpu) o.indexCpu=threadCpuNs()-start;
}
Observation observe(const Game& game,const SimulationSnapshot::Handle& captured,const Observation* previous,bool capturedFacts,bool profileCpu,bool deferIndex)
{
    if (captured.tick!=game.stepCounter || captured.worldIdentity!=game.map.identity())
        throw std::invalid_argument("Turret observation is not the completed-world boundary");
    Observation o;
    o.compactOutputs=capturedFacts;
    o.world=captured.project(capturedFacts ? targetingRequirements : requirements);
    o.blocksProjectiles=game.map.hasProjectileBlockingTerrain();
    o.indexBudget=previous && previous->indexBudget ? previous->indexBudget : std::make_shared<IndexBudget>();
    if (!deferIndex) prepareIndex(o,previous ? previous->index : nullptr,profileCpu);
    const auto factsStart=profileCpu ? threadCpuNs() : 0;
    o.offense.reserve(o.world.entities->units.size());
    for (const auto& u:o.world.entities->units) {
        if (capturedFacts) {
            const int strength=int(std::clamp<Sint64>((Sint64(u.performance[ATTACK_STRENGTH])+u.experienceLevel)*game.gameHeader.getGlassCannonScale(),0,INT_MAX));
            if (!game.areaEffects.enabled()) o.offense.push_back(strength);
            else {
                const auto* live=game.resolveUnit(u.identity);
                if (!live) throw std::logic_error("Turret observation boundary changed");
                o.offense.push_back(live->applyAreaAttack(strength));
            }
        } else {
            const auto* live=game.resolveUnit(u.identity);
            if (!live) throw std::logic_error("Turret observation boundary changed");
            o.offense.push_back(live->getRealAttackStrength());
        }
    }
    if (profileCpu) o.factsCpu=threadCpuNs()-factsStart;
    return o;
}
Observation observe(const Game& game,const Observation* previous)
{
    // Fixtures may mutate scalar facts without advancing time. Announce that
    // observation explicitly; production passes the existing boundary instead.
    game.snapshots().invalidateBoundary();
    return observe(game,game.captureReadBoundary({},true,requirements),previous);
}
std::size_t retainedIndexBytes(const Observation& o) { return o.indexBudget ? o.indexBudget->bytes.load() : 0; }
Plan plan(const Building& b,unsigned capacity,unsigned margin) { return run(source(b),Live{b},capacity,margin); }
Plan plan(const Source& s,const Observation& o,unsigned capacity,unsigned margin)
{
    if ((o.world.requirements & targetingRequirements)!=targetingRequirements || !o.world.entities || !o.world.catalogs || !o.world.occupancy || !o.world.terrain || !o.world.teams)
        throw std::invalid_argument("Incomplete turret observation");
    return run(s,Frozen{o,o.world.view()},capacity,margin,o.compactOutputs);
}
Target firstValid(const Building& b,const std::vector<Entry>& entries,Metrics* metrics)
{
    if (b.runtime->shootSpeed<256) return {};
    const Source s{Game::refOf(&b),b.posX,b.posY,b.runtime->width,b.runtime->height,
        b.runtime->shootingRange,b.runtime->shootSpeed,
        b.owner->attackableTeams() & ~b.owner->sharedVisionExchange,
        b.owner->attackableTeams(),b.runtime->projectileBuildingDamage,{}};
    const Live world{b};
    for (const auto& e:entries) {
        if (metrics) ++metrics->examined;
        const auto reject=[&](Rejection reason) -> Target {
            if (metrics) ++metrics->rejectionReasons[unsigned(reason)];
            return {};
        };
        const auto tryEntry=[&]() -> Target {
            int x,y,ticks=256;
            if (e.kind==Kind::Building) {
                if (e.gid>=Building::MAX_COUNT*Team::MAX_COUNT) return reject(Rejection::Identity);
                const auto* target=b.owner->game->resolveBuilding({e.gid,e.incarnation});
                if (!target || target->buildingState==Building::DEAD || target->hp<=0) return reject(Rejection::Identity);
                if (!eligible(e.gid,s.buildingTeams)) return reject(Rejection::Team);
                if (s.buildingDamage<=0) return reject(Rejection::Damage);
                if (e.offsetX<0 || e.offsetY<0 || e.offsetX>=target->runtime->width || e.offsetY>=target->runtime->height) return reject(Rejection::Aim);
                x=target->posX+e.offsetX; y=target->posY+e.offsetY;
                if (world.occupancy(x,y).building!=e.gid) return reject(Rejection::Occupancy);
            } else if (e.kind==Kind::Unit) {
                if (e.gid>=Unit::MAX_COUNT*Team::MAX_COUNT) return reject(Rejection::Identity);
                const auto* target=b.owner->game->resolveUnit({e.gid,e.incarnation});
                if (!target || target->isDead || target->hp<=0) return reject(Rejection::Identity);
                if (!eligible(e.gid,s.unitTeams)) return reject(Rejection::Team);
                if (b.runtime->damage(target->typeNum)<=0) return reject(Rejection::Damage);
                x=target->posX; y=target->posY;
                const auto cell=world.occupancy(x,y);
                if ((target->performance[FLY] ? cell.airUnit : cell.groundUnit)!=e.gid) return reject(Rejection::Occupancy);
                ticks=target->speed>0 ? (256-target->delta)/target->speed : INT_MAX;
            } else return reject(Rejection::Kind);
            const auto distance=[](int tile,int start,int size,int period) {
                int best=INT_MAX;
                for (int shift : {-period,0,period}) {
                    const int relative=tile+shift-start;
                    best=std::min(best,relative<0 ? -relative : relative>=size ? relative-size+1 : 0);
                }
                return best;
            };
            if (std::max(distance(x,s.x,s.width,world.width()),distance(y,s.y,s.height,world.height()))>s.range) return reject(Rejection::Range);
            const auto geometry=shot(s,world,x,y);
            if (geometry.ticks>=ticks) return reject(Rejection::Arrival);
            if (!b.hasClearShotTo(x,y)) return reject(Rejection::Clearance);
            return {e,x,y,ticks,INT_MIN,0};
        };
        auto target=tryEntry();
        if (target.found()) return target;
        if (metrics) ++metrics->rejected;
    }
    return {};
}
Uint64 threadCpuNs()
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)!=0) throw std::runtime_error("Thread CPU clock unavailable");
    return Uint64(value.tv_sec)*1000000000u+value.tv_nsec;
#else
    throw std::runtime_error("Turret CPU profiling requires a thread CPU clock on this platform");
#endif
}
bool refreshNeeded(int cooldown,unsigned rhythm,unsigned delay,unsigned refresh)
{
    // At completed boundary t, the next turretStep still uses counter t.
    // The last attempt served by this generation is t+delay+refresh-1.
    const Uint64 last=Uint64(delay)+refresh-1;
    if (cooldown<=0) return true;
    if (!rhythm) return last==std::numeric_limits<Uint64>::max();
    // ceil(cooldown / rhythm) <= last without division. The first comparison
    // also bounds the multiplication for callers passing large intervals.
    return last>=Uint64(cooldown) || Uint64(cooldown)<=last*rhythm;
}
Uint64 stepsUntilReady(int cooldown,unsigned rhythm)
{
    if (cooldown<=0) return 0;
    if (!rhythm) return std::numeric_limits<Uint64>::max();
    return (Uint64(cooldown)+rhythm-1)/rhythm;
}
}
