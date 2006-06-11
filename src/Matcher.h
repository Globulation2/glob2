/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charriere
  for any question or comment contact us at nct at ysagoon dot com or
  nuage at ysagoon dot com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __MATCHER_H
#define __MATCHER_H

#include <list>
#include <GAGSys.h>
#include "UnitConsts.h"
#include "MatcherConsts.h"

class Unit;
class Building;
class Team;
class Map;

//! The Matcher is the system which tries to make the best matching between Units and Buildings.
class Matcher
{
public:
	// Flags or buildings for units to work on/in :
	std::list<Building *> fillable; //!< to bring resources to
	std::list<Building *> zonableWorkers; //!< to be visited by Workers.
	std::list<Building *> zonableExplorers; //!< to be visited by Explorers.
	std::list<Building *> zonableWarriors; //!< to be visited by Warriors.
	std::list<Building *> upgrade[NB_ABILITY]; //!< to upgrade the units' abilities.
	
	// Buildings which have one specifics abilites.
	std::list<Building *> canFeedUnit; //!< inns with enough food and free room.
	std::list<Building *> canHealUnit; //!< hospitals with free room
	std::list<Building *> canExchange; //!< markets
	
	// Units in those lists are free and have NULL attachedBuilding
	std::list<Unit *> freeWorkers; //!< free workers to be allocated
	std::list<Unit *> freeExplorers; //!< free explorers to be allocated
	std::list<Unit *> freeWarriors; //!< free warriors to be allocated
	std::list<Unit *> freeUnits; //!< all free units to be allocated
	
	// Units in need and have NULL attachedBuilding
	std::list<Unit *> hungryUnits; //!< units looking for inns
	std::list<Unit *> damagedUnits; //!< units looking for hospitals
	
	Team *team;
	Map *map;
	
protected:
	bool canReachGround(Unit *unit, Building *target, Uint32 *dist, const bool ignoreHungryness);
	bool canReachAir(Unit *unit, Building *target, Uint32 *dist, const bool ignoreHungryness);
	bool canReach(Unit *unit, Building *building, Uint32 *dist, const bool ignoreHungryness);
	
	void matchFree();
	void matchHungry();
	void matchDamaged();
	
public:
	Matcher(Team *team);
	
	void match();
	
	void addFreeUnit(Unit *unit);
	void addHungryUnit(Unit *unit);
	void addDamagedUnit(Unit *unit);
	
	void removeUnit(Unit *unit);
};

#endif
