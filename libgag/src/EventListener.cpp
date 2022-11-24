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
	std::unique_lock lock(doneMutex);
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
		std::unique_lock lock(startMutex);
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
		std::unique_lock lock(doneMutex);
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