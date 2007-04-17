/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "UnitEditorScreen.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "UnitsSkins.h"

#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIButton.h>
#include <StringTable.h>

UnitEditorScreen::UnitEditorScreen(Unit *toEdit) :
	OverlayScreen(globalContainer->gfx, 300, 400)
{
	assert(toEdit);
	unit = toEdit;
	
	// window title
	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Unit editor]")));
	
	// parameters
	int ypos = 50;
	addWidget(new Text(10, ypos, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[skin]")));
	skin = new MultiTextButton(10, ypos, 100, 25, ALIGN_RIGHT, ALIGN_TOP, "standard", "", -1);
	addWidget(skin);
// 	
	ypos += 30;
	addWidget(new Text(10, ypos, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[hungryness]")));
	hungryness = new TextInput(10, ypos, 100, 25, ALIGN_RIGHT, ALIGN_TOP, "standard", "");
	addWidget(hungryness);
	
	// ok / cancel
	addWidget(new TextButton(10, 10, 135, 40, ALIGN_LEFT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13));
	addWidget(new TextButton(10, 10, 135, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));
	
	// important, widgets must be initialised by hand as we use custom event loop
	dispatchInit();
	
	// change widgets's properties
	globalContainer->unitsSkins->buildSkinsList(skin);
	skin->setIndexFromText(unit->skinName);
	hungryness->setText(unit->hungryness);
}

UnitEditorScreen::~UnitEditorScreen()
{

}

void UnitEditorScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == OK)
		{
			endValue = par1;
			unit->skinName = skin->getText();
			unit->skinPointerFromName();
			unit->hungryness = hungryness->getText<Sint32>();
		}
		else if (par1 == CANCEL)
		{
			endValue = par1;
		}
	}/*
	if (action==TEXT_ACTIVATED)
	{
		if (source==skin)
			hungryness->deactivate();
		else if (source==hungryness)
			skin->deactivate();
	}*/
}
