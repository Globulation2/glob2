// SPDX-License-Identifier: GPL-3.0-or-later
#include <GUIBase.h>
#include <ScreenStack.h>
#include <InputState.h>
#include <ApplicationHost.h>
#include <CooperativeSlice.h>
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

struct TaskLifetime { int& live; TaskLifetime(int& live) : live(live) { ++live; } ~TaskLifetime() { --live; } };
GAGCore::CooperativeTask childTask(int& live, bool fail)
{
    TaskLifetime lifetime(live);
    co_await GAGCore::CooperativeTask::checkpoint("child");
    if (fail) throw std::runtime_error("child failure");
    co_return true;
}
GAGCore::CooperativeTask parentTask(int& live, bool fail)
{
    TaskLifetime lifetime(live);
    const bool result = co_await childTask(live, fail);
    co_await GAGCore::CooperativeTask::checkpoint("parent");
    co_return result;
}

GAGCore::CooperativeTask timedWork(GAGCore::CooperativeSlice::Time& now, int& steps,
                                  std::chrono::milliseconds cost, int count)
{
    for (int i = 0; i < count; ++i) {
        ++steps; now += cost;
        co_await GAGCore::CooperativeTask::checkpoint("work");
    }
    co_return true;
}
int main()
{
    {
        using Slice = GAGCore::CooperativeSlice;
        Slice::Time now{};
        int steps = 0;
        Slice slice([&] { return now; });
        auto task = timedWork(now, steps, std::chrono::milliseconds(1), 10);
        require(!slice.advance(task) && steps == 4, "Slice stops at its elapsed-time budget");
        require(!slice.advance(task) && steps == 8, "Each callback starts a fresh time budget");
        require(slice.advance(task) && task.result() && steps == 10, "Slice reports completion without an extra callback");
        steps = 0;
        auto slow = timedWork(now, steps, std::chrono::milliseconds(10), 3);
        require(!slice.advance(slow) && steps == 1, "An expensive checkpoint stops the slice immediately afterward");
        steps = 0;
        auto cheap = timedWork(now, steps, std::chrono::milliseconds(0), 100);
        require(!slice.advance(cheap) && steps == 64, "Checkpoint cap bounds zero-cost or low-resolution clocks");
        require(slice.advance(cheap) && steps == 100, "Capped work resumes without skipping steps");
        bool rejected = false;
        try { Slice invalid([&] { return now; }, std::chrono::milliseconds(0)); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid time budgets are rejected");
    }

    {
        GAGCore::InputState held;
        SDL_Event key{}; key.type = SDL_KEYDOWN; key.key.keysym.scancode = SDL_SCANCODE_LEFT;
        key.key.keysym.mod = KMOD_CTRL;
        held.observe(key); held.clearHeld();
        require(!held.keyboard()[SDL_SCANCODE_LEFT] && held.modifiers() == KMOD_NONE && held.hasFocus(),
                "Suspending input clears controls without losing window focus");
    }

    int live = 0;
    {
        auto task = parentTask(live, false);
        require(live == 0 && !task.advance() && live == 2, "Nested jobs start lazily and stop at child checkpoints");
        require(std::string(task.stage()) == "child", "Child progress is visible to the root");
    }
    require(live == 0, "Cancelling root releases suspended children");
    {
        auto task = parentTask(live, false);
        task.advance();
        require(!task.advance() && live == 1, "Child completion resumes its parent to the next checkpoint");
        require(task.advance() && task.result() && live == 0, "Root completes with the child result");
    }
    {
        auto task = parentTask(live, true);
        task.advance();
        require(task.advance(), "Child exception completes the root");
        bool rejected = false;
        try { task.result(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && live == 0, "Child exceptions propagate and release resources");
    }
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    GAGCore::GraphicContext context(800, 600, 0, "Screen lifecycle regression");
    GAGCore::DrawableSurface surface(800, 600);
    {
        struct LayoutProbe : Screen {
            int geometry = 0;
            void onAction(Widget*, Action, int, int) override {}
            void updateLayout() override { geometry = getW(); }
            void onSDLEvent(SDL_Event*) override {
                require(geometry == getW(), "Input uses refreshed layout before paint");
            }
            void paint() override {
                require(geometry == getW(), "Paint uses refreshed layout");
            }
        } probe;
        probe.beginExecution(&surface);
        SDL_Event tap{}; tap.type = SDL_MOUSEBUTTONDOWN;
        probe.dispatchEvents(&tap);
        probe.geometry = 0;
        probe.dispatchPaint();
        probe.endExecute(0);
        probe.finishExecution();

        struct Dialog : OverlayScreen {
            int inputX = -1, inputY = -1;
            Dialog(GAGCore::GraphicContext* parent, unsigned w, unsigned h)
                : OverlayScreen(parent, w, h) {}
            void onAction(Widget*, Action, int, int) override {}
            void onSDLEvent(SDL_Event* event) override {
                if (event->type == SDL_MOUSEBUTTONDOWN) {
                    inputX = event->button.x; inputY = event->button.y;
                }
            }
            void paint() override {}
        } dialog(&context, 240, 160);
        dialog.decX = 900; dialog.decY = 700;
        tap.button.x = 570; tap.button.y = 450;
        dialog.translateAndProcessEvent(&tap);
        require(dialog.decX == 560 && dialog.decY == 440 &&
                dialog.inputX == 10 && dialog.inputY == 10,
                "Embedded dialog clamps before translating the first input");
        dialog.decX = 900; dialog.decY = 700;
        dialog.dispatchPaint();
        require(dialog.decX == 560 && dialog.decY == 440,
                "Embedded dialog paint uses the same bounds as input");
        for (auto [w, h] : {std::pair{320,568}, {568,320}, {360,640}, {640,360}, {768,1024}}) {
            dialog.viewportResized(800, 600, w, h);
            require(dialog.decX == (w-240)/2 && dialog.decY == (h-160)/2,
                    "Host resize retains centered dialogs in phone and tablet orientations");
        }
        Dialog oversized(&context, 900, 700);
        require(oversized.decX == 0 && oversized.decY == 0,
                "Oversized dialog construction cannot underflow unsigned coordinates");
        oversized.viewportResized(800, 600, 320, 568);
        require(oversized.decX == 0 && oversized.decY == 0,
                "Oversized dialog keeps its origin reachable after rotation");
    }

    {
        struct LifecycleScreen : Screen {
            std::vector<Uint32> ticks;
            int draws=0, actions=0, cancellations=0;
            bool held=false;
            void onAction(Widget*,Action,int,int) override {}
            void updateExecution(Uint32 tick) override { ticks.push_back(tick); if(held) ++actions; }
            void drawExecution() override { ++draws; }
            void handleExecutionEvent(SDL_Event event) override { if(event.type==SDL_KEYDOWN) held=true; }
            void cancelExecutionInput() override { held=false; ++cancellations; }
            Uint32 executionDelay(Uint32 now,Uint32) override { return now-ticks.back(); }
        };
        ScreenStack lifecycle(surface);
        auto owned=std::make_unique<LifecycleScreen>();auto* probe=owned.get();
        lifecycle.push(std::move(owned));
        SDL_Event key{};key.type=SDL_KEYDOWN;
        SDL_Event background{};background.type=SDL_APP_WILLENTERBACKGROUND;
        SDL_Event foreground{};foreground.type=SDL_APP_DIDENTERFOREGROUND;
        lifecycle.frame(1000,{key});
        lifecycle.frame(1040,{background});
        lifecycle.frame(90000,{key});
        require(probe->ticks.size()==1 && probe->draws==1 && probe->actions==0 && !probe->held,
                "Backgrounding clears queued input before simulation and suppresses updates/presentation");
        require(lifecycle.delay(90020,0)==100,"Background host does not busy-spin");
        lifecycle.frame(120000,{key,foreground,key});
        require(probe->ticks.back()==1000 && !probe->held && probe->actions==0,
                "Resume excludes elapsed time and ignores input from the interrupted batch");
        require(lifecycle.delay(120010,0)==10,"Pacing uses the same resumed clock as updates");
        lifecycle.frame(120040,{});
        require(probe->ticks.back()==1040,"Normal frame timing resumes without accumulated background lag");
        lifecycle.frame(300000,{background,foreground});
        require(probe->ticks.back()==1040,"Background and foreground in one batch also exclude the gap");
        lifecycle.frame(300040,{key});
        SDL_Event resize{};resize.type=SDL_WINDOWEVENT;resize.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
        lifecycle.frame(300080,{resize,key});
        require(!probe->held && probe->actions==0,"Rotation clears queued controls before the next update");
        lifecycle.frame(300120,{key});
        lifecycle.push(std::make_unique<LifecycleScreen>());
        lifecycle.frame(300160,{});
        require(!probe->held,"Opening a child clears parent held input");
        lifecycle.frame(300200,{background});
        SDL_Event quit{};quit.type=SDL_QUIT;
        lifecycle.frame(300240,{quit});
        require(!lifecycle.running(),"Quit is processed while backgrounded");

        ScreenStack hiddenStartup(surface);
        auto initial=std::make_unique<LifecycleScreen>();auto* initialProbe=initial.get();
        hiddenStartup.push(std::move(initial));
        hiddenStartup.frame(400000,{background});
        require(!initialProbe->isExecutionRunning(),"Background startup defers screen admission");
        hiddenStartup.frame(500000,{foreground});
        require(initialProbe->ticks.back()==400000,"Deferred startup uses the suspended host clock");
    }
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
    struct BorrowingScreen : Screen {
        std::weak_ptr<int> resource;
        bool& aliveDuringDestruction;
        bool complete;
        BorrowingScreen(std::weak_ptr<int> resource, bool& alive, bool complete)
            : resource(resource), aliveDuringDestruction(alive), complete(complete) {}
        ~BorrowingScreen() override { aliveDuringDestruction = !resource.expired(); }
        void onAction(Widget*, Action, int, int) override {}
        void onTimer(Uint32) override { if (complete) endExecute(0); }
    };
    for (int mode = 0; mode < 3; ++mode) {
        bool aliveDuringDestruction = false;
        auto resource = std::make_shared<int>(7);
        std::weak_ptr<int> released = resource;
        ScreenStack lifetime(surface);
        lifetime.push(std::make_unique<BorrowingScreen>(resource, aliveDuringDestruction, mode == 0),
                      [resource](Screen&, int) {});
        resource.reset();
        if (mode != 2) lifetime.frame(0, {});
        if (mode != 0) lifetime.stop();
        lifetime.frame(40, {});
        require(aliveDuringDestruction && released.expired(),
                "Continuation-owned resources outlive screens on completion and cancellation");
    }

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
