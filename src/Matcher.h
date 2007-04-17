/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charriere
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

class Unit;
class Building;
class Team;
class Map;

//! The Matcher is the system which tries to make the best matching between Units and Buildings.
class Matcher
{
public:
	// Lists of flags or buildings for units to work on/in :
	std::list<Building *> foodable; //!< to bring food to
	std::list<Building *> fillable; //!< to bring resources to
	std::list<Building *> zonableWorkers[2]; //!< to be built by workers who can ([1]) or needn't ([0]) swim.
	std::list<Building *> zonableExplorer; //!< to be visited by Explorers.
	std::list<Building *> zonableWarrior; //!< to be visited by Warriors.
	std::list<Building *> upgrade[NB_ABILITY]; //!< to upgrade the units' abilities.
	
	// The lists of building which have one specific ability.
	std::list<Building *> canFeedUnit; //!< inns with enough food and free room.
	std::list<Building *> canHealUnit; //!< hospitals with free room
	std::list<Building *> canExchange; //!< markets
	
	// The lists of the free units
	std::list<Unit *> workers; //!< free workers to be allocated
	std::list<Unit *> explorers; //!< free explorers to be allocated
	std::list<Unit *> warrior; //!< free warriors to be allocated
	
	Team *team;
	Map *map;
	
protected:
	bool canReach(Unit *unit, Building *target, Uint32 *dist);
	
public:
	Matcher(Team *team);
	
	void matchWorkers();
	void match();
};

#endif
