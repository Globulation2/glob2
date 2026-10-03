// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AIFarmAreas.h"
#include "shared_runtime/Runtime.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::SearchTools;

MapInfo::MapInfo(Runtime& runtime) : runtime(runtime)
{

}



int MapInfo::get_width()
{
	return runtime.player->map->getW();
}



int MapInfo::get_height()
{
	return runtime.player->map->getH();
}



bool MapInfo::is_forbidden_area(int x, int y)
{
	return runtime.player->map->isForbidden(x, y, runtime.player->team->me);
}



bool MapInfo::is_guard_area(int x, int y)
{
	return runtime.player->map->isGuardArea(x, y, runtime.player->team->me);
}



bool MapInfo::is_clearing_area(int x, int y)
{
	return runtime.player->map->isClearArea(x, y, runtime.player->team->me);
}



bool MapInfo::is_farm_area(int x, int y)
{
	return runtime.player->map->isFarmArea(x, y, runtime.player->team->me);
}



bool MapInfo::farm_areas_enabled()
{
	return runtime.player->map->farmAreasEnabled();
}



bool MapInfo::wants_farm(int x, int y)
{
	return AIFarmAreas::wantsFarm(*runtime.player->map, x, y);
}



bool MapInfo::is_discovered(int x, int y)
{
	return runtime.player->map->isMapDiscovered(x, y, runtime.player->team->me);
}



bool MapInfo::is_resource(int x, int y, int type)
{
	return runtime.player->map->isResourceTakeable(x, y, type);
}



bool MapInfo::is_resource(int x, int y)
{
	return runtime.player->map->isResource(x, y);
}



bool MapInfo::is_water(int x, int y)
{
	return runtime.player->map->isWater(x, y);
}



bool MapInfo::is_sand(int x, int y)
{
	return runtime.player->map->isSand(x, y);
}



bool MapInfo::is_grass(int x, int y)
{
	return runtime.player->map->isGrass(x, y);
}



bool MapInfo::backs_onto_sand(int x, int y)
{
	if(runtime.player->map->hasSand(x-1, y))
		return true;
	if(runtime.player->map->hasSand(x+1, y))
		return true;
	if(runtime.player->map->hasSand(x-1, y-1))
		return true;
	if(runtime.player->map->hasSand(x, y-1))
		return true;
	if(runtime.player->map->hasSand(x+1, y-1))
		return true;
	if(runtime.player->map->hasSand(x-1, y+1))
		return true;
	if(runtime.player->map->hasSand(x, y+1))
		return true;
	if(runtime.player->map->hasSand(x+1, y+1))
		return true;
	return false;
}



int MapInfo::get_amount_resource(int x, int y)
{
	return runtime.player->map->getResource(x, y).amount;
}
