/*
  Copyright (C) 2007-2008 Bradley Arsenault

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


#include "YOGClientGameConnectionDialog.h"
#include "GUIText.h"
#include <iomanip>
#include "Map.h"
#include <sstream>
#include "StringTable.h"
#include "Toolkit.h"

using namespace GAGCore;
using namespace GAGGUI;

YOGClientGameConnectionDialog::YOGClientGameConnectionDialog(GraphicContext *parentCtx, boost::shared_ptr<MultiplayerGame> game)
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
	// save screen in a temporary surface
	parentCtx->setClipRect();
	DrawableSurface *background = new DrawableSurface(parentCtx->getW(), parentCtx->getH());
	background->drawSurface(0, 0, parentCtx);

	dispatchPaint();

	SDL_Event event;
	while(endValue<0)
	{
		Sint32 time = SDL_GetTicks();
		while (SDL_PollEvent(&event))
		{
			if (event.type==SDL_QUIT)
				break;
			//Manual integration of cmd+q and alt f4
			if(event.type == SDL_KEYDOWN)
			{
#					ifdef USE_OSX
				SDL_Keymod modState = SDL_GetModState();
				if(event.key.keysym.sym == SDLK_q && modState & KMOD_GUI)
				{
					break;
				}
#					else
				SDL_GetModState();
#					endif
#					ifdef USE_WIN32
				SDL_Keymod modState = SDL_GetModState();
				if(event.key.keysym.sym == SDLK_F4 && modState & KMOD_ALT)
				{
					break;
				}
#					endif
			}

			translateAndProcessEvent(&event);
		}
		dispatchPaint();
		parentCtx->drawSurface((int)0, (int)0, background);
		parentCtx->drawSurface(decX, decY, getSurface());
		parentCtx->nextFrame();
		updateGame();
		Sint32 newTime = SDL_GetTicks();
		SDL_Delay(std::max(40 - newTime + time, 0));
	}
	
	delete background;
}


void YOGClientGameConnectionDialog::updateGame()
{
	game->update();
	if(game->isFullyInGame())
		endValue = Success;
}



void YOGClientGameConnectionDialog::handleMultiplayerGameEvent(boost::shared_ptr<MultiplayerGameEvent> event)
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
