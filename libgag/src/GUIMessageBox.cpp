/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#include <GUIMessageBox.h>
#include <GUIBase.h>
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <algorithm>

using namespace GAGCore;

namespace GAGGUI
{
	class MessageBoxScreen:public OverlayScreen
	{
	public:
		MessageBoxScreen(GraphicContext *parentCtx, const char *font, MessageBoxType type, const char *title, int titleWidth, int totCaptionWidth, int captionCount, int captionWidth[3], const char *captionArray[3]);
		virtual ~MessageBoxScreen() { }
		virtual void onAction(Widget *source, Action action, int par1, int par2);
	};
	
	MessageBoxScreen::MessageBoxScreen(GraphicContext *parentCtx, const char *font, MessageBoxType type, const char *title, int titleWidth, int totCaptionWidth, int captionCount, int captionWidth[3], const char *captionArray[3])
	:OverlayScreen(parentCtx, titleWidth > totCaptionWidth ? titleWidth : totCaptionWidth, 110)
	{
		addWidget(new Text(0, 20, ALIGN_FILL, ALIGN_LEFT, font, title));
	
		int dec;
		if (titleWidth>totCaptionWidth)
			dec=20+((titleWidth-totCaptionWidth)>>1);
		else
			dec=20;
		for (int i=0; i<captionCount; i++)
		{
			addWidget(new TextButton(dec, 53, captionWidth[i], 40, ALIGN_LEFT, ALIGN_LEFT, font, captionArray[i], i));
			dec+=20 + captionWidth[i];
		}
		dispatchInit();
	}
	
	void MessageBoxScreen::onAction(Widget *source, Action action, int par1, int par2)
	{
		if (action==BUTTON_RELEASED)
			endValue=par1;
	}
	
	int MessageBox(GraphicContext *parentCtx, const char *font, MessageBoxType type, const char *title, const char *caption1, const char *caption2, const char *caption3)
	{
		// for passing captions to class
		const char *captionArray[3]={
			caption1,
			caption2,
			caption3 };
	
		int captionWidth[3];
		memset(captionWidth, 0, sizeof(captionWidth));
		Font *fontPtr=Toolkit::getFont(font);
	
		// compute number of caption
		unsigned captionCount;
		if (caption3!=NULL)
		{
			captionCount = 3;
			captionWidth[2] = fontPtr->getStringWidth(captionArray[2])+40;
			captionWidth[1] = fontPtr->getStringWidth(captionArray[1])+40;
			captionWidth[0] = fontPtr->getStringWidth(captionArray[0])+40;
		}
		else if (caption2!=NULL)
		{
			captionCount = 2;
			captionWidth[1] = fontPtr->getStringWidth(captionArray[1])+40;
			captionWidth[0] = fontPtr->getStringWidth(captionArray[0])+40;
		}
		else
		{
			captionCount = 1;
			captionWidth[0] = fontPtr->getStringWidth(captionArray[0])+40;
		}
	
		int totCaptionWidth = captionWidth[0]+captionWidth[1]+captionWidth[2]+(captionCount-1)*20+40;
		int titleWidth =  fontPtr->getStringWidth(title)+20;
	
		MessageBoxScreen *mbs = new MessageBoxScreen(parentCtx, font, type, title, titleWidth, totCaptionWidth, captionCount, captionWidth, captionArray);
	
		// save screen in a temporary surface
		parentCtx->setClipRect();
		DrawableSurface *background = new DrawableSurface(parentCtx->getW(), parentCtx->getH());
		background->drawSurface(0, 0, parentCtx);
		
		mbs->dispatchPaint();
	
		SDL_Event event;
		while(mbs->endValue<0)
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

				mbs->translateAndProcessEvent(&event);
			}
			mbs->dispatchPaint();
			parentCtx->drawSurface((int)0, (int)0, background);
			parentCtx->drawSurface(mbs->decX, mbs->decY, mbs->getSurface());
			parentCtx->nextFrame();
			Sint32 newTime = SDL_GetTicks();
			SDL_Delay(std::max(40 - newTime + time, 0));
		}
	
		int retVal;
		if (mbs->endValue>=0)
			retVal=mbs->endValue;
		else
			retVal=-1;
	
		// clean up
		delete mbs;
		
		// restore screen and destroy temporary surface
		parentCtx->drawSurface(0, 0, background);
		delete background;
	
		return retVal;
	}
}
