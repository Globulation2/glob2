// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "Material.h"
#include "shared_runtime/Runtime.h"
#include "GlobalContainer.h"

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



bool MapInfo::can_paint_farm(int x, int y)
{
	return runtime.player->map->canPaintFarmArea(x, y);
}



bool MapInfo::is_discovered(int x, int y)
{
	return runtime.player->map->isMapDiscovered(x, y, runtime.player->team->me);
}



bool MapInfo::is_resource(int x, int y, int type)
{
	return runtime.player->map->isMaterialTakeable(x, y, type);
}



bool MapInfo::is_resource(int x, int y)
{
	return runtime.player->map->isResource(x, y);
}



bool MapInfo::is_water(int x, int y)
{
	return runtime.player->map->terrainPropertiesAt(x, y).swimmable;
}



bool MapInfo::is_sand(int x, int y)
{
	return runtime.player->map->terrainPropertiesAt(x, y).inhibitionQ8 != 0;
}



bool MapInfo::is_resource_habitat(int x, int y, int resource)
{
	return runtime.player->map->terrainSupportsMaterialAt(x,y,resource);
}

bool MapInfo::is_crop_habitat(int x, int y)
{
	return runtime.player->map->terrainSupportsMaterialAt(x,y,materialIndex(MaterialId::Food));
}

bool MapInfo::is_grass(int x, int y)
{
	return runtime.player->map->terrainPropertiesAt(x, y).buildable;
}



bool MapInfo::backs_onto_sand(int x, int y)
{
	if(runtime.player->map->terrainPropertiesAt(x-1, y).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x+1, y).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x-1, y-1).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x, y-1).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x+1, y-1).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x-1, y+1).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x, y+1).shoreline)
		return true;
	if(runtime.player->map->terrainPropertiesAt(x+1, y+1).shoreline)
		return true;
	return false;
}



int MapInfo::get_amount_resource(int x, int y)
{
	return runtime.player->map->getResource(x, y).amount;
}
