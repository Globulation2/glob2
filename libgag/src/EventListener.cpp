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
namespace GAGCore {
std::deque<SDL_Event> events = std::deque<SDL_Event>();
EventListener* EventListener::el = nullptr;
std::mutex EventListener::startMutex;
std::condition_variable EventListener::startedCond;
std::mutex EventListener::doneMutex;
std::condition_variable EventListener::doneCond;
EventListener::EventListener(GraphicContext* gfx)
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
void EventListener::run()
{
	{
		std::unique_lock<std::mutex> lock(startMutex);
		quit = false;
		startedCond.notify_one();
	}
	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT) {
				quit = true;
			}
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