// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "YOGClientGameConnectionDialog.h"
#include "GUIText.h"
#include "StringTable.h"
#include "Toolkit.h"

using namespace GAGCore;
using namespace GAGGUI;

YOGClientGameConnectionDialog::YOGClientGameConnectionDialog(GraphicContext *parentCtx, std::shared_ptr<MultiplayerGame> game)
	: OverlayScreen(parentCtx, 200, 100), parentCtx(parentCtx), game(game)
{
	addWidget(new Text(0, 20, ALIGN_FILL, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[connecting to game]")));
	game->addEventListener(this);
	dispatchInit();
}


YOGClientGameConnectionDialog::~YOGClientGameConnectionDialog()
{
	game->removeEventListener(this);
}


void YOGClientGameConnectionDialog::onAction(Widget *source, Action action, int par1, int par2)
{

}


void YOGClientGameConnectionDialog::execute()
{
	executeModal(parentCtx);
}


void YOGClientGameConnectionDialog::onTimer(Uint32)
{
	updateGame();
}


void YOGClientGameConnectionDialog::updateGame()
{
	game->update();
	if(game->isFullyInGame())
		endValue = Success;
}



void YOGClientGameConnectionDialog::handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event)
{
	Uint8 type = event->getEventType();
	if(type == MGEGameRefused)
	{
		endValue = Failed;
	}
	else if(type == MGEServerDisconnected)
	{
		endValue = Failed;
	}
}
