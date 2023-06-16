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
#ifndef __EVENTLISTENER_H
#define __EVENTLISTENER_H
#include "GraphicContext.h"
#include <SDL.h>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
#define WINDOWS_OR_MINGW 1
#endif

namespace GAGCore {
extern std::queue<SDL_Event> events;
class EventListener {
public:
	EventListener(GraphicContext* gfx);
	void run();
	void stop();
	bool isRunning();
	int poll(SDL_Event* e);
	static EventListener *instance();
	~EventListener();
	static std::mutex startMutex;
	static std::condition_variable startedCond;
	static std::mutex doneMutex;
	static std::condition_variable doneCond;
	static std::recursive_mutex renderMutex;
	void setPainter(std::function<void()> f);
	void addPainter(const std::string& name, std::function<void()> f);
	void removePainter(const std::string& name);
	static void ensureContext();
	void paint();
private:
	std::function<void()> painter;
	std::multimap<const std::string, std::function<void()> > painters;
	GraphicContext* gfx;
	static EventListener* el;
	std::atomic<bool> quit, done;
	std::atomic<int> depth;

	static std::mutex queueMutex; // used when pushing/popping queue.
};
}
#endif //__EVENTLISTENER_H