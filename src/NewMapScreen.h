// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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

