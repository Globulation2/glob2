// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <string>
#include "MapEdit.h"

void MapEdit::performAction(const std::string& action, int relMouseX, int relMouseY)
{
	if(action.find("&")!=std::string::npos)
	{
		int pos=action.find("&");
		performAction(action.substr(0, pos));
		performAction(action.substr(pos+1, action.size()-pos-1));
	}
	if(performViewAction(action, relMouseX, relMouseY))
		return;
	if(performTerrainAction(action, relMouseX, relMouseY))
		return;
	if(performUnitAction(action, relMouseX, relMouseY))
		return;
	performBuildingAction(action, relMouseX, relMouseY);
}
