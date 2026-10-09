// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "PowerOfTwo.h"
#include "OverlayAreas.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include "OverlayFill.h"
#include "BuildingType.h"
#include "Bullet.h"
#include "sim/snapshot/WorldSnapshot.h"

// Radius (in tiles) of the bump painted for each unit on the Starving and
// Damage overlays. Units contribute equally, so this is a fixed footprint.
static const int UNIT_OVERLAY_RADIUS = 8;

OverlayArea::OverlayArea()
{
    type=None;width=height=0;
	lasttype = None;
	overlaymax = 0;
}


OverlayArea::~OverlayArea()
{

}


Uint32 OverlayArea::getValue(int x, int y) const
{
	return overlay ? overlay[x * height + y] : 0;
}


	
Uint32 OverlayArea::getMaximum() const
{
	return overlaymax;
}



OverlayArea::OverlayType OverlayArea::getOverlayType() const
{
	return type;
}



void OverlayArea::forceRecompute()
{
	lasttype = None;
}



size_t OverlayArea::chunks(const SimulationSnapshot::Handle& world,OverlayType requested)
{
    const size_t clearChunks=(size_t(world.width)*world.height+1023)/1024;
    if (requested==Starving || requested==Damage) return 1+clearChunks+(world.entities->units.size()+15)/16;
    if (requested==Defence) return 1+clearChunks+(world.entities->buildings.size()+15)/16;
    if (requested==Fertility) return 1+(size_t(world.width)*world.height+1023)/1024;
    return 1;
}
void OverlayArea::compute(const SimulationSnapshot::Handle& world,OverlayType requested,int localteam,Uint16 fertilityMaximum)
{
    for (size_t chunk=0;chunk<chunks(world,requested);++chunk)
        while (!computeChunk(world,requested,localteam,fertilityMaximum,chunk)) {}
}
bool OverlayArea::computeChunk(const SimulationSnapshot::Handle& world,OverlayType requested,int localteam,Uint16 fertilityMaximum,size_t chunk)
{
    if (chunk==0) {
        type=requested;width=world.width;height=world.height;
        // Allocation does not initialize the whole map in one scheduler claim.
        overlay.reset(requested==None ? nullptr : new Uint32[size_t(width)*height]);overlaymax=0;lasttype=type;
        activeChunk=std::numeric_limits<size_t>::max();
        return true;
    }
    --chunk;
    const size_t cells=size_t(width)*height;
    std::span<Uint32> field(overlay.get(),cells);
    if (type==Starving || type==Damage || type==Defence) {
        const size_t clearChunks=(cells+1023)/1024;
        if (chunk<clearChunks) {
            const size_t first=chunk*1024;
            std::fill_n(overlay.get()+first,std::min(size_t(1024),cells-first),0);
            return true;
        }
        chunk-=clearChunks;
    }
    if (type==Starving || type==Damage) {
        const auto& units=world.entities->units;
        for (size_t i=chunk*16;i<std::min(units.size(),(chunk+1)*16);++i) {
            const auto& u=units[i];
            if (u.team!=localteam || u.activity==UnitState::ACT_UPGRADING) continue;
            const bool hungry=!world.rules->values.hungerDisabled
                && u.hungry<=(u.carriedMaterial==-1 ? u.trigHungry : u.trigHungryCarrying);
            if ((type==Starving && hungry && u.hp<u.performance[HP]) || (type==Damage && u.medical==UnitState::MED_DAMAGED))
                OverlayFill::increasePoint(u.posX,u.posY,UNIT_OVERLAY_RADIUS,width,height,field,overlaymax);
        }
    } else if (type==Defence) {
        const auto& buildings=world.entities->buildings;
        if (activeChunk!=chunk) {activeChunk=chunk;buildingCursor=chunk*16;kernelCursor=0;}
        // A single large turret is resumed across claims; small groups also yield.
        size_t budget=1024;
        while (buildingCursor<std::min(buildings.size(),(chunk+1)*16)) {
            const auto& b=buildings[buildingCursor];
            const auto& definition=world.catalogs->buildings->at(b.typeNum).resolvedType;
            if (b.team==localteam && definition.semantics.projectileDamage[WARRIOR]>0) {
                const int power=int(std::min<std::int64_t>(std::numeric_limits<int>::max(),
                    (std::int64_t(definition.semantics.projectileDamage[WARRIOR])*definition.shootRhythm)>>SHOOTING_COOLDOWN_MAGNITUDE));
                const size_t before=kernelCursor;
                const bool complete=OverlayFill::spreadPointChunk(b.posX,b.posY,power,definition.shootingRange,
                    width,height,field,overlaymax,kernelCursor,budget);
                budget-=kernelCursor-before;
                if (!complete) return false;
            }
            ++buildingCursor;kernelCursor=0;
            if (!budget) return buildingCursor==std::min(buildings.size(),(chunk+1)*16);
        }
    } else if (type==Fertility) {
        const auto count=size_t(width)*height;
        for (size_t i=chunk*1024;i<std::min(count,(chunk+1)*1024);++i) {
            const size_t x=i/height,y=powerOfTwoRemainder(i, height);
            overlay[i]=world.resources->cells[y*width+x].fertility;
        }
        overlaymax=fertilityMaximum;
    }
    return true;
}
