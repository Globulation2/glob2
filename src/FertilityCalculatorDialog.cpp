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

#include "FertilityCalculatorDialog.h"
#include "Toolkit.h"
#include "StringTable.h"

#include "Map.h"
#include "GUIText.h"
#include "FertilityCalculatorThreadMessage.h"

#include <sstream>
#include <iomanip>

using namespace GAGCore;
using namespace GAGGUI;
using namespace boost;

FertilityCalculatorDialog::FertilityCalculatorDialog(GraphicContext *parentCtx, Map& map)
	: OverlayScreen(parentCtx, 200, 100), map(map), parentCtx(parentCtx), thread(map, incoming, incomingMutex)
{
		addWidget(new Text(0, 20, ALIGN_FILL, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[Computing Fertility]")));
		percentDone = new Text(0, 40, ALIGN_FILL, ALIGN_LEFT, "menu");
		addWidget(percentDone);
		dispatchInit();
}



void FertilityCalculatorDialog::onAction(Widget *source, Action action, int par1, int par2)
{

}


void FertilityCalculatorDialog::execute()
{
	// save screen in a temporary surface
	parentCtx->setClipRect();
	DrawableSurface *background = new DrawableSurface(parentCtx->getW(), parentCtx->getH());
	background->drawSurface(0, 0, parentCtx);

	// start computing
	boost::thread new_thread(boost::ref(thread));

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
				SDLMod modState = SDL_GetModState();
#					ifdef USE_OSX
				if(event.key.keysym.sym == SDLK_q && modState & KMOD_META)
				{
					break;
				}
#					endif
#					ifdef USE_WIN32
				if(event.key.keysym.sym == SDLK_F4 && modState & KMOD_ALT)
				{
					break;
				}
#					endif
			}

			translateAndProcessEvent(&event);
		}
		proccessIncoming(background);
		dispatchPaint();
		parentCtx->drawSurface((int)0, (int)0, background);
		parentCtx->drawSurface(decX, decY, getSurface());
		parentCtx->nextFrame();
		Sint32 newTime = SDL_GetTicks();
		SDL_Delay(std::max(40 - newTime + time, 0));
	}
	
	delete background;
}



void FertilityCalculatorDialog::proccessIncoming(DrawableSurface *background)
{
	//First parse incoming thread messages
	boost::recursive_mutex::scoped_lock lock(incomingMutex);
	while(!incoming.empty())
	{
		boost::shared_ptr<FertilityCalculatorThreadMessage> message = incoming.front();
		incoming.pop();
		Uint8 type = message->getMessageType();
		switch(type)
		{
			case FCTMUpdateCompletionPercent:
			{
					boost::shared_ptr<FCTUpdateCompletionPercent> info = static_pointer_cast<FCTUpdateCompletionPercent>(message);
					std::stringstream s;
					s<<std::setprecision(3)<<(info->getPercent() * 100.0)<<"%"<<std::endl;
					percentDone->setText(s.str());
			}
			break;
			case FCTMFertilityCompleted:
			{
					boost::shared_ptr<FCTFertilityCompleted> info = static_pointer_cast<FCTFertilityCompleted>(message);
					endValue = 1;
			}
			break;
		}
	}
}

