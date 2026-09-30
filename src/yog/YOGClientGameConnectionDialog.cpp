// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault
#include "YOGClientGameConnectionDialog.h"

using namespace Glob2UI;

YOGClientGameConnectionDialog::YOGClientGameConnectionDialog(std::shared_ptr<MultiplayerGame> game)
	: game(game)
{
	game->addEventListener(this);
}

YOGClientGameConnectionDialog::~YOGClientGameConnectionDialog()
{
	game->removeEventListener(this);
}

Element YOGClientGameConnectionDialog::build(const Presentation &p)
{
	return page("", center(paragraph(tr("[connecting to game]"), {FontRole::Body, false, TextAlign::Center})),
				actions({{"cancel", tr("[Cancel]"), [this] { endExecute(Cancelled); }, false, SDLK_ESCAPE}}, p), p, 480);
}

void YOGClientGameConnectionDialog::onTimer(Uint32) { updateGame(); }

void YOGClientGameConnectionDialog::updateGame()
{
	game->update();
	if (game->isFullyInGame())
		endExecute(Success);
}

void YOGClientGameConnectionDialog::handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event)
{
	const Uint8 type = event->getEventType();
	if (type == MGEGameRefused || type == MGEServerDisconnected)
		endExecute(Failed);
}
