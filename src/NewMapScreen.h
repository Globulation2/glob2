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

#ifndef __NEWMAPSCREEN_H
#define __NEWMAPSCREEN_H

#include "Glob2Screen.h"
#include "MapGenerationDescriptor.h"

namespace GAGGUI
{
	class Number;
	class Text;
	class Ratio;
	class List;
}

//! This screen allows to choose the size of the map and the default background
class NewMapScreen : public Glob2Screen
{
public:
	enum
	{
		OK = 1,
		CANCEL = 2
	};
public:
	MapGenerationDescriptor descriptor;

private:
	Number *mapSizeX, *mapSizeY;
	List *methodes, *terrains;
	Ratio *waterRatio, *sandRatio, *grassRatio, *desertRatio;
	Ratio *wheatRatio, *woodRatio, *stoneRatio, *algaeRatio, *craterDensity;
	Ratio *riverDiameter, *fruitRatio;
	Number *smooth, *extraIslands;
	Number *nbTeams;
	Ratio *oldIslandSize;
	Number *oldBeach;
	Number *nbWorkers;
	Number *logRepeatAreaTimes;
	Text *numberOfTeamText, *numberOfWorkerText, *craterDensityText, *extraIslandsText;
	Text *ratioText, *waterText, *sandText, *grassText, *desertText, *wheatText, *woodText, *stoneText, *algaeText, *fruitText, *smoothingText, *riverDiameterText, *areaTimesText;
	Text *oldIslandSizeText, *oldBeachSizeText;


public:
	//! Constructor
	NewMapScreen();
	//! Destructor
	virtual ~NewMapScreen() { };
	//! Action handler
	void onAction(Widget *source, Action action, int par1, int par2);
};

#endif
