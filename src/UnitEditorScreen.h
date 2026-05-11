// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GUIBase.h>

// forward declaration to optimise compilation speed
namespace GAGGUI
{
	class TextInput;
}
using namespace GAGGUI;
class Unit;

//! Allow the map editor user to change some unit parameters
class UnitEditorScreen : public OverlayScreen
{
public:
	enum
	{
		OK = 0,
		CANCEL = 1,
	};
	
public:
	UnitEditorScreen(Unit *toEdit);
	virtual ~UnitEditorScreen();
	
protected:
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	
protected:
	Unit *unit; //!< unit being edited
	TextInput *hungryness;
};

