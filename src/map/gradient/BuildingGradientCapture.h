// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientBuild.h"
#include "Map.h"
#include "Game.h"
#include "sim/snapshot/SnapshotStore.h"
#include "BuildingType.h"

namespace building_gradient
{
inline Building *refreshDestination(Game *game, int gid)
{
	if (gid < 0 || gid >= Building::MAX_COUNT * Team::MAX_COUNT)
		return nullptr;
	const int team = Building::GIDtoTeam(gid);
	return team < game->teamsCount() && game->teams[team]
			   ? game->teams[team]->myBuildings[Building::GIDtoID(gid)]
			   : nullptr;
}
inline building_gradient::Destination refreshDescription(const Building &b, int swim,
														 std::uint32_t epoch, BuildingRoute route=BuildingRoute::Automatic)
{
	building_gradient::Destination d;
	d.gid = b.gid;
	d.identity = b.scriptIdentity;
	d.epoch = epoch;
	d.x = b.posX;
	d.y = b.posY;
	d.width = b.type->width;
	d.height = b.type->height;
	d.route = int(b.resolveRoute(route));
	d.occupiesGround = b.type->semantics.occupiesGround;
	d.radius = b.unitStayRange;
	d.swim = swim;
	d.teamMask = b.owner->me;
	d.allies = b.owner->allies;
	d.virtualBuilding = b.type->isVirtual;
	d.clearing = d.route == int(BuildingRoute::Clearing);
	d.war = d.route == int(BuildingRoute::Combat);
	for (int r = 0; r < BASIC_COUNT; ++r)
		d.clearingResources[r] = b.clearingResources[r];
	return d;
}
inline std::shared_ptr<Terrain> captureTerrain(const Map &map)
{
 auto result=std::make_shared<Terrain>();
 result->width=map.getW(); result->height=map.getH(); result->generation=map.topologyGeneration;
 result->modifiedCosts=map.hasTerrainMovementModifiers(); result->registry=map.frozenTerrainRegistry();
 result->costs=map.frozenTerrainSnapshot();
 const auto water=map.frozenWaterSnapshot();
 for(int sw=0;sw<SWIM_CLASS_COUNT;++sw) {
  auto &inputs=result->inputs[sw]; inputs.terrain=result->costs; inputs.registry=result->registry;
  inputs.modified=result->modifiedCosts; inputs.buckets=map.terrainQueueBuckets();
  if(inputs.modified && inputs.registry->size()>TERRAIN_COUNT) inputs.profiles=map.frozenTerrainMovementSnapshot(sw);
  if(!inputs.modified && gradient_kernel::weightedClass(sw) && inputs.registry->size()>TERRAIN_COUNT) inputs.water=water;
 }
 // Capture only the components used by the pure gradient kernel. This can
 // occur after mutation within a tick already observed by AI, so it must name
 // a fresh owner boundary instead of reusing the tick's earlier whole handle.
 constexpr auto requirements = SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain)
  | SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)
  | SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy)
  | SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
 if (map.game) result->snapshot = map.game->snapshotStore()->captureBoundary(*map.game, requirements, true);
 else { // Isolated Map fixtures have no engine snapshot owner.
  const auto size=std::size_t(map.getW())*map.getH(); result->cells.resize(size);
  for(std::size_t i=0;i<size;++i) {
   const auto &tile=map.getTile(i);
   result->cells[i]={tile.forbidden,tile.building,tile.resource.type,map.getImmobileUnit(i % map.getW(), i / map.getW()),
    Uint8(tile.building==NOGBID ? 0 : Building::GIDtoTeam(tile.building)), map.terrainTypeAt(i)};
  }
 }
 return result;
}
inline void captureParents(Map &map, Building &b, int swim,
 std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> &parents,
 std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> &goals,
 const std::function<const Uint16*(int)> &published={})
{
 const auto size=std::size_t(map.getW())*map.getH();
 for(int r=0;r<MAX_NB_RESOURCES;++r) if(b.roundTripGradient[r][swim]) {
  const unsigned modes=map.resourceSupplyModes(&b,r);
  const auto *parent=published ? published(r) : map.getResourceGradient(b.owner->teamNumber,r,swim,modes!=0,&b);
  parents[r].assign(parent,parent+size);
  if(!modes) continue;
  goals[r].assign(size,0);
  for(std::size_t i=0;i<size;++i) if(map.getTile(i).building!=NOGBID && parents[r][i]>GRADIENT_UNREACHABLE) goals[r][i]=1;
  auto visit=[&](const Building *supplier) {
   if(!supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround) && !map.stockSupplierEligible(supplier,&b,r,modes)) return;
   const auto penalty=1+supplier->type->semantics.market.pickupPenalty*GRADIENT_STEP;
   for(int y=0;y<supplier->type->height;++y) for(int x=0;x<supplier->type->width;++x) {
    const auto i=map.coordToIndex(supplier->posX+x,supplier->posY+y);
    if(parents[r][i]>GRADIENT_UNREACHABLE) {
     if(supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround) || !goals[r][i]) goals[r][i]=penalty;
     else goals[r][i]=std::min<Uint16>(goals[r][i],penalty);
    }
   }
  };
  if(modes&1) for(const auto *supplier:b.owner->stockSuppliers) visit(supplier);
  if(modes&2) for(const auto *supplier:b.owner->directStockSuppliers)
   if(!(modes&1) || !(supplier->runtime->suppliesStockMask&(1u<<r))) visit(supplier);
 }
}
} // namespace building_gradient
