// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GAGSys.h"
#include "GraphicContext.h"
#include <utility>

namespace GAGCore
{
	class GraphicContext;
	class DrawableSurface;
}

namespace GAGGUI
{
	//! transform an ucs 16 unicode char to an utf8 one
	void UCS16toUTF8(Uint16 ucs16, char utf8[4]);

	//! Interpolate from V0 on time 0 to V1 on time T for value x, so that f(0) = V0, f(T) = V1, f'(0) = 0, f'(T) = 0
	float splineInterpolation(float T, float V0, float V1, float x);

	//! A screen owns one frame of the application: it paints a surface, receives
	//! time and already-polled input from its host and ends with a return code.
	//! Menus and dialogs build on ui::UIScreen; this base only carries the
	//! execution lifecycle every screen a ScreenStack can run shares.
	class Screen
	{
	protected:
		//! true while execution is running, no need for serialisation
		bool run;
		bool executionActive;
		//! the return code, no need for serialisation
		Sint32 returnCode;
		//! the graphic context associated with this screen
		GAGCore::DrawableSurface *gfx;

	public:
		//! Whether the mouse wheel scrolls menus (the settings option that also
		//! governs in-game wheel input).
		static bool scrollWheelEnabled;

		//! Returned by execute() when the application itself is being quit
		//! (window close, Cmd+Q on macOS, Alt+F4 on Windows). Produced by the
		//! execute() event loop, not by endExecute(). Screens that run nested
		//! screens must propagate it to their own caller so every menu level
		//! unwinds and the process can exit.
		static constexpr int QUIT_APPLICATION = -1;

		Screen();
		virtual ~Screen();
		virtual const char *recordingId() const { return "screen"; }

		//! Method called for each timer's tick
		virtual void onTimer(Uint32 tick) { }
		//! Method called for each SDL_Event
		virtual void onSDLEvent(SDL_Event *event) { }
		//! Called once execution has begun, before the first frame.
		virtual void onScreenCreated() { }
		//! Called once, when a finished execution is collected.
		virtual void onScreenDestroyed() { }
		//! Full screen paint
		virtual void paint(void);

		//! Nonblocking lifecycle. The host supplies time and already-polled input.
		virtual void beginExecution(GAGCore::DrawableSurface *surface);
		virtual void updateExecution(Uint32 tick);
		virtual void suspendExecution() {}
		virtual void viewportResized(int oldWidth, int oldHeight, int width, int height) {}
		virtual void handleExecutionEvent(SDL_Event event);
		virtual bool usesResponsiveViewport() const { return false; }
		virtual bool supportsCompactViewport() const { return usesResponsiveViewport(); }
		virtual std::pair<int,int> minimumViewportSize() const { return {0,0}; }
		//! Called between frames before host interruption or a child transition.
		//! Discard held/queued input without synthesizing release actions.
		virtual void cancelExecutionInput() {}
		virtual void drawExecution();
		virtual Uint32 executionDelay(Uint32 now, Uint32 fallback) { return fallback; }
		bool isExecutionRunning() const { return run; }
		//! Complete once stopped; repeated calls do not repeat destruction callbacks.
		int finishExecution();

		//! Compatibility host loop for callers not yet migrated to a screen stack.
		//! Run the screen until someone call endExecute(returnCode). Return returnCode
		virtual int execute(GAGCore::DrawableSurface *gfx, int stepLength);
		//! Call this method to stop the execution of the screen
		void endExecute(int returnCode);
		//! Return the associated drawable surface
		GAGCore::DrawableSurface *getSurface(void) { return gfx; }
		//! Return the width of the screen
		int getW(void);
		//! Return the height of the screen
		int getH(void);
	};
}
