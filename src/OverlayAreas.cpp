// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "OverlayAreas.h"
#include <algorithm>
#include "OverlayFill.h"
#include "Unit.h"
#include "BuildingType.h"
#include "Game.h"
#include "Bullet.h"

// Radius (in tiles) of the bump painted for each unit on the Starving and
// Damage overlays. Units contribute equally, so this is a fixed footprint.
static const int UNIT_OVERLAY_RADIUS = 8;

OverlayArea::OverlayArea()
{
	lasttype = None;
	overlaymax = 0;
//	fertilitymax = 0;
//	fertilityComputed = 0;
}


OverlayArea::~OverlayArea()
{

}


void OverlayArea::compute(Game& game, OverlayType ntype, int localteam)
{
	type = ntype;
	height = game.map.getH();
	width = game.map.getW();
	overlay.resize(game.map.getW() * game.map.getH());
	if(type == Starving || type == Damage)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlaymax = 0;
		for (int i=0; i<Unit::MAX_COUNT; i++)
		{
			Unit *u=game.teams[localteam]->myUnits[i];
			if (u && u->activity != Unit::ACT_UPGRADING)
			{
				if (type == Starving && u->isUnitHungry() && u->hp < u->performance[HP])
				{
					OverlayFill::increasePoint(u->posX, u->posY, UNIT_OVERLAY_RADIUS, width, height, overlay, overlaymax);
				}
				else if(type == Damage && u->medical==Unit::MED_DAMAGED)
				{
					OverlayFill::increasePoint(u->posX, u->posY, UNIT_OVERLAY_RADIUS, width, height, overlay, overlaymax);
				}
			}
		}
	}
	else if(type == Defence)
	{
		std::fill(overlay.begin(), overlay.end(), 0);
		overlaymax = 0;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b = game.teams[localteam]->myBuildings[i];
			if (b)
			{
				if(b->type->shootDamage > 0)
				{
					int power = (b->type->shootDamage*b->type->shootRythme) >> SHOOTING_COOLDOWN_MAGNITUDE;
					OverlayFill::spreadPoint(b->posX, b->posY, power, b->type->shootingRange, width, height, overlay, overlaymax);
				}
			}


		}
	}
	else if(type == Fertility && lasttype != Fertility)
	{
		for(int x=0; x<game.map.getW(); ++x)
		{
			for(int y=0; y<game.map.getH(); ++y)
			{
				overlay[x * height + y] = game.map.getCase(x, y).fertility;
				overlaymax = game.map.fertilityMaximum;
			}
		}
	}
	lasttype = type;
}



Uint32 OverlayArea::getValue(int x, int y)
{
	return overlay[x * height + y];
}


	
Uint32 OverlayArea::getMaximum()
{
	return overlaymax;
}



OverlayArea::OverlayType OverlayArea::getOverlayType()
{
	return type;
}



void OverlayArea::forceRecompute()
{
	lasttype = None;
}


