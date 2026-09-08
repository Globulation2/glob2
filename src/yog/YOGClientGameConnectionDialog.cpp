// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "YOGClientGameConnectionDialog.h"
#include "GUIText.h"
#include "StringTable.h"
#include "Toolkit.h"
#include <GUIButton.h>

using namespace GAGCore;
using namespace GAGGUI;

YOGClientGameConnectionDialog::YOGClientGameConnectionDialog(std::shared_ptr<MultiplayerGame> game)
	: game(game)
{
	addWidget(new Text(0, 200, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[connecting to game]")));
	addWidget(new TextButton(240, 280, 160, 35, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
		Toolkit::getStringTable()->getString("[Cancel]"), Cancelled, 27));
	game->addEventListener(this);
}


YOGClientGameConnectionDialog::~YOGClientGameConnectionDialog()
{
	game->removeEventListener(this);
}


void YOGClientGameConnectionDialog::onAction(Widget *source, Action action, int par1, int par2)
{
	if(action == BUTTON_RELEASED || action == BUTTON_SHORTCUT) endExecute(Cancelled);
}


void YOGClientGameConnectionDialog::onTimer(Uint32)
{
	updateGame();
}


void YOGClientGameConnectionDialog::updateGame()
{
	game->update();
	if(game->isFullyInGame())
		endExecute(Success);
}



void YOGClientGameConnectionDialog::handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event)
{
	Uint8 type = event->getEventType();
	if(type == MGEGameRefused)
	{
		endExecute(Failed);
	}
	else if(type == MGEServerDisconnected)
	{
		endExecute(Failed);
	}
}
