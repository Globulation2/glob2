#include "GraphicContext.h"
#include <SDL.h>
#include <deque>
namespace GAGCore {
extern std::deque<SDL_Event> events;
class EventListener {
public:
	EventListener(GraphicContext* gfx);
	void run();
	~EventListener();
private:
	GraphicContext* gfx;
	bool quit, done;
};
}