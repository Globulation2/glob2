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

#include "MultiplayersOfferScreen.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <StringTable.h>

MultiplayersOfferScreen::MultiplayersOfferScreen()
{
	addWidget(new TextButton(150,  25, 340, 40, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[host]"), HOST));
	addWidget(new TextButton(150,  90, 340, 40, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[join a game]"), JOIN));
	addWidget(new TextButton(150, 415, 340, 40, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), QUIT, 27));

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}

MultiplayersOfferScreen::~MultiplayersOfferScreen()
{
	/*delete font;
	delete arch;*/
}

void MultiplayersOfferScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endExecute(par1);
	}
}

void MultiplayersOfferScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	//gfxCtx->drawSprite(0, 0, arch, 0);
}

int MultiplayersOfferScreen::menu(void)
{
	return MultiplayersOfferScreen().execute(globalContainer->gfx, 30);
}
