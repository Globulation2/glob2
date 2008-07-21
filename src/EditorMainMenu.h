/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef EditorMainMenu_h
#define EditorMainMenu_h

#include "Glob2Screen.h"

namespace GAGGUI
{
}

//! This screen allows to choose how to make a new map
class EditorMainMenu : public Glob2Screen
{
public:
	enum
	{
		NEWMAP = 1,
		LOADMAP = 2,
		CANCEL = 3,
		NEWCAMPAIGN = 4,
		LOADCAMPAIGN = 5,
	};

public:
	//! Constructor
	EditorMainMenu();
	//! Destructor
	virtual ~EditorMainMenu() { }
	//! Action handler
	void onAction(Widget *source, Action action, int par1, int par2);
};







#endif
