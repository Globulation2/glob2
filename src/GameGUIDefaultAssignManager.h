/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef GameGUIDefaultAssignManager_h
#define GameGUIDefaultAssignManager_h

#include <map>

///This class manages the default number of units to be assigned when constructing a new buildings
class GameGUIDefaultAssignManager
{
public:
	///Constructs a GameGUIDefaultAssignManager
	GameGUIDefaultAssignManager();
	
	///Retrive the default assigned units for a given building typenum (note, not the 
	///ntBuildingType typenum, the BuildingTypes typenum)
	int getDefaultAssignedUnits(int typenum);
	
	///Sets the default assigned units for a given building typenum
	void setDefaultAssignedUnits(int typenum, int value);
	
private:
	std::map<int, int> unitCount;
};


#endif
