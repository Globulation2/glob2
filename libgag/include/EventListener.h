#ifndef __EVENTLISTENER_H
#define __EVENTLISTENER_H
#include "GraphicContext.h"
#include <SDL.h>
#include <deque>
#include <atomic>
namespace GAGCore {
extern std::deque<SDL_Event> events;
class EventListener {
public:
	EventListener(GraphicContext* gfx);
	void run();
	void stop();
	bool isRunning();
	int poll(SDL_Event* e);
	static EventListener *instance();
	~EventListener();
private:
	GraphicContext* gfx;
	static EventListener* el;
	std::atomic<bool> quit, done;
};
}
#endif //__EVENTLISTENER_H