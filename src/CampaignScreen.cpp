/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "CampaignScreen.h"
#include "GlobalContainer.h"
#include <iostream>
#include <sstream>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUITextArea.h>
using namespace GAGGUI;
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;

CampaignScreen::CampaignScreen(const std::string &text)
{
	
	addWidget(new TextButton(20, 20, 70, 25, ALIGN_RIGHT, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Start]"), 0, std::string("Démarrer la partie"), "standard", '\r'));

	addWidget(new TextButton(20, 20, 70, 25, ALIGN_LEFT, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Cancel]"), 1, 27));
	
	
	addWidget(new TextArea(20, 20, 20, 60, ALIGN_FILL, ALIGN_FILL, "standard", true, text.c_str()));
}

void CampaignScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endExecute(par1);
}
