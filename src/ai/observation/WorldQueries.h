// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/observation/AIWorldView.h"
#include "ai/observation/ObservationQueries.h"
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
 {}
 int getW() const { return world.width; }
 int getH() const { return world.height; }
 TileView getTile(int x,int y) const { return world.tile(x,y); }
 Uint16 getBuilding(int x,int y) const { return world.occupancyAt(world.tileIndex(x,y)).building; }
 const TerrainProperties& terrainPropertiesAt(int x,int y) const { return world.terrain->properties(world.terrainAt(world.tileIndex(x,y)).type); }
 bool isMapDiscovered(int x,int y,Uint32 mask) const { return world.visibilityAt(world.tileIndex(x,y)).discovered&mask; }
 bool isFOWDiscovered(int x,int y,Uint32 mask) const { return world.visibilityAt(world.tileIndex(x,y)).visible&mask; }
 bool isForbidden(int x,int y,Uint32 mask) const { return world.areasAt(world.tileIndex(x,y)).forbidden&mask; }
 bool isGuardArea(int x,int y,Uint32 mask) const { return world.areasAt(world.tileIndex(x,y)).guard&mask; }
 bool isClearArea(int x,int y,Uint32 mask) const { return world.areasAt(world.tileIndex(x,y)).clear&mask; }
 bool isFarmArea(int x,int y,Uint32 mask) const { return world.areasAt(world.tileIndex(x,y)).farm&mask; }
 bool farmAreasEnabled() const { return world.farmAreasEnabled; }
 bool canPaintFarmArea(int x,int y) const { return world.canPaintFarmAt(world.tileIndex(x,y)); }
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
  const auto at=[&](int a,int b){return values[world.tileIndex(a,b)];};
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
 const Kind& kind(const Building& b) const { return kind(b.typeNum); }
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
 { const auto& type=kind(b); return matches(type.site?type.next:b.typeNum,intent); }
 bool available(const AIPlanning::BuildingCandidate& candidate,Intent intent) const
 { return AIEngine::ObservationQueries::available(world,candidate,intent); }
 const std::vector<AIPlanning::BuildingCandidate>& placements(Intent intent,bool costOrder=false) const
 { return costOrder ? world.capabilities().placementsByCost(intent) : world.capabilities().placements(intent); }
 bool isResourceTakeable(int x,int y,int material) const { return MapState::hasMaterialSlot(world.state(),world.tileIndex(x,y),material); }
 bool isFreeForBuilding(int x,int y,int width=1,int height=1,bool hard=false,Uint16 ignore=0xffff) const
 {
  for(int dy=0;dy<height;++dy) for(int dx=0;dx<width;++dx) {
   const auto i=world.tileIndex(x+dx,y+dy);const auto r=world.resourceAt(i);const auto o=world.occupancyAt(i);
   if(MapState::resourceBlocksBuilding(world.state(),i) || (o.building!=0xffff && o.building!=ignore)
     || (!hard && o.groundUnit!=0xffff) || !world.terrain->properties(world.terrainAt(i).type).buildable) return false;
  }
  return true;
 }
 bool checkRoomForBuilding(int x,int y,int type,int owner) const
 {
  const auto& k=kind(type);
  const auto overlaps=[&](int type,int pendingX,int pendingY) {
   const auto& pending=kind(type);
   if(k.isVirtual && pending.isVirtual && world.normalizeX(x)==world.normalizeX(pendingX) && world.normalizeY(y)==world.normalizeY(pendingY)) return true;
   if(!k.isVirtual && !pending.isVirtual)
    for(int dy=0;dy<k.height;++dy) for(int dx=0;dx<k.width;++dx)
     if(world.normalizeX(x+dx-pendingX)<pending.width && world.normalizeY(y+dy-pendingY)<pending.height) return true;
   return false;
  };
  for(const auto& r:reservations) if(overlaps(r.type,r.x,r.y)) return false;
  for(const auto& project:world.buildProjects)
   if(project.teamNumber==team && overlaps(project.typeNum,project.posX,project.posY)) return false;
  if(k.isVirtual) {
   for(const auto identity:world.teams[owner].virtualBuildings)
    if(const auto* b=world.building(identity);b && b->posX==world.normalizeX(x) && b->posY==world.normalizeY(y)) return false;
   return true;
  }
  if(!isFreeForBuilding(x,y,k.width,k.height)) return false;
  for(int dy=0;dy<k.height;++dy) for(int dx=0;dx<k.width;++dx)
   if(world.visibilityAt(world.tileIndex(x+dx,y+dy)).discovered & world.teams[owner].mask) return true;
  return false;
 }
 bool hardSpaceForUpgrade(const Building& b) const
 {
  const auto& current=kind(b);
  if(world.rules.upgradesDisabled || current.next<0) return false;
  const auto& next=kind(current.next);
  return next.isVirtual || isFreeForBuilding(b.posX+next.decLeft-current.decLeft,b.posY+next.decTop-current.decTop,
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
  const bool repair=k.site?b.constructionResultState==::Building::REPAIR:b.hp<b.maxHp;
  const int target=k.site?b.typeNum:repair?k.previous:k.next;
  const auto& placement=target>=0?kind(target):k;
  const int origin=k.site?b.constructionOriginTypeNum:b.typeNum;
  const auto& completed=repair && origin>=0?kind(origin):placement.site?kind(placement.next):placement;
  return std::make_shared<OrderConstruction>(b.identity.gid,std::clamp(workers,0,placement.semantics.assignmentLimit),
   std::clamp(futureWorkers,0,completed.semantics.assignmentLimit));
 }
 std::span<const Uint16> resourceGradient(int owner,int resource,int swim)
 {
  const int key=(owner*MaterialSlotCount+resource)*7+swim;
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
    const auto terrainCell=world.terrainAt(i);const auto resourceCell=world.resourceAt(i);const auto occupancy=world.occupancyAt(i);
    const auto& terrain=world.terrain->properties(terrainCell.type);
    modified |= terrain.groundSpeedQ8!=256;
    Uint16 value=GRADIENT_FORBIDDEN;
    if(!(world.areasAt(i).forbidden & mask) && occupancy.immobileUnit==255) {
     if(!MapState::resourceBlocksGround(world.state(),i) && occupancy.building==0xffff)
      value=(terrain.walkable || (swim && terrain.swimmable))?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
     // Passable sources are goals too. Visibility belongs to each source.
     if((MapState::materialMaskAt(world.state(),i) & MaterialMask(1u<<resource)) && (!MapState::resourceVisibleToHarvest(world.state(),i) || (world.visibilityAt(i).visible & mask)))
      value=GRADIENT_AT_GOAL;
    }
    values[i]=value;
   }
   gradient_kernel::propagateTerrainField(values.data(),swim,gradient_kernel::COST_LIMIT,{world.width,world.height},scratch,
    [&](size_t i){return world.terrainAt(i).type;},modified,*world.terrain,256);
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
  const auto index=[&](int px,int py){return world.tileIndex(px,py);};
  Uint16 best=gradient[index(x,y)];
  const bool found=best>GRADIENT_UNREACHABLE;
  if(found && distance) *distance=gradientTiles(best);
  int px=world.normalizeX(x),py=world.normalizeY(y);
  static constexpr int directions[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
  while(true) {
   int dx=0,dy=0;bool step=false;
   for(const auto& d:directions) if(const auto candidate=gradient[index(px+d[0],py+d[1])];candidate>best) {
    best=candidate; dx=d[0];dy=d[1];step=true;
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
  if(!required) return {};
  for(const auto& b:world.buildings) if(b.team==team && b.buildingState==::Building::ALIVE) {
   const auto& k=kind(b); const int completed=k.site?(b.constructionResultState==::Building::REPAIR && b.constructionOriginTypeNum>=0?b.constructionOriginTypeNum:k.next):b.typeNum;
   provided |= unsigned(kind(completed).rawCapabilityMask)&7u;
   if(!anchor || (world.capabilities().intentMask(b.typeNum) & 7u)) anchor=&b;
   if((provided & required)==required) return {};
  }
  for(const auto& r:reservations) {
   const auto& k=kind(r.type);provided |= unsigned(kind(k.site?k.next:r.type).rawCapabilityMask)&7u;
  }
  for(const auto& project:world.buildProjects) if(project.teamNumber==team) {
   const auto& k=kind(project.typeNum);provided |= unsigned(kind(k.site?k.next:project.typeNum).rawCapabilityMask)&7u;
  }
  if(!anchor || !required || (provided & required)==required) return {};
  for(unsigned unit=0;unit<NB_UNIT_TYPE;++unit) if((required & (1u<<unit)) && !(provided & (1u<<unit)))
   for(const auto& candidate:placements(Intent(unit),true)) {
    if(!available(candidate,Intent(unit))) continue;
    const int type=candidate.placementType;
    const auto& k=kind(type); bool eligible=false;
    for(int level=k.semantics.requiredWorkerLevel;level<NB_UNIT_LEVELS;++level)
     if(level>=0 && world.teams[team].statistics.workersByConstructionLevel[level]>0) {eligible=true;break;}
    if(!eligible) continue;
    for(int radius=1;radius<=32;++radius) for(int dx=-radius;dx<=radius;++dx) for(int dy=-radius;dy<=radius;++dy) {
     if(std::abs(dx)!=radius && std::abs(dy)!=radius) continue;
     const int x=world.normalizeX(anchor->posX+dx),y=world.normalizeY(anchor->posY+dy);
     if(!(world.visibilityAt(world.tileIndex(x,y)).discovered & world.teams[team].allies) || !checkRoomForBuilding(x,y,type,team)) continue;
     return createOrder(team,x,y,type,workers,futureWorkers);
    }
   }
  return {};
 }
};
} // namespace AIEngine
