// SPDX-License-Identifier: GPL-3.0-or-later
#include <GUIBase.h>
#include <SDLGraphicContext.h>
#include <stdexcept>
#include <iostream>

using namespace GAGGUI;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Probe : Screen
{
    int created = 0, destroyed = 0, paints = 0, inputs = 0, timers = 0;
    Uint32 lastTick = 0;
    bool closeOnCreate = false, closeOnTimer = false;
    void onAction(Widget*, Action action, int, int) override
    {
        if (action == SCREEN_CREATED) {
            ++created;
            if (closeOnCreate) endExecute(7);
        }
        if (action == SCREEN_DESTROYED) ++destroyed;
    }
    void paint() override { ++paints; }
    void onTimer(Uint32 tick) override
    {
        ++timers;
        lastTick = tick;
        if (closeOnTimer) endExecute(9);
    }
    void onSDLEvent(SDL_Event*) override { ++inputs; endExecute(42); }
};

int main()
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    GAGCore::GraphicContext context(800, 600, 0, "Screen lifecycle regression");
    GAGCore::DrawableSurface surface(800, 600);
    Probe screen;
    screen.beginExecution(&surface);
    require(screen.created == 1 && screen.paints == 0, "Begin must not draw");
    screen.updateExecution(1234);
    require(screen.lastTick == 1234 && screen.timers == 1, "Host owns the timer");
    screen.drawExecution();
    require(screen.paints == 1, "Drawing is an explicit phase");
    bool rejected = false;
    try { screen.beginExecution(&surface); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "A screen cannot execute twice concurrently");
    rejected = false;
    try { screen.finishExecution(); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "A running screen cannot finish");
    SDL_Event event{};
    event.type = SDL_USEREVENT;
    screen.handleExecutionEvent(event);
    screen.handleExecutionEvent(event);
    screen.updateExecution(5678);
    screen.drawExecution();
    require(screen.inputs == 1 && screen.timers == 1 && screen.paints == 1,
            "Completed screens must not consume another frame or event");
    require(screen.finishExecution() == 42 && screen.finishExecution() == 42,
            "Completion result must be stable");
    require(screen.destroyed == 1, "Destroy callback must run exactly once");

    screen.beginExecution(&surface);
    event.type = SDL_QUIT;
    screen.handleExecutionEvent(event);
    require(screen.finishExecution() == Screen::QUIT_APPLICATION, "Quit must propagate");
    require(screen.created == 2 && screen.destroyed == 2 && screen.inputs == 1,
            "A completed screen can be reused and quit bypasses widget input");

#if defined(USE_OSX) || defined(USE_WIN32)
    screen.beginExecution(&surface);
    event = {};
    event.type = SDL_KEYDOWN;
#ifdef USE_OSX
    event.key.keysym.sym = SDLK_q;
    event.key.keysym.mod = KMOD_GUI;
#else
    event.key.keysym.sym = SDLK_F4;
    event.key.keysym.mod = KMOD_ALT;
#endif
    screen.handleExecutionEvent(event);
    require(screen.finishExecution() == Screen::QUIT_APPLICATION,
            "Quit chords must use the supplied event's modifiers");
#endif

    Probe immediate;
    immediate.closeOnCreate = true;
    require(immediate.execute(&surface, 40) == 7, "Creation callback may complete the screen");
    require(immediate.paints == 0 && immediate.destroyed == 1,
            "Creation-time completion must not be overwritten by the wrapper");
    Probe timer;
    timer.closeOnTimer = true;
    require(timer.execute(&surface, 40) == 9, "Legacy host must drive the same lifecycle");
    require(timer.timers == 1 && timer.paints == 1 && timer.destroyed == 1,
            "Timer completion must stop before another draw");
    std::cout << "PASS: screen phases, completion, reuse, quit and compatibility host\n";
}
