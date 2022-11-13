#ifndef __EVENTLISTENER_H
#define __EVENTLISTENER_H
#include "GraphicContext.h"
#include <SDL.h>
#include <deque>
namespace GAGCore {
extern std::deque<SDL_Event> events;
class EventListener {
public:
	EventListener(GraphicContext* gfx);
	void run();
	bool isRunning();
	~EventListener();
private:
	GraphicContext* gfx;
	bool quit, done;
};
}
#endif //__EVENTLISTENER_H