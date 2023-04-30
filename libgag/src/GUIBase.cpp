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

#include <GUIBase.h>
#include <GUIStyle.h>
#include <assert.h>
#include <GraphicContext.h>
#include <SDLCompat.h>
#include <cmath>

#include <Toolkit.h>

#define SCREEN_ANIMATION_FRAME_COUNT 10

using namespace GAGCore;

namespace GAGGUI
{
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
	
	unsigned getNextUTF8Char(const std::string text, unsigned pos)
	{
		unsigned next=pos+getNextUTF8Char(text[pos]);
		assert(next<=text.length());
		return next;
	}
	
	unsigned getPrevUTF8Char(const std::string text, unsigned pos)
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
		this->tooltipFontPtr = NULL;
		visible=true;
		parent=NULL;
	}
	
	Widget::Widget(const std::string& tooltip, const std::string &tooltipFont)
	{
		this->tooltipFontPtr = NULL;
		this->tooltip = tooltip;
		this->tooltipFont = tooltipFont;
		lastIdleTick = 0;
		my = -1;
		mx = -1;
		visible = true;
		parent = NULL;
	}
	
	Widget::~Widget()
	{
		
	}

	void Widget::init(void)
	{
		if(tooltipFont.length() != 0 && tooltip.length() != 0) {
			this->tooltipFontPtr = Toolkit::getFont(tooltipFont.c_str());
		}
		internalInit();
	}

	void Widget::timerTick(Uint32 tick)
	{
		currentTick = tick;
		onTimer(tick);
	}

	void Widget::SDLMouseMotion(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEMOTION);
		// Mouse has moved
		lastIdleTick = currentTick;
		mx = event->motion.x;
		my = event->motion.y;
		onSDLMouseMotion(event);
	}
	
	void Widget::displayTooltip()
	{
		// We have a tooltip and the mouse is idle on our widget for some ticks (1 SDL tick = 1ms)
		if(tooltipFontPtr != NULL && !tooltip.empty() && (currentTick - lastIdleTick) > 1000 && isOnWidget(mx, my))
		{
			DrawableSurface *gfx = parent->getSurface();
			assert(gfx);
			
			int width = tooltipFontPtr->getStringWidth(tooltip.c_str());
			int height = tooltipFontPtr->getStringHeight(tooltip.c_str());
			int x = mx - width - 10;
			if(x < 0) x = 0;
			int y = my - height - 10;
			if(y < 0) y = 0;
			
			gfx->drawFilledRect(x + 1, y + 1, width + 1, height, 22, 0, 0, 128);
			gfx->drawRect(x,y, width + 2, height + 1, 255, 255, 255);
			gfx->drawString(x, y, tooltipFontPtr, tooltip);

		}
	}
	RectangularWidget::RectangularWidget()
	{
		x=y=w=h=hAlignFlag=vAlignFlag=0;
	}

	RectangularWidget::RectangularWidget(const std::string& tooltip, const std::string &tooltipFont)
		: Widget(tooltip, tooltipFont)
	{
		x=y=w=h=hAlignFlag=vAlignFlag=0;
	}

	bool RectangularWidget::isOnWidget(int _x, int _y)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		return (isPtInRect(_x, _y, x, y, w, h));
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
	
		int screenW = parent->getSurface()->getW();
		int screenH = parent->getSurface()->getH();
		
		switch (hAlignFlag)
		{
			case ALIGN_LEFT:
				*sx=x;
				*sw=w;
				break;
	
			case ALIGN_RIGHT:
				*sx=screenW-w-x;
				*sw=w;
				break;
	
			case ALIGN_FILL:
				*sx=x;
				*sw=screenW-w-x;
				break;
				
			case ALIGN_SCREEN_CENTERED:
				*sx=x+((screenW-640)>>1);
				*sw=w;
				break;
				
			case ALIGN_CENTERED:
				*sx = (screenW - w) >> 1;
				*sw = w;
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
				*sy=screenH-h-y;
				*sh=h;
				break;
	
			case ALIGN_FILL:
				*sy=y;
				*sh=screenH-h-y;
				break;
				
			case ALIGN_SCREEN_CENTERED:
				*sy=y+((screenH-480)>>1);
				*sh=h;
				break;
				
			case ALIGN_CENTERED:
				*sy = (screenH - h) >> 1;
				*sh = h;
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

	HighlightableWidget::HighlightableWidget(const std::string& tooltip, const std::string &tooltipFont, Sint32 returnCode)
		: RectangularWidget(tooltip, tooltipFont), totalAnimationTime(10)
	{
		assert(totalAnimationTime > 0);
		highlighted = false;
		prevHighlightValue = 0;
		nextHighlightValue = 0;
		actAnimationTime = 0;
		this->returnCode = returnCode;
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

	HighlightableWidget::HighlightableWidget(const std::string& tooltip, const std::string & tooltipFont)
		: RectangularWidget(tooltip, tooltipFont), totalAnimationTime(10)
	{
		assert(totalAnimationTime > 0);
		highlighted = false;
		prevHighlightValue = 0;
		nextHighlightValue = 0;
		actAnimationTime = 0;
		this->returnCode = 0;
	}
	
	void HighlightableWidget::onSDLMouseMotion(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEMOTION);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
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
	
	unsigned HighlightableWidget::getNextHighlightValue(void)
	{
		float actHighlight;
		if (actAnimationTime > 0)
		{
			// as actAnimationTime is decreasing over time, we have to invert prev and next highlight values
			actHighlight = splineInterpolation(totalAnimationTime,  nextHighlightValue, prevHighlightValue, actAnimationTime);
			actAnimationTime--;
		}
		else
			actHighlight = nextHighlightValue;
			
		return static_cast<unsigned>(255.0 * actHighlight);
	}
	
	void HighlightableWidget::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		Style::style->drawFrame(parent->getSurface(), x, y, w, h, getNextHighlightValue());
	}
	
	bool Screen::scrollWheelEnabled = true;
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
		Uint64 frameStartTime;
		Sint64 frameWaitTime;
		
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
			frameStartTime=SDL_GetTicks64();
			
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
					case SDL_WINDOWEVENT:
					{
						windowEvent=event;
						wasWindowEvent=true;
					}
					break;
					case SDL_WINDOWEVENT_RESIZED:
					{
						// FIXME: window resize is broken
						// gfx->setRes(event.window.data1, event.window.data2);
					}
					break;
					case SDL_WINDOWEVENT_SIZE_CHANGED:
					{
						onAction(NULL, SCREEN_RESIZED, gfx->getW(), gfx->getH());
					}
					break;
					case SDL_KEYDOWN:
					{
						//Manual integration of cmd+q and alt f4
#						ifdef USE_OSX
						if(event.key.keysym.sym == SDLK_q && SDL_GetModState() & KMOD_GUI)
						{
							run=false;
							returnCode=-1;
							break;
						}
#						endif
#						ifdef USE_WIN32
						if(event.key.keysym.sym == SDLK_F4 && SDL_GetModState() & KMOD_ALT)
						{
							run=false;
							returnCode=-1;
							break;
						}
#						endif
						dispatchEvents(&event);
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
			frameWaitTime=static_cast<Sint64>(SDL_GetTicks64())-static_cast<Sint64>(frameStartTime);
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
		// We put the switch here in order to avoid
		// a switch in each specific onSDLEvent method
		// we never receive neither SDL_QUIT nor
		// SDL_VIDEORESIZE (not dispatched)
		// For the moment, we do not take the following event in account :
		// SDL_SYSWMEVENT, SDL_JOY*****, 
		switch(event->type)
		{
			case SDL_WINDOWEVENT:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLActive(event);
				}
				break;
			case SDL_KEYDOWN:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLKeyDown(event);
				}
				break;
			case SDL_KEYUP:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLKeyUp(event);
				}
				break;
			case SDL_MOUSEMOTION:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->SDLMouseMotion(event);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLMouseButtonUp(event);
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLMouseButtonDown(event);
				}
				break;
			case SDL_WINDOWEVENT_EXPOSED:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLVideoExpose(event);
				}
				break;
			case SDL_TEXTINPUT:
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLTextInput(event);
				}
				break;
			case SDL_MOUSEWHEEL:
				if (!scrollWheelEnabled)
					break;
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLMouseWheel(event);
				}
				break;

			default:
				// Every other event is passed to onSDLEvent
				for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
				{
					if ((*it)->visible)
						(*it)->onSDLOtherEvent(event);
				}
				break;

		}
	}
	
	void Screen::dispatchTimer(Uint32 tick)
	{
		onTimer(tick);
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			if ((*it)->visible)
				(*it)->timerTick(tick);
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
		/* We need a second loop in order to have tooltip over everything else */
		for (std::set<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); ++it)
		{
			if ((*it)->visible)
				(*it)->displayTooltip();
		}
		gfx->nextFrame();
		
		if (animationFrame < SCREEN_ANIMATION_FRAME_COUNT)
			animationFrame++;
	}
	
	void Screen::paint(void)
	{
		gfx->drawFilledRect(0, 0, getW(), getH(), Style::style->backColor);
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
		int newX, newY;
		SDL_Event ev=*event;
		switch (ev.type)
		{
			case SDL_MOUSEMOTION:
				newX = (int)ev.motion.x - decX;
				newY = (int)ev.motion.y - decY;
				ev.motion.x = (unsigned)std::max(0, newX);
				ev.motion.y = (unsigned)std::max(0, newY);
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				newX = (int)ev.button.x - decX;
				newY = (int)ev.button.y - decY;
				ev.button.x = (unsigned)std::max(0, newX);
				ev.button.y = (unsigned)std::max(0, newY);
				break;
			default:
				break;
		}
		dispatchEvents(&ev);
	}
	
	void OverlayScreen::paint(void)
	{
		gfx->drawFilledRect(0, 0, getW(), getH(), Style::style->backOverlayColor);
		Style::style->drawFrame(gfx, 0, 0, getW(), getH(), Color::ALPHA_TRANSPARENT);
	}
}
