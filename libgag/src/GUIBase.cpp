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

#include <GUIBase.h>
#include <assert.h>
#include <GraphicContext.h>
#include <cmath>

#define SCREEN_ANIMATION_FRAME_COUNT 10

using namespace GAGCore;

namespace GAGGUI
{
	namespace ColorTheme
	{
		// for green theme
		Color frontColor = Color(255, 255, 255);
		Color frontFrameColor = Color(0, 200, 100);
		Color listSelectedElementColor = Color(170, 170, 240);
		Color backColor = Color(0, 0, 0);
		Color backOverlayColor = Color(0, 0, 40);
	}
	
	// this function support base unicode (UCS16)
	void UCS16toUTF8(Uint16 ucs16, char utf8[4])
	{
		if (ucs16<0x80)
		{
			utf8[0]=static_cast<Uint8>(ucs16);
			utf8[1]=0;
		}
		else if (ucs16<0x800)
		{
			utf8[0]=static_cast<Uint8>(((ucs16>>6)&0x1F)|0xC0);
			utf8[1]=static_cast<Uint8>((ucs16&0x3F)|0x80);
			utf8[2]=0;
		}
		else if (ucs16<0xd800)
		{
			utf8[0]=static_cast<Uint8>(((ucs16>>12)&0x0F)|0xE0);
			utf8[1]=static_cast<Uint8>(((ucs16>>6)&0x3F)|0x80);
			utf8[2]=static_cast<Uint8>((ucs16&0x3F)|0x80);
			utf8[3]=0;
		}
		else
		{
			utf8[0]=0;
			fprintf(stderr, "GAG : UCS16toUTF8 : Error, can handle UTF16 characters\n");
		}
	}
	
	// this function support full unicode (UCS32)
	unsigned getNextUTF8Char(unsigned char c)
	{
		if (c>0xFC)
		{
			return 6;
		}
		else if (c>0xF8)
		{
			return 5;
		}
		else if (c>0xF0)
		{
			return 4;
		}
		else if (c>0xE0)
		{
			return 3;
		}
		else if (c>0xC0)
		{
			return 2;
		}
		else
		{
			return 1;
		}
	}
	
	unsigned getNextUTF8Char(const char *text, unsigned pos)
	{
		unsigned next=pos+getNextUTF8Char(text[pos]);
		assert(next<=strlen(text));
		return next;
	}
	
	unsigned getPrevUTF8Char(const char *text, unsigned pos)
	{
		// TODO : have a more efficient algo
		unsigned last=0, i=0;
		while (i<(unsigned)pos)
		{
			last=i;
			i+=getNextUTF8Char(text[i]);
		}
		return last;
	}
	
	Widget::Widget()
	{
		visible=true;
		parent=NULL;
	}
	
	Widget::~Widget()
	{
		
	}
	
	RectangularWidget::RectangularWidget()
	{
		x=y=w=h=hAlignFlag=vAlignFlag=0;
	}
	
	void RectangularWidget::show(void)
	{
		visible = true;
	}
	
	void RectangularWidget::hide(void)
	{
		visible = false;
	}
	
	void RectangularWidget::setVisible(bool newState)
	{
		if (newState)
			show();
		else
			hide();
	}
	
	float splineInterpolation(float T, float V0, float V1, float x)
	{
		assert(T > 0);
		float a = (-2 * (V1 - V0)) / (T * T * T);
		float b = (3 * (V1 - V0)) / (T * T);
		float c = 0;
		float d = V0;
		return a * (x * x * x) + b * (x * x) + c * x + d;
	}
	
	//! Interpolate from V0 to V1 on time T for value x, so that f(0) = V0, f(T) = V1, f'(0) = -1, f'(T) = 0
	float splineInterpolationFastStart(float T, float V0, float V1, float x)
	{
		assert(T > 0);
		float a = (2 * (V0 - V1 - T / 2)) / (T * T * T);
		float b = (1 / (2 * T)) - (3 * (V0 - V1 - T / 2)) / (T * T);
		float c = -1;
		float d = V0;
		return a * (x * x * x) + b * (x * x) + c * x + d;
	}
	
