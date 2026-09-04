// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
