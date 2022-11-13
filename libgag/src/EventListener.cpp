#include "EventListener.h"
namespace GAGCore {
std::deque<SDL_Event> events = std::deque<SDL_Event>();
EventListener* EventListener::el = nullptr;
EventListener::EventListener(GraphicContext* gfx)
{
	this->gfx = gfx;
	el = this;
	done = false;
	quit = false;
}
EventListener::~EventListener()
{
	quit = true;
	while (!done) {
		SDL_Delay(100);
	}
}
void EventListener::run()
{
	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			events.push_back(event);
		}
	}
	done = true;
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