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
#include <cassert>

/*
  The main thread opens the window and handles events; the logic thread handles
  everything else. Every so often, the logic thread paints the screen. During
  resize, the event watch function will call paint() on every registered painter.

  The logic thread is started in Glob2::run.
*/
namespace GAGCore {
std::queue<SDL_Event> events = std::queue<SDL_Event>();
std::mutex EventListener::queueMutex;
EventListener* EventListener::el = nullptr;
std::mutex EventListener::startMutex;
std::condition_variable EventListener::startedCond;
std::mutex EventListener::doneMutex;
std::condition_variable EventListener::doneCond;
std::recursive_mutex EventListener::renderMutex;

#define SIZE_MOVE_TIMER_ID 1

// The depth variable is used to return early when indirect recursion happens
EventListener::EventListener(GraphicContext* gfx)
: painter(nullptr), depth(0)
{
	assert(gfx);
	this->gfx = gfx;
	el = this;
	done = false;
	quit = true;
}

//! End the event listening loop
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

//! Create an OpenGL context or set existing context as current on this thread.
void EventListener::ensureContext()
{
	instance()->gfx->createGLContext();
}

//! deprecated; use addPainter/removePainter instead.
void EventListener::setPainter(std::function<void()> f)
{
	std::unique_lock<std::recursive_mutex> lock(renderMutex);
	painter = f;
}

/** Add a painter to the list of painting functions to be called on window resize.
 *  name should be the class name that the function is called on
 *  f is the function to call to draw the screen.
 */
void EventListener::addPainter(const std::string& name, std::function<void()> f)
{
	std::unique_lock<std::recursive_mutex> lock(renderMutex);
	painters.insert(std::pair<const std::string, std::function<void()> >(name, f));
}

/** Erase the latest painter added with the name `name` from the multimap
 *  Removes the most recently added painter with that name.
 */
void EventListener::removePainter(const std::string& name)
{
	if (painters.empty())
		assert("Tried to remove a painter when painters map is empty.");
	std::unique_lock<std::recursive_mutex> lock(renderMutex);
	for (std::multimap<const std::string, std::function<void()> >::reverse_iterator it = painters.rbegin(); it != painters.rend(); ++it)
	{
		if (it->first == name)
		{
			// There might be multiple Screens active, so we remove the one added last.
			// For example, a Screen with an OverlayScreen above it.
			painters.erase(--(it.base()));
			break;
		}
	}
}

//! Draw all the registered painters in order
void EventListener::paint()
{
	depth++;
	if (depth > 1)
		return;
	if (painters.size())
	{
		std::unique_lock<std::recursive_mutex> lock(renderMutex);
		gfx->createGLContext();
		for (std::multimap<const std::string, std::function<void()> >::iterator it = painters.begin(); it != painters.end(); ++it)
		{
			it->second();
		}
		gfx->nextFrame();
	}
	depth--;
}

//! Handle user resizing the game window on Microsoft Windows.
// TODO: Handle window resizing on macOS
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

/** Listens for events and adds them to a queue.
 *  Call EventListener::poll to get an event from the queue.
 */
void EventListener::run()
{
	{
		std::unique_lock<std::mutex> lock(startMutex);
		quit = false;
	}
	startedCond.notify_one();
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
			{
				std::lock_guard<std::mutex> lock(queueMutex);
				events.push(event);
			}
		}
	}
	{
		std::unique_lock<std::mutex> lock(doneMutex);
		done = true;
	}
	doneCond.notify_one();
}

/** Drop-in replacement for SDL_PollEvent.
 *  Call this to get an event from the queue.
 */
int EventListener::poll(SDL_Event* e)
{
	std::lock_guard<std::mutex> lock(queueMutex);
	if (events.size()) {
		*e = events.front();
		events.pop();
		return 1;
	}
	return 0;
}

/** Gets the active EventListener instance.
 *  Does not initialize the EventListener if it is nullptr.
 */
EventListener *EventListener::instance()
{
	return el;
}
//! Checks if the EventListener is currently in the event handling loop.
bool EventListener::isRunning()
{
	return !quit;
}
}