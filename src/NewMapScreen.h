/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __NEWMAPSCREEN_H
#define __NEWMAPSCREEN_H

#include "Map.h"
#include "MapGenerationDescriptor.h"
#include <GAG.h>

//! This screen allows to choose how to make a new map
class HowNewMapScreen:public Screen
{
public:
	enum
	{
		NEW = 1,
		LOAD = 2,
		CANCEL = 3
	};

public:
	//! Constructor
	HowNewMapScreen();
	//! Destructor
	virtual ~HowNewMapScreen() { }
	//! Action handler
	void onAction(Widget *source, Action action, int par1, int par2);
	//! Paint handler
	void paint(int x, int y, int w, int h);
};

//! This screen allows to choose the size of the map and the default background
class NewMapScreen:public Screen
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
	Ratio *waterRatio, *sandRatio, *grassRatio;
	Number *smooth;
	Number *nbTeams;
	Ratio *islandsSize;
	Number *beach;
	Number *nbWorkers;
	Text *numberOfTeamText, *numberOfWorkerText;
	Text *ratioText, *waterText, *sandText, *grassText, *smoothingText;
	Text *islandSizeText, *beachSizeText;

public:
	//! Constructor
	NewMapScreen();
	//! Destructor
	virtual ~NewMapScreen() { }
	//! Action handler
	void onAction(Widget *source, Action action, int par1, int par2);
	//! Paint handler
	void paint(int x, int y, int w, int h);
};

#endif
