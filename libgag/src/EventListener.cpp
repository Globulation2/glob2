/*
  Copyright (C) 2022 Nathan Mills
  for any question or comment contact us at <the dot true dot nathan dot mills at gmail dot com>

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
#include "EventListener.h"
#include <SDL_syswm.h>
namespace GAGCore {
std::deque<SDL_Event> events = std::deque<SDL_Event>();
EventListener* EventListener::el = nullptr;
std::mutex EventListener::startMutex;
std::condition_variable EventListener::startedCond;
std::mutex EventListener::doneMutex;
std::condition_variable EventListener::doneCond;

#define SIZE_MOVE_TIMER_ID 1
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
#define WINDOWS_OR_MINGW 1
#endif

EventListener::EventListener(GraphicContext* gfx)
: painter(nullptr)
{
	this->gfx = gfx;
	el = this;
	done = false;
	quit = true;
}
void EventListener::stop()
{
	quit = true;
	std::unique_lock<std::mutex> lock(doneMutex);
	while (!done) {
		doneCond.wait(lock);
	}
}
EventListener::~EventListener()
{
}
void EventListener::setPainter(std::function<void()> f)
{
	painter = f;
}
void EventListener::paint()
{
	if (painter)
	{
		painter();
		gfx->nextFrame();
	}
}
//https://stackoverflow.com/a/51597338/8890345
#ifdef WINDOWS_OR_MINGW
bool sizeMoveTimerRunning = false;
int eventWatch(void* self, SDL_Event* event) {
	if (event->type == SDL_SYSWMEVENT)
	{
		const auto &winMessage = event->syswm.msg->msg.win;
		if (winMessage.msg == WM_ENTERSIZEMOVE)
		{
			// the user started dragging, so create the timer (with the minimum timeout)
			// if you have vsync enabled, then this shouldn't render unnecessarily
			sizeMoveTimerRunning = SetTimer(GetActiveWindow(), SIZE_MOVE_TIMER_ID, USER_TIMER_MINIMUM, nullptr);
		}
		else if (winMessage.msg == WM_TIMER)
		{
			if (winMessage.wParam == SIZE_MOVE_TIMER_ID)
			{
				// call your render function
				EventListener *el = reinterpret_cast<EventListener*>(self);
				if (el)
					el->paint();
			}
		}
	}
	return 0;
}
#endif
void EventListener::run()
{
	{
		std::unique_lock<std::mutex> lock(startMutex);
		quit = false;
		startedCond.notify_one();
	}
#ifdef WINDOWS_OR_MINGW
	SDL_AddEventWatch(eventWatch, this); // register the event watch function
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE); // we need the native Windows events, so we can listen to WM_ENTERSIZEMOVE and WM_TIMER
#endif
	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT) {
				quit = true;
			}
			if (event.type == SDL_WINDOWEVENT &&
				(event.window.event == SDL_WINDOWEVENT_RESIZED ||
				 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
			{
				/*glClearColor (0.0f, 0.0f, 0.0f, 1.0f );
				glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				SDL_GL_SwapWindow(_gc->window);*/
				/*if (painter)
				{
					painter();
					gfx->nextFrame();
				}*/
			}
#ifdef WINDOWS_OR_MINGW
			if (sizeMoveTimerRunning)
			{
				// modal drag/size loop ended, so kill the timer
				KillTimer(GetActiveWindow(), SIZE_MOVE_TIMER_ID);
				sizeMoveTimerRunning = false;
			}
#endif
			events.push_back(event);
		}
	}
	{
		std::unique_lock<std::mutex> lock(doneMutex);
		done = true;
		doneCond.notify_one();
	}
}
int EventListener::poll(SDL_Event* e)
{
	if (events.size()) {
		*e = events.front();
		events.pop_front();
		return 1;
	}
	return 0;
}
EventListener *EventListener::instance()
{
	return el;
}
bool EventListener::isRunning()
{
	return !quit;
}
}