	void RectangularWidget::getScreenPos(int *sx, int *sy, int *sw, int *sh)
	{
		assert(sx);
		assert(sy);
		assert(sw);
		assert(sh);
		assert(parent);
		assert(parent->getSurface());
	
		int screenw = parent->getSurface()->getW();
		int screenh = parent->getSurface()->getH();
		
		switch (hAlignFlag)
		{
			case ALIGN_LEFT:
				*sx=x;
				*sw=w;
				break;
	
			case ALIGN_RIGHT:
				*sx=screenw-w-x;
				*sw=w;
				break;
	
			case ALIGN_FILL:
				*sx=x;
				*sw=screenw-w-x;
				break;
				
			case ALIGN_SCREEN_CENTERED:
				*sx=x+((screenw-640)>>1);
				*sw=w;
				break;
	
			default:
				assert(false);
		}
	
		switch (vAlignFlag)
		{
			case ALIGN_LEFT:
				*sy=y;
				*sh=h;
				break;
	
			case ALIGN_RIGHT:
				*sy=screenh-h-y;
				*sh=h;
				break;
	
			case ALIGN_FILL:
				*sy=y;
				*sh=screenh-h-y;
				break;
				
			case ALIGN_SCREEN_CENTERED:
				*sy=y+((screenh-480)>>1);
				*sh=h;
				break;
	
			default:
				assert(false);
		}
	}
	
	HighlightableWidget::HighlightableWidget()
	: totalAnimationTime(10)
	{
		assert(totalAnimationTime > 0);
		highlighted = false;
		prevHighlightValue = 0;
		nextHighlightValue = 0;
		actAnimationTime = 0;
		this->returnCode = 0;
	}
	
	HighlightableWidget::HighlightableWidget(Sint32 returnCode)
	: totalAnimationTime(10)
	{
		assert(totalAnimationTime > 0);
		highlighted = false;
		prevHighlightValue = 0;
		nextHighlightValue = 0;
		actAnimationTime = 0;
		this->returnCode = returnCode;
	}
	
