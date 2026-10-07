// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "ai/observation/AIWorldView.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::SearchTools;

MapInfo::MapInfo(Runtime& runtime) : runtime(&runtime)
{
    teamMask=runtime.observedTeam().mask;
    world=&runtime.observation();
}


MapInfo::MapInfo(const AIEngine::AIWorldView& view, Uint32 mask)
    : world(&view), teamMask(mask) {}

int MapInfo::get_width()
{
    return world->width;
}



int MapInfo::get_height()
{
    return world->height;
}



bool MapInfo::is_forbidden_area(int x, int y)
{
    return (world->areasAt(world->tileIndex(x,y)).forbidden & teamMask)!=0;
}



bool MapInfo::is_guard_area(int x, int y)
{
    return (world->areasAt(world->tileIndex(x,y)).guard & teamMask)!=0;
}



bool MapInfo::is_clearing_area(int x, int y)
{
    return (world->areasAt(world->tileIndex(x,y)).clear & teamMask)!=0;
}



bool MapInfo::is_farm_area(int x, int y)
{
    return (world->areasAt(world->tileIndex(x,y)).farm & teamMask)!=0;
}



bool MapInfo::farm_areas_enabled()
{
    return world->farmAreasEnabled;
}



bool MapInfo::can_paint_farm(int x, int y)
{
    return world->canPaintFarmAt(world->tileIndex(x,y));
}



bool MapInfo::is_discovered(int x, int y)
{
    return (world->visibilityAt(world->tileIndex(x,y)).discovered & teamMask)!=0;
}



bool MapInfo::is_resource(int x, int y, int type)
{
    const auto resource=world->resourceAt(world->tileIndex(x,y)).resource;
    return resource.type==type && resource.amount>0;
}



bool MapInfo::is_resource(int x, int y)
{
    return world->resourceAt(world->tileIndex(x,y)).resource.type!=NO_RES_TYPE;
}



bool MapInfo::is_water(int x, int y)
{
    return world->terrain->properties(world->terrainAt(world->tileIndex(x,y)).type).swimmable;
}



bool MapInfo::is_sand(int x, int y)
{
    return world->terrain->properties(world->terrainAt(world->tileIndex(x,y)).type).inhibitionQ8!=0;
}



bool MapInfo::is_resource_habitat(int x, int y, int resource)
{
        const auto& terrain=world->terrain->properties(world->terrainAt(world->tileIndex(x,y)).type);
        return resource>=0 && resource<MAX_RESOURCES
            && (terrain.allowedResources & (1u<<resource))
            && (world->resourceShrinkable[resource] || terrain.nonGrowingResources);

}

bool MapInfo::is_crop_habitat(int x, int y)
{
    return world->terrain->properties(world->terrainAt(world->tileIndex(x,y)).type).allowedResources & (1u<<WHEAT);
}

bool MapInfo::is_grass(int x, int y)
{
    return world->terrain->properties(world->terrainAt(world->tileIndex(x,y)).type).buildable;
}



bool MapInfo::backs_onto_sand(int x, int y)
{
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if((dx || dy) && world->terrain->properties(world->terrainAt(world->tileIndex(x+dx,y+dy)).type).shoreline)
                return true;
        return false;

}



int MapInfo::get_amount_resource(int x, int y)
{
    return world->resourceAt(world->tileIndex(x,y)).resource.amount;
}
