#include "EventListener.h"
namespace GAGCore {
std::deque<SDL_Event> events = std::deque<SDL_Event>();
EventListener::EventListener(GraphicContext* gfx)
{
	this->gfx = gfx;
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
bool EventListener::isRunning()
{
	return !quit;
}
}