	void HighlightableWidget::onSDLEvent(SDL_Event *event)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		if (event->type==SDL_MOUSEMOTION)
		{
			if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
			{
				if (!highlighted)
				{
					highlighted = true;
					parent->onAction(this, BUTTON_GOT_MOUSEOVER, returnCode, 0);
					prevHighlightValue = actAnimationTime/totalAnimationTime;
					nextHighlightValue = 1;
					actAnimationTime = totalAnimationTime-actAnimationTime;
				}
			}
			else
			{
				if (highlighted)
				{
					highlighted = false;
					parent->onAction(this, BUTTON_LOST_MOUSEOVER, returnCode, 0);
					prevHighlightValue = 1 - actAnimationTime/totalAnimationTime;
					nextHighlightValue = 0;
					actAnimationTime = totalAnimationTime-actAnimationTime;
				}
			}
		}
	}
	
	
	
	void HighlightableWidget::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		float actHighlight;
		if (actAnimationTime > 0)
		{
			// as actAnimationTime is decreasing over time, we have to invert prev and next highlight values
			actHighlight = splineInterpolation(totalAnimationTime,  nextHighlightValue, prevHighlightValue, actAnimationTime);
			actAnimationTime--;
		}
		else
			actHighlight = nextHighlightValue;
		
		parent->getSurface()->drawRect(x, y, w, h, ColorTheme::frontFrameColor);
		
		if (actHighlight > 0)
		{
			unsigned val = static_cast<unsigned>(255.0 * actHighlight);
			parent->getSurface()->drawRect(x+1, y+1, w-2, h-2, ColorTheme::frontColor.applyAlpha(val));
		}
	}
	
	Screen::Screen()
	{
		gfx = NULL;
		returnCode = 0;
		run = false;
	}
	
	Screen::~Screen()
	{
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			delete (*it);
		}
	}
	
	int Screen::execute(DrawableSurface *gfx, int stepLength)
	{
		Uint32 frameStartTime;
		Sint32 frameWaitTime;
		
		this->gfx = gfx;
	
		// init widgets
		dispatchInit();
		
		// create screen event
		onAction(NULL, SCREEN_CREATED, 0, 0);
		
		// draw screen
		dispatchPaint();
		run=true;
		
		while (run)
		{
			// get first timer
			frameStartTime=SDL_GetTicks();
			
			// send timer
			dispatchTimer(frameStartTime);
	
			// send events
			SDL_Event lastMouseMotion, windowEvent, event;
			bool hadLastMouseMotion=false;
			bool wasWindowEvent=false;
			while (SDL_PollEvent(&event))
			{
				switch (event.type)
				{
					case SDL_QUIT:
					{
						run=false;
						returnCode=-1;
						break;
					}
					break;
					case SDL_MOUSEMOTION:
					{
						hadLastMouseMotion=true;
						lastMouseMotion=event;
					}
					break;
					case SDL_ACTIVEEVENT:
					{
						windowEvent=event;
						wasWindowEvent=true;
					}
					break;
					case SDL_VIDEORESIZE:
					{
						gfx->setRes(event.resize.w, event.resize.h);
						onAction(NULL, SCREEN_RESIZED, gfx->getW(), gfx->getH());
					}
					break;
					default:
					{
						dispatchEvents(&event);
					}
					break;
				}
			}
			if (hadLastMouseMotion)
				dispatchEvents(&lastMouseMotion);
			if (wasWindowEvent)
				dispatchEvents(&windowEvent);
				
			// draw
			dispatchPaint();
	
			// wait timer
			frameWaitTime=SDL_GetTicks()-frameStartTime;
			frameWaitTime=stepLength-frameWaitTime;
			if (frameWaitTime>0)
				SDL_Delay(frameWaitTime);
		}
		
		// destroy screen event
		onAction(NULL, SCREEN_DESTROYED, 0, 0);
	
		return returnCode;
	}
	
	void Screen::endExecute(int returnCode)
	{
		run=false;
		this->returnCode=returnCode;
	}
	
	void Screen::addWidget(Widget* widget)
	{
		assert(widget);
		widget->parent=this;
		// this option enable or disable the multiple add check
		widgets.insert(widget);
	}
	
	void Screen::removeWidget(Widget* widget)
	{
		assert(widget);
		assert(widget->parent==this);
		widgets.erase(widget);
	}
	
	void Screen::dispatchEvents(SDL_Event *event)
	{
		onSDLEvent(event);
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			if ((*it)->visible)
				(*it)->onSDLEvent(event);
		}
	}
	
	void Screen::dispatchTimer(Uint32 tick)
	{
		onTimer(tick);
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			if ((*it)->visible)
				(*it)->onTimer(tick);
		}
	}
	
	void Screen::dispatchInit(void)
	{
		animationFrame = 0;
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			(*it)->init();
		}
	}
	
	void Screen::dispatchPaint(void)
	{
		assert(gfx);
		gfx->setClipRect();
		paint();
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			if ((*it)->visible)
				(*it)->paint();
		}
		gfx->nextFrame();
		
		if (animationFrame < SCREEN_ANIMATION_FRAME_COUNT)
			animationFrame++;
	}
	
	void Screen::paint(void)
	{
		gfx->drawFilledRect(0, 0, getW(), getH(), ColorTheme::backColor);
	}
	
	int Screen::getW(void)
	{
		if (gfx)
			return gfx->getW();
		else
			return 0;
	
	}
	
	int Screen::getH(void)
	{
		if (gfx)
			return gfx->getH();
		else
			return 0;
	}
	
	// Overlay screen, used for non full frame dialog
	
	OverlayScreen::OverlayScreen(GraphicContext *parentCtx, unsigned w, unsigned h)
	{
		gfx = new DrawableSurface(w, h);
		decX = (parentCtx->getW()-w)>>1;
		decY = (parentCtx->getH()-h)>>1;
		endValue = -1;
	}
	
	OverlayScreen::~OverlayScreen()
	{
		delete gfx;
	}
	
	int OverlayScreen::execute(DrawableSurface *gfx, int stepLength)
	{
		return Screen::execute(this->gfx, stepLength);
	}
	
	void OverlayScreen::translateAndProcessEvent(SDL_Event *event)
	{
		SDL_Event ev=*event;
		switch (ev.type)
		{
			case SDL_MOUSEMOTION:
				ev.motion.x-=static_cast<Uint16>(decX);
				ev.motion.y-=static_cast<Uint16>(decY);
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				ev.button.x-=static_cast<Uint16>(decX);
				ev.button.y-=static_cast<Uint16>(decY);
				break;
			default:
				break;
		}
		dispatchEvents(&ev);
	}
	
	void OverlayScreen::paint(void)
	{
		gfx->drawFilledRect(0, 0, getW(), getH(), ColorTheme::backOverlayColor);
	}
}
