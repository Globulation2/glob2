// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <ApplicationHost.h>
#include <typeinfo>
#include <stdexcept>
#include <GUIBase.h>
#include <GUIStyle.h>
#include <assert.h>
#include <GraphicContext.h>
#include <SDLCompat.h>

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

	float splineInterpolation(float T, float V0, float V1, float x)
	{
		assert(T > 0);
		float a = (-2 * (V1 - V0)) / (T * T * T);
		float b = (3 * (V1 - V0)) / (T * T);
		float c = 0;
		float d = V0;
		return a * (x * x * x) + b * (x * x) + c * x + d;
	}

	bool Screen::scrollWheelEnabled = true;

	Screen::Screen()
	{
		gfx = NULL;
		returnCode = 0;
		run = false;
		executionActive = false;
	}

	Screen::~Screen()
	{
	}

	void Screen::beginExecution(DrawableSurface *surface)
	{
		if (executionActive) throw std::logic_error("Screen execution is already active");
		if (!surface) throw std::invalid_argument("Screen execution requires a surface");
		gfx = surface;
		returnCode = 0;
		run = true;
		executionActive = true;
		ApplicationHost::screenChanged(typeid(*this).name());
		onScreenCreated();
	}

	void Screen::updateExecution(Uint32 tick)
	{
		if (run) onTimer(tick);
	}

	void Screen::handleExecutionEvent(SDL_Event event)
	{
		if (!run) return;
		GraphicContext::translateMouseEvent(&event);
		if (event.type == SDL_QUIT)
		{
			endExecute(QUIT_APPLICATION);
			return;
		}
		if (event.type == SDL_KEYDOWN)
		{
#ifdef USE_OSX
			if (event.key.keysym.sym == SDLK_q && (event.key.keysym.mod & KMOD_GUI))
			{
				endExecute(QUIT_APPLICATION);
				return;
			}
#endif
#ifdef USE_WIN32
			if (event.key.keysym.sym == SDLK_F4 && (event.key.keysym.mod & KMOD_ALT))
			{
				endExecute(QUIT_APPLICATION);
				return;
			}
#endif
		}
		if (event.type == SDL_MOUSEWHEEL && !scrollWheelEnabled) return;
		onSDLEvent(&event);
	}

	void Screen::drawExecution()
	{
		if (!run) return;
		assert(gfx);
		Style::style->onFrame();
		gfx->setClipRect();
		paint();
		gfx->nextFrame();
	}

	int Screen::finishExecution()
	{
		if (run) throw std::logic_error("Cannot finish a running screen");
		const int result = returnCode;
		if (executionActive)
		{
			executionActive = false;
			onScreenDestroyed();
		}
		return result;
	}

	int Screen::execute(DrawableSurface *surface, int stepLength)
	{
		beginExecution(surface);
		drawExecution();
		while (isExecutionRunning())
		{
			const Uint64 frameStart = SDL_GetTicks64();
			updateExecution(static_cast<Uint32>(frameStart));
			SDL_Event lastMouseMotion{}, windowEvent{}, event{};
			bool hadLastMouseMotion = false;
			bool hadWindowEvent = false;
			while (isExecutionRunning() && GraphicContext::pollEvent(&event))
			{
				if (event.type == SDL_MOUSEMOTION)
				{
					lastMouseMotion = event;
					hadLastMouseMotion = true;
				}
				else if (event.type == SDL_WINDOWEVENT)
				{
					windowEvent = event;
					hadWindowEvent = true;
				}
				else handleExecutionEvent(event);
			}
			if (hadLastMouseMotion) handleExecutionEvent(lastMouseMotion);
			if (hadWindowEvent) handleExecutionEvent(windowEvent);
			drawExecution();
			if (isExecutionRunning())
			{
				const Sint64 elapsed = static_cast<Sint64>(SDL_GetTicks64() - frameStart);
				ApplicationHost::wait(std::max<Sint64>(stepLength - elapsed, 0));
			}
		}
		return finishExecution();
	}

	void Screen::endExecute(int returnCode)
	{
		run=false;
		this->returnCode=returnCode;
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
}
