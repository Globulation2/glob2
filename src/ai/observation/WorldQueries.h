// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/observation/AIWorldView.h"
#include "ResourceInitializationCache.h"
#include "ai/engine/AIDecision.h"
#include <set>
#include "Building.h"
#include "BuildingCapabilities.h"
#include "Order.h"
#include "field/RuntimeTerrainGradient.h"
#include "field/Influence.h"
#include <algorithm>
#include <map>
#include <numeric>

namespace AIEngine
{

// Scratch is private to one decision. Query code cannot reach a live Map or Game.
class WorldQueries
{
 using Intent=AIPlanning::BuildingIntent;
 using Building=AIEngine::BuildingView;
 using Kind=AIEngine::BuildingKindView;
 const AIEngine::AIWorldView& world;
 int team;
 ResourceInitializations& resourceFields;
 std::vector<AIEngine::ResourceEnrollmentRequest>* enrollments;
 std::set<int> enrollmentsReported;
 GradientWorkspace scratch;
 struct Reservation { int type,x,y; };
 std::vector<Reservation> reservations;
public:
 WorldQueries(const AIEngine::AIWorldView& world,int team,ResourceInitializations& cache,std::vector<AIEngine::ResourceEnrollmentRequest>* enrollments=nullptr):world(world),team(team),resourceFields(cache),enrollments(enrollments)
 {
  for(const auto& project:world.buildProjects) if(project.teamNumber==team) reserve(project.typeNum,project.posX,project.posY);
 }
 int getW() const { return world.width; }
 int getH() const { return world.height; }
 TileView getTile(int x,int y) const { return world.tile(x,y); }
 Uint16 getBuilding(int x,int y) const { return world.tile(x,y).building; }
 const TerrainProperties& terrainPropertiesAt(int x,int y) const { return world.terrain->properties(world.tile(x,y).terrain); }
 bool isMapDiscovered(int x,int y,Uint32 mask) const { return world.tile(x,y).discovered&mask; }
 bool isFOWDiscovered(int x,int y,Uint32 mask) const { return world.tile(x,y).visible&mask; }
 bool isForbidden(int x,int y,Uint32 mask) const { return world.tile(x,y).forbidden&mask; }
 bool isGuardArea(int x,int y,Uint32 mask) const { return world.tile(x,y).guard&mask; }
 bool isClearArea(int x,int y,Uint32 mask) const { return world.tile(x,y).clear&mask; }
 bool isFarmArea(int x,int y,Uint32 mask) const { return world.tile(x,y).farm&mask; }
 bool farmAreasEnabled() const { return world.farmAreasEnabled; }
 bool canPaintFarmArea(int x,int y) const { return world.tile(x,y).canPaintFarm; }
 bool isHardSpaceForBuilding(int x,int y,int width=1,int height=1,Uint16 ignore=0xffff) const
 { return isFreeForBuilding(x,y,width,height,true,ignore); }
 void updateGlobalGradient(Uint8* values) const
 {
  field::ConvergentInfluence influence(values,{world.width,world.height});
  if(!influence.hasSources()) return;
  int passes=0;
  do {
   influence.beginPass();
   for(int y=0;y<world.height;++y) influence.forwardRow(y);
   for(int y=world.height;y-->0;) influence.reverseRow(y);
   if(++passes>=256) throw std::logic_error("AI influence gradient failed to converge");
  } while(influence.passChanged());
 }
 template<class T> bool getGlobalGradientDestination(const T* values,int x,int y,Sint32* targetX,Sint32* targetY) const
 {
  static constexpr int directions[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
  int vx=world.normalizeX(x),vy=world.normalizeY(y);
  const auto at=[&](int a,int b){return values[world.normalizeY(b)*world.width+world.normalizeX(a)];};
  T strongest=at(vx,vy);
  bool exact=false;
  while(true) {
   bool found=false;int dx=0,dy=0;
   for(const auto& d:directions) if(const T candidate=at(vx+d[0],vy+d[1]);candidate>strongest) {
    strongest=candidate;dx=d[0];dy=d[1];found=true;
   }
   vx=world.normalizeX(vx+dx);vy=world.normalizeY(vy+dy);
   if(strongest==std::numeric_limits<T>::max()) {exact=true;break;}
   if(!found) break;
  }
  *targetX=vx;*targetY=vy;return exact;
 }
 void reserve(int type,int x,int y) { reservations.push_back({type,x,y}); }
 const Kind& kind(int type) const { return world.catalog->at(type); }
 const Kind& kind(const Building& b) const { return kind(b.type); }
 Uint64 rawIntentMask(int type) const { return kind(type).rawCapabilityMask; }
 int lineagePosition(int type) const { return kind(type).lineagePosition; }
 bool matches(int type,Intent intent) const { return kind(type).rawCapabilityMask & (Uint64(1)<<unsigned(intent)); }
 bool available(int type,Intent intent) const { return kind(type).available && (kind(type).capabilityMask & (Uint64(1)<<unsigned(intent))); }
 bool allowed(Intent intent) const
 {
  if(world.rules.hungerDisabled && intent==Intent::Feed) return false;
  if(world.rules.upgradesDisabled && ((intent>=Intent::TrainWalk && intent<Intent::ProjectileDefense) || intent==Intent::TrainConstruction)) return false;
  if(world.rules.peaceful)
   switch(intent) {
    case Intent::ProduceWarrior:case Intent::TrainAttackSpeed:case Intent::TrainAttackStrength:
    case Intent::TrainAirAttack:case Intent::TrainBombing:case Intent::ProjectileDefense:case Intent::AttractWarriors:return false;
    default:break;
   }
  return true;
 }
 bool provides(const Building& b,Intent intent) const
 { const auto& type=kind(b); return matches(type.site?type.next:b.type,intent); }
 std::vector<int> placements(Intent intent,bool costOrder=false) const
 {
  std::vector<int> result;
  for(unsigned i=0;i<world.catalog->size();++i)
   if(kind(i).semantics.placeable && available(i,intent)) result.push_back(i);
  if(costOrder) std::sort(result.begin(),result.end(),[&](int a,int b) {
   const auto cost=[&](int type){const auto& c=kind(type).semantics.constructionCost;return std::accumulate(c.begin(),c.end(),0);};
   return std::pair{cost(a),a}<std::pair{cost(b),b};
  });
  return result;
 }
 bool isResourceTakeable(int x,int y,int resource) const
 { const auto& r=world.tile(x,y).resource; return r.type==resource && r.amount>0; }
 bool isFreeForBuilding(int x,int y,int width=1,int height=1,bool hard=false,Uint16 ignore=0xffff) const
 {
  for(int dy=0;dy<height;++dy) for(int dx=0;dx<width;++dx) {
   const auto& tile=world.tile(x+dx,y+dy);
   if(tile.resource.type!=NO_RES_TYPE || (tile.building!=0xffff && tile.building!=ignore)
     || (!hard && tile.groundUnit!=0xffff) || !world.terrain->properties(tile.terrain).buildable) return false;
  }
  return true;
 }
 bool checkRoomForBuilding(int x,int y,int type,int owner) const
 {
  const auto& k=kind(type);
  for(const auto& r:reservations) {
   const auto& pending=kind(r.type);
   if(k.isVirtual && pending.isVirtual && world.normalizeX(x)==world.normalizeX(r.x) && world.normalizeY(y)==world.normalizeY(r.y)) return false;
   if(!k.isVirtual && !pending.isVirtual)
    for(int dy=0;dy<k.height;++dy) for(int dx=0;dx<k.width;++dx)
     if(world.normalizeX(x+dx-r.x)<pending.width && world.normalizeY(y+dy-r.y)<pending.height) return false;
  }
  if(k.isVirtual) {
   for(const auto identity:world.teams[owner].virtualBuildings)
    if(const auto* b=world.building(identity);b && b->x==world.normalizeX(x) && b->y==world.normalizeY(y)) return false;
   return true;
  }
  if(!isFreeForBuilding(x,y,k.width,k.height)) return false;
  for(int dy=0;dy<k.height;++dy) for(int dx=0;dx<k.width;++dx)
   if(world.tile(x+dx,y+dy).discovered & world.teams[owner].mask) return true;
  return false;
 }
 bool hardSpaceForUpgrade(const Building& b) const
 {
  const auto& current=kind(b);
  if(world.rules.upgradesDisabled || current.next<0) return false;
  const auto& next=kind(current.next);
  return next.isVirtual || isFreeForBuilding(b.x+next.decLeft-current.decLeft,b.y+next.decTop-current.decTop,
   next.width,next.height,true,b.identity.gid);
 }
 std::shared_ptr<Order> createOrder(int owner,int x,int y,int type,int workers,int futureWorkers) const
 {
  const auto& k=kind(type); const auto& complete=k.site?kind(k.next):k;
  return std::make_shared<OrderCreate>(owner,x,y,type,std::clamp(workers,0,k.semantics.assignmentLimit),
   std::clamp(futureWorkers,0,complete.semantics.assignmentLimit));
 }
 std::shared_ptr<Order> constructionOrder(const Building& b,int workers,int futureWorkers) const
 {
  const auto& k=kind(b);
  const bool repair=k.site?b.construction==::Building::REPAIR:b.hp<b.maxHp;
  const int target=k.site?b.type:repair?k.previous:k.next;
  const auto& placement=target>=0?kind(target):k;
  const int origin=k.site?b.originType:b.type;
  const auto& completed=repair && origin>=0?kind(origin):placement.site?kind(placement.next):placement;
  return std::make_shared<OrderConstruction>(b.identity.gid,std::clamp(workers,0,placement.semantics.assignmentLimit),
   std::clamp(futureWorkers,0,completed.semantics.assignmentLimit));
 }
 std::span<const Uint16> resourceGradient(int owner,int resource,int swim)
 {
  const int key=(owner*MAX_NB_RESOURCES+resource)*7+swim;
  auto published=world.resourceGradient(owner,resource,swim);
  std::span<const Uint16> gradient=published;
  if(!published.empty()) resourceFields.erase(key);
  else {
   auto [it,inserted]=resourceFields.try_emplace(key);
   auto& initialization=it->second;
   if(inserted) {
    initialization.observedTick=world.tick;
    auto buffer=std::make_shared<std::vector<Uint16>>();
    auto& values=*buffer;
   const Uint32 mask=world.teams[owner].mask;
   values.resize(world.tiles.size());
   bool modified=false;
   for(size_t i=0;i<values.size();++i) {
    const auto& tile=world.tiles[i]; const auto& terrain=world.terrain->properties(tile.terrain);
    modified |= terrain.groundSpeedQ8!=256;
    Uint16 value=GRADIENT_FORBIDDEN;
    if(!(tile.forbidden & mask) && tile.immobileUnit==255) {
     if(tile.resource.type==NO_RES_TYPE && tile.building==0xffff)
      value=(terrain.walkable || (swim && terrain.swimmable))?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
     else if(tile.resource.type==resource && (!world.resourceVisibleToBeCollected[resource] || (tile.visible & mask)))
      value=GRADIENT_AT_GOAL;
    }
    values[i]=value;
   }
   gradient_kernel::propagateTerrainField(values.data(),swim,gradient_kernel::COST_LIMIT,{world.width,world.height},scratch,
    [&](size_t i){return world.tiles[i].terrain;},modified,*world.terrain,256);
    initialization.values=std::move(buffer);
   }
   if(enrollments && enrollmentsReported.insert(key).second)
    enrollments->push_back({owner,resource,swim,initialization.observedTick,initialization.values});
   gradient=*initialization.values;
  }
  return gradient;
 }
 bool resourceAvailableUpdate(int owner,int resource,int swim,int x,int y,int* rx,int* ry,int* distance)
 {
  const auto gradient=resourceGradient(owner,resource,swim);
  const auto index=[&](int px,int py){return size_t(world.normalizeY(py))*world.width+world.normalizeX(px);};
  Uint16 best=gradient[index(x,y)];
  const bool found=best>GRADIENT_UNREACHABLE;
  if(found && distance) *distance=gradientTiles(best);
  int px=world.normalizeX(x),py=world.normalizeY(y);
  static constexpr int directions[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
  while(true) {
   int dx=0,dy=0;bool step=false;
   for(const auto& d:directions) if(gradient[index(px+d[0],py+d[1])]>best) {
    best=gradient[index(px+d[0],py+d[1])]; dx=d[0];dy=d[1];step=true;
   }
   px=world.normalizeX(px+dx);py=world.normalizeY(py+dy);
   if(best==GRADIENT_AT_GOAL || !step) break;
  }
  *rx=px;*ry=py; return found;
 }
 std::shared_ptr<Order> missingProductionOrder(const std::array<int,NB_UNIT_TYPE>& desired,int workers,int futureWorkers) const
 {
  unsigned required=0,provided=0;const Building* anchor=nullptr;
  for(unsigned unit=0;unit<NB_UNIT_TYPE;++unit) if(desired[unit]>0 && allowed(Intent(unit))) required |= 1u<<unit;
  for(const auto& b:world.buildings) if(b.team==team && b.state==::Building::ALIVE) {
   const auto& k=kind(b); const int completed=k.site?(b.construction==::Building::REPAIR && b.originType>=0?b.originType:k.next):b.type;
   provided |= unsigned(kind(completed).rawCapabilityMask)&7u;
   if(!anchor || (!k.site && (k.rawCapabilityMask & 7u))) anchor=&b;
   if((provided & required)==required) return {};
  }
  for(const auto& r:reservations) {
   const auto& k=kind(r.type);provided |= unsigned(kind(k.site?k.next:r.type).rawCapabilityMask)&7u;
  }
  if(!anchor || !required || (provided & required)==required) return {};
  for(unsigned unit=0;unit<NB_UNIT_TYPE;++unit) if((required & (1u<<unit)) && !(provided & (1u<<unit)))
   for(int type:placements(Intent(unit),true)) {
    const auto& k=kind(type); bool eligible=false;
    for(int level=k.semantics.requiredWorkerLevel;level<NB_UNIT_LEVELS;++level)
     if(level>=0 && world.teams[team].statistics.workersByConstructionLevel[level]>0) {eligible=true;break;}
    if(!eligible) continue;
    for(int radius=1;radius<=32;++radius) for(int dx=-radius;dx<=radius;++dx) for(int dy=-radius;dy<=radius;++dy) {
     if(std::abs(dx)!=radius && std::abs(dy)!=radius) continue;
     const int x=world.normalizeX(anchor->x+dx),y=world.normalizeY(anchor->y+dy);
     if(!(world.tile(x,y).discovered & world.teams[team].allies) || !checkRoomForBuilding(x,y,type,team)) continue;
     return createOrder(team,x,y,type,workers,futureWorkers);
    }
   }
  return {};
 }
};
} // namespace AIEngine
