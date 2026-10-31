// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Farming.h"
#include "shared_runtime/Runtime.h"
#include "Material.h"
#include "AIResourcePolicy.h"
#include "Game.h"
#include "Building.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::SearchTools;

namespace
{
// Water-gradient steps: the outer wheat band is kept free of spreading wood.
constexpr int WoodWaterDistance = 6;
constexpr int WheatWaterDistance = 10;
// Permanent donors at (1,1); temporary expansion donors at (0,1) and (1,0).
constexpr int PatternStride = 2;
}

void AISharedRuntime::updateFarmingAreas(Runtime& runtime)
{
	// Protecting growth cells would permanently withhold resources without regrowth.
	if (runtime.observation().configuration->isResourceGrowthDisabled()) return;
	//Farming wheat and wood in areas near water
	AddArea* mo_farming=new AddArea(ForbiddenArea);
	RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
	AddArea* mo_clearing=new AddArea(ClearingArea);
	RemoveArea* mo_non_clearing=new RemoveArea(ClearingArea);
	// With the farm-areas experiment, wheat near water is farmed with a farm
	// area on the same protection pattern. Wood keeps its forbidden spots.
	MapInfo mi(runtime);
	const MapState::View& map=runtime.observation().state();
	const bool farms = mi.farm_areas_enabled();
	AddArea* mo_farm=farms ? new AddArea(FarmArea) : nullptr;
	RemoveArea* mo_non_farm=farms ? new RemoveArea(FarmArea) : nullptr;
	AISharedRuntime::Gradients::GradientInfo gi_water;
	gi_water.add_source(new Entities::Water);
	Gradient& water_gradient=runtime.get_gradient_manager().get_gradient(gi_water);

	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(mi.is_discovered(x, y))
			{
				const int wood_dist = WoodWaterDistance;
				const int wheat_dist = WheatWaterDistance;

				bool is_wood = mi.is_resource(x, y, materialIndex(MaterialId::Wood));
				bool is_wheat = mi.is_resource(x, y, materialIndex(MaterialId::Food));

				const auto type=map.resources[map.index(x,y)].resource.type;
				const auto properties=type==NO_RES_TYPE ? ResourceProperties{} : map.resourceProperties(type);
				const bool nonLand=properties.ecology!=ResourceEcology::Land;
				bool is_in_wheat_zone = nonLand || water_gradient.within_dist(x, y, wheat_dist);
				bool is_in_wood_zone = nonLand || water_gradient.within_dist(x, y, wood_dist);
				const bool reserveWood=AIResourcePolicy::needsSeedReserve(map,map.index(x,y),MaterialId::Wood);
				const bool reserveFood=AIResourcePolicy::needsSeedReserve(map,map.index(x,y),MaterialId::Food);

				bool farm_spot = false;

				//Permanent farming exists for every second row and column
				if(x%PatternStride==1 && y%PatternStride==1)
				{
					if((reserveWood && is_in_wood_zone) || (reserveFood && is_in_wheat_zone))
					{
						farm_spot = true;
					}
				}

				//Expand the farm horizontally
				if((x%PatternStride==0 && y%PatternStride==1))
				{
					if(reserveWood && mi.is_resource(x-1, y, materialIndex(MaterialId::Wood)) && !mi.is_resource(x+1,y) && (nonLand || water_gradient.within_dist(x+1, y, wood_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x+1,y),MaterialId::Wood))
					{
						farm_spot = true;
					}
					else if(reserveFood && mi.is_resource(x-1, y, materialIndex(MaterialId::Food)) && !mi.is_resource(x+1,y) && (nonLand || water_gradient.within_dist(x+1, y, wheat_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x+1,y),MaterialId::Food))
					{
						farm_spot = true;
					}
					else if(reserveWood && mi.is_resource(x+1, y, materialIndex(MaterialId::Wood)) && !mi.is_resource(x-1,y) && (nonLand || water_gradient.within_dist(x-1, y, wood_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x-1,y),MaterialId::Wood))
					{
						farm_spot = true;
					}
					else if(reserveFood && mi.is_resource(x+1, y, materialIndex(MaterialId::Food)) && !mi.is_resource(x-1,y) && (nonLand || water_gradient.within_dist(x-1, y, wheat_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x-1,y),MaterialId::Food))
					{
						farm_spot = true;
					}
				}

				//Expand the farm vertically
				if((x%PatternStride==1 && y%PatternStride==0))
				{
					if(reserveWood && mi.is_resource(x, y-1, materialIndex(MaterialId::Wood)) && !mi.is_resource(x,y+1) && (nonLand || water_gradient.within_dist(x, y+1, wood_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x,y+1),MaterialId::Wood))
					{
						farm_spot = true;
					}
					else if(reserveFood && mi.is_resource(x, y-1, materialIndex(MaterialId::Food)) && !mi.is_resource(x,y+1) && (nonLand || water_gradient.within_dist(x, y+1, wheat_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x,y+1),MaterialId::Food))
					{
						farm_spot = true;
					}
					else if(reserveWood && mi.is_resource(x, y+1, materialIndex(MaterialId::Wood)) && !mi.is_resource(x,y-1) && (nonLand || water_gradient.within_dist(x, y-1, wood_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x,y-1),MaterialId::Wood))
					{
						farm_spot = true;
					}
					else if(reserveFood && mi.is_resource(x, y+1, materialIndex(MaterialId::Food)) && !mi.is_resource(x,y-1) && (nonLand || water_gradient.within_dist(x, y-1, wheat_dist)) && AIResourcePolicy::canSpreadTo(map,map.index(x,y),map.index(x,y-1),MaterialId::Food))
					{
						farm_spot = true;
					}
				}

				// Preserve the existing wood-clearing rules.
				bool clear_wood = is_wood && !is_wheat && properties.clearable
					&& AIResourcePolicy::canPropagate(map,map.index(x,y),MaterialId::Wood) &&
					((is_in_wheat_zone && !is_in_wood_zone) || mi.is_resource(x-1, y, materialIndex(MaterialId::Food)) ||
					 mi.is_resource(x+1, y, materialIndex(MaterialId::Food)) || mi.is_resource(x, y-1, materialIndex(MaterialId::Food)) ||
					 mi.is_resource(x, y+1, materialIndex(MaterialId::Food)) || mi.is_resource(x-1, y-1, materialIndex(MaterialId::Food)) ||
					 mi.is_resource(x-1, y+1, materialIndex(MaterialId::Food)) || mi.is_resource(x+1, y-1, materialIndex(MaterialId::Food)) ||
					 mi.is_resource(x+1, y+1, materialIndex(MaterialId::Food)));
				bool clearing_area = mi.is_clearing_area(x,y);
				if(clear_wood && !clearing_area)
					mo_clearing->add_location(x, y);
				else if(!clear_wood && clearing_area)
				{
					// Keep building clearance; release cleared farm tiles for wheat.
					bool beside_building = false;
					for(int dx=-1; dx<=1; ++dx)
						for(int dy=-1; dy<=1; ++dy)
						{
							int gid = runtime.observation().occupancyAt(runtime.observation().tileIndex(x+dx,y+dy)).building;
							if(gid!=NOGBID && Building::GIDtoTeam(gid)==runtime.teamNumber())
								beside_building = true;
						}
					if(!beside_building)
					{
						mo_non_clearing->add_location(x, y);
						clearing_area = false;
					}
				}

				if(farm_spot && (clear_wood || clearing_area))
				{
					farm_spot = false;
				}

				if(farm_spot && !MapState::terrainSupportsResourceSlot(map,map.index(x,y),type))
				{
					farm_spot = false;
				}

				if(farms)
				{
					const auto foodYield=is_wheat ? map.resourceRegistry->yields(static_cast<ResourceId>(type))[materialIndex(MaterialId::Food)] : YieldProperties{};
					const bool wheat_farm = farm_spot && reserveFood && properties.farmable
						&& foodYield.consumption==ResourceConsumption::One && !foodYield.destroysDeposit
						&& mi.can_paint_farm(x, y);
					if(wheat_farm && !mi.is_farm_area(x, y))
						mo_farm->add_location(x, y);
					else if(!wheat_farm && mi.is_farm_area(x, y))
						mo_non_farm->add_location(x, y);
					if(wheat_farm && !reserveWood) farm_spot = false;
				}

				if(farm_spot && !mi.is_forbidden_area(x, y))
				{
					mo_farming->add_location(x, y);
				}
				else if(!farm_spot && mi.is_forbidden_area(x, y))
				{
					mo_non_farming->add_location(x, y);
				}
			}
		}
	}
	runtime.add_management_order(mo_non_clearing);
	runtime.add_management_order(mo_farming);
	runtime.add_management_order(mo_non_farming);
	if(farms)
	{
		runtime.add_management_order(mo_farm);
		runtime.add_management_order(mo_non_farm);
	}
	runtime.add_management_order(mo_clearing);
}

