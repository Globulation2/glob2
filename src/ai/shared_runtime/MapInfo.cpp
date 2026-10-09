// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "Material.h"
#include "shared_runtime/Runtime.h"
#include "ai/observation/AIWorldView.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::SearchTools;

MapInfo::MapInfo(Runtime& runtime) : runtime(&runtime)
{
    teamMask=runtime.observedTeam().mask;
}


MapInfo::MapInfo(const AIEngine::AIWorldView& view, Uint32 mask)
    : world(&view), teamMask(mask) {}

// An owner refresh can replace the runtime's lease while this adapter survives.
// Explicit snapshot adapters continue borrowing the caller's immutable view.
const AIEngine::AIWorldView& MapInfo::observation() const
{
    return runtime ? runtime->observation() : *world;
}

int MapInfo::get_width()
{
    const auto& observed=observation();
    return observed.width;
}



int MapInfo::get_height()
{
    const auto& observed=observation();
    return observed.height;
}



bool MapInfo::is_forbidden_area(int x, int y)
{
    const auto& observed=observation();
    return (observed.areasAt(observed.tileIndex(x,y)).forbidden & teamMask)!=0;
}



bool MapInfo::is_guard_area(int x, int y)
{
    const auto& observed=observation();
    return (observed.areasAt(observed.tileIndex(x,y)).guard & teamMask)!=0;
}



bool MapInfo::is_clearing_area(int x, int y)
{
    const auto& observed=observation();
    return (observed.areasAt(observed.tileIndex(x,y)).clear & teamMask)!=0;
}



bool MapInfo::is_farm_area(int x, int y)
{
    const auto& observed=observation();
    return (observed.areasAt(observed.tileIndex(x,y)).farm & teamMask)!=0;
}



bool MapInfo::farm_areas_enabled()
{
    const auto& observed=observation();
    return observed.farmAreasEnabled;
}



bool MapInfo::can_paint_farm(int x, int y)
{
    const auto& observed=observation();
    return observed.canPaintFarmAt(observed.tileIndex(x,y));
}



bool MapInfo::is_discovered(int x, int y)
{
    const auto& observed=observation();
    return (observed.visibilityAt(observed.tileIndex(x,y)).discovered & teamMask)!=0;
}



bool MapInfo::is_resource(int x, int y, int type)
{
    const auto& observed=observation();
    return MapState::hasMaterialSlot(observed.state(),observed.tileIndex(x,y),type);
}



bool MapInfo::is_resource(int x, int y)
{
    const auto& observed=observation();
    return observed.resourceAt(observed.tileIndex(x,y)).resource.type!=NO_RES_TYPE;
}



bool MapInfo::is_water(int x, int y)
{
    const auto& observed=observation();
    return observed.terrainPropertiesAt(observed.tileIndex(x,y)).swimmable;
}



bool MapInfo::is_sand(int x, int y)
{
    const auto& observed=observation();
    return observed.terrainPropertiesAt(observed.tileIndex(x,y)).inhibitionQ8!=0;
}



bool MapInfo::is_resource_habitat(int x, int y, int resource)
{
    const auto& observed=observation();
    return MapState::terrainSupportsMaterial(observed.state(),observed.tileIndex(x,y),resource);
}

bool MapInfo::is_crop_habitat(int x, int y)
{
    const auto& observed=observation();
    return MapState::terrainSupportsMaterial(observed.state(),observed.tileIndex(x,y),MaterialId::Food);
}

bool MapInfo::is_grass(int x, int y)
{
    const auto& observed=observation();
    return observed.terrainPropertiesAt(observed.tileIndex(x,y)).buildable;
}



bool MapInfo::backs_onto_sand(int x, int y)
{
    const auto& observed=observation();
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if((dx || dy) && observed.terrainPropertiesAt(observed.tileIndex(x+dx,y+dy)).shoreline)
                return true;
        return false;

}



int MapInfo::get_amount_resource(int x, int y)
{
    const auto& observed=observation();
    return observed.resourceAt(observed.tileIndex(x,y)).resource.amount;
}
