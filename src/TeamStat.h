/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __TEAM_STAT_H
#define __TEAM_STAT_H

#include "Team.h"
#include "UnitType.h"

struct TeamStat
{
	int totalUnit;
	int numberPerType[UnitType::NB_UNIT_TYPE];
	int totalFree;
	int isFree[UnitType::NB_UNIT_TYPE];
	
	int needFood;
	int needHeal;
	int needNothing;
	int upgradeState[NB_ABILITY][4];

	int totalFood;
	int totalFoodCapacity;
	int totalUnitFoodable;
	int totalUnitFooded;
};

struct TeamSmoothedStat
{
	int totalFree;
	int isFree[UnitType::NB_UNIT_TYPE];
};

class TeamStats
{
public:
	TeamStats();
	virtual ~TeamStats(void);
	
	void step(Team *team);
	void drawText();
	void drawStat();
	int getFreeUnits();

private:
	static const int STATS_SMOOTH_SIZE=32;
	static const int STATS_SIZE=128;
	
	int statsIndex;
	TeamStat stats[STATS_SIZE];
	
	int smoothedIndex;
	TeamSmoothedStat smoothedStats[STATS_SMOOTH_SIZE];
};

#endif
