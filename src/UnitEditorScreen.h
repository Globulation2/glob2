/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __UNIT_EDITOR_SCREEN_H
#define __UNIT_EDITOR_SCREEN_H

#include <GUIBase.h>

// forward declaration to optimise compilation speed
namespace GAGGUI
{
	class TextInput;
	class MultiTextButton;
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
	MultiTextButton *skin;
	TextInput *hungryness;
};

#endif
