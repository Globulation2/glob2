// SPDX-License-Identifier: GPL-3.0-or-later
#include <GUIBase.h>
#include <ScreenStack.h>
#include <InputState.h>
#include <ApplicationHost.h>
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
    // A child request must return before construction/dispatch starts. The
    // parent stays alive through child completion and resumes on a later frame.
    struct Stacked : Screen {
        void onAction(Widget*, Action, int, int) override {}
        std::function<void()> input;
        int& destroyed;
        explicit Stacked(int& destroyed) : destroyed(destroyed) {}
        ~Stacked() override { ++destroyed; }
        void onSDLEvent(SDL_Event*) override { if (input) input(); }
    };
    int rootDestroyed = 0, childDestroyed = 0, callbacks = 0, rootInputs = 0;
    ScreenStack stack(surface);
    auto root = std::make_unique<Stacked>(rootDestroyed);
    auto* rootPtr = root.get();
    root->input = [&] {
        ++rootInputs;
        auto child = std::make_unique<Stacked>(childDestroyed);
        auto* childPtr = child.get();
        child->input = [childPtr] { childPtr->endExecute(17); };
        stack.push(std::move(child), [&](Screen&, int result) {
            require(result == 17 && rootDestroyed == 0 && childDestroyed == 0,
                    "Completion can read child results while both screens live");
            ++callbacks;
        });
    };
    stack.push(std::move(root));
    event = {}; event.type = SDL_USEREVENT;
    stack.frame(0, {event, event});
    require(rootInputs == 1 && callbacks == 0, "Opening input must not leak into a child");
    stack.frame(40, {event});
    require(callbacks == 0 && childDestroyed == 0, "Completion is deferred out of dispatch");
    stack.frame(80, {});
    require(callbacks == 1 && childDestroyed == 1 && rootDestroyed == 0,
            "Child is destroyed after completion and parent remains alive");
    rootPtr->endExecute(8);
    stack.frame(120, {});
    require(!stack.running() && stack.result() == 8 && rootDestroyed == 1,
            "Root completion empties the stack and retains its result");

    int abandonedParent = 0, abandonedChild = 0;
    ScreenStack abandoning(surface);
    auto parent = std::make_unique<Stacked>(abandonedParent);
    auto* parentPtr = parent.get();
    parent->input = [&] {
        bool recursiveRejected = false;
        try { abandoning.frame(1, {}); } catch (const std::logic_error&) { recursiveRejected = true; }
        require(recursiveRejected, "Callbacks cannot recursively drive the host");
        abandoning.push(std::make_unique<Stacked>(abandonedChild), [](Screen&, int) {
            throw std::runtime_error("Cancelled child continuation must not run");
        });
        parentPtr->endExecute(4);
    };
    abandoning.push(std::move(parent));
    abandoning.frame(0, {event});
    abandoning.frame(40, {});
    require(!abandoning.running() && abandonedParent == 1 && abandonedChild == 1,
            "Completing a parent cancels children queued by its final callback");

    int cancelled = 0;
    ScreenStack quitting(surface);
    quitting.push(std::make_unique<Stacked>(cancelled), [&](Screen&, int) {
        throw std::runtime_error("Quit must not run admission/continuation callbacks");
    });
    quitting.frame(0, {});
    event.type = SDL_QUIT;
    quitting.frame(40, {event});
    require(!quitting.running() && quitting.result() == Screen::QUIT_APPLICATION && cancelled == 1,
            "Quit releases owned screens without starting another flow");
    GAGCore::InputState held;
    event = {}; event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = SDL_SCANCODE_LEFT;
    event.key.keysym.mod = KMOD_CTRL;
    held.observe(event);
    require(held.keyboard()[SDL_SCANCODE_LEFT] && held.modifiers() == KMOD_CTRL,
            "Held keys and modifiers come from supplied events");
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    held.observe(event);
    require(!held.hasFocus() && !held.keyboard()[SDL_SCANCODE_LEFT] && held.modifiers() == KMOD_NONE,
            "Focus loss clears held input even without key-up delivery");
    event = {}; event.type = SDL_KEYDOWN; event.key.keysym.scancode = SDL_SCANCODE_LEFT;
    held.observe(event);
    require(!held.keyboard()[SDL_SCANCODE_LEFT], "Unfocused input cannot become held");
    event.type = SDL_WINDOWEVENT; event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    held.observe(event);
    require(held.hasFocus() && !held.keyboard()[SDL_SCANCODE_LEFT], "Focus return starts with released keys");
    struct HostProbe : GAGCore::ApplicationHost::Loop {
        int& frames; bool& destroyed;
        HostProbe(int& frames, bool& destroyed) : frames(frames), destroyed(destroyed) {}
        ~HostProbe() override { destroyed = true; }
        bool frame(std::uint32_t, const std::vector<SDL_Event>&) override { return ++frames < 3; }
        std::uint32_t delay(std::uint32_t) override { return 0; }
    };
    int hostFrames = 0;
    bool hostDestroyed = false, hostCompleted = false;
    GAGCore::ApplicationHost::run(std::make_unique<HostProbe>(hostFrames, hostDestroyed), [&] {
        require(hostDestroyed && hostFrames == 3, "Host releases application state before global cleanup");
        hostCompleted = true;
    });
    require(hostCompleted, "Native host completes exactly once before returning");
    std::cout << "PASS: screen phases, completion, reuse, quit and compatibility host\n";
}
