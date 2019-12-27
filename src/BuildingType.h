/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __BULDING_TYPE_H
#define __BULDING_TYPE_H

#include <vector>

#include "ConfigFiles.h"
#include "UnitConsts.h"
#include "Ressource.h"

class BuildingType: public LoadableFromConfigFile
{
public:
	// basic infos
	std::string type;

	// visualisation
	std::string gameSprite;
	Sint32 gameSpriteImage;
	Sint32 gameSpriteCount;
	std::string miniSprite;
	Sint32 miniSpriteImage;

	Sint32 hueImage; // bool. The way we show the building's team (false=we draw a flag, true=we hue all the sprite)
	Sint32 flagImage;
	Sint32 crossConnectMultiImage; // If true, mean we have a wall-like building

	// could be Uint8, if non 0 tell the number of maximum units locked by bulding for:
	// by order of priority (top = max)
	Sint32 upgrade[NB_ABILITY]; // What kind on units can be upgraded here
	Sint32 upgradeTime[NB_ABILITY]; // Time to upgrade an unit, given the upgrade type needed.
	Sint32 upgradeInParallel; // if true, can learn all upgardes with one learning time into the building
	Sint32 foodable;
	Sint32 fillable;
	Sint32 zonable[NB_UNIT_TYPE]; // If an unit is required for a presence.
	Sint32 zonableForbidden;

	Sint32 canFeedUnit;
	Sint32 timeToFeedUnit;
	Sint32 canHealUnit;
	Sint32 timeToHealUnit;
	Sint32 insideSpeed;
	Sint32 canExchange;
	Sint32 useTeamRessources;

	Sint32 width, height; // Uint8, size in square
	Sint32 decLeft, decTop;
	Sint32 isVirtual; // bool, doesn't occupy ground occupation map, used for war-flag and exploration-flag.
	Sint32 isCloacked; // bool, graphicaly invisible for enemy.
	//Sint32 *walkOverMap; // should be allocated and deleted in a cleany way
	//Sint32 walkableOver; // bool, can walk over
	Sint32 shootingRange; // Uint8, if 0 can't shoot
	Sint32 shootDamage; // Uint8
	Sint32 shootSpeed; // Uint8, the actual speed at which the shots fly through the air.
	Sint32 shootRythme;		// Uint8, The frequency with which a tower fires. It fires once every
							// SHOOTING_COOLDOWN_MAX/shootRythme ticks.
	Sint32 maxBullets;
	Sint32 multiplierStoneToBullets; //The tower gets this many bullets every time a worker delivers stone to it.

	Sint32 unitProductionTime; // Uint8, nb tick to produce one unit
	Sint32 ressourceForOneUnit; // The amount of wheat consumed in the production of a unit.

	Sint32 maxRessource[MAX_NB_RESSOURCES];
	Sint32 multiplierRessource[MAX_NB_RESSOURCES];
	Sint32 maxUnitInside;
	Sint32 maxUnitWorking;

	Sint32 hpInit;	// (Uint16) Initial HP of the building. This is generally equal to hpMax for completed buildings,
					// equal to 1 for newly created buildings, and equal to the hpMax of the original building for
					// upgrading buildings.
	Sint32 hpMax;
	Sint32 hpInc;	// The amount by which the building's hitpoints are incremented when a resource is added to it,
					// for buildings under construction.
	Sint32 armor;	// (Uint8) Any damage the building takes is reduced by this much, although it has a minumum of 1
					// for most damage, 0 only for Explorers.
	Sint32 level; // (Uint8)
	Sint32 shortTypeNum; // BuildingTypeShortNumber, Should not be used by the main engine, but only to choose the next level building.
	Sint32 isBuildingSite;

	// Flag usefull
	Sint32 defaultUnitStayRange;
	Sint32 maxUnitStayRange;

	Sint32 viewingRange;
	Sint32 regenerationSpeed;

	Sint32 prestige;

	// Regenerated parameters
	Sprite *gameSpritePtr;
	Sprite *miniSpritePtr;
	int prevLevel;
	int nextLevel;

public:
	BuildingType();
	virtual ~BuildingType() { }
	virtual void loadFromConfigFile(const ConfigBlock *configBlock);
	Uint32 checkSum(void);
};

#endif

