// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <emscripten.h>

namespace GAGCore::ApplicationHost
{
namespace
{
struct ScheduledLoop { std::unique_ptr<Loop> loop; std::function<void()> complete; };
void scheduledFrame(void* opaque)
{
    auto* state = static_cast<ScheduledLoop*>(opaque);
    std::vector<SDL_Event> events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) events.push_back(event);
    if (!state->loop->frame(SDL_GetTicks(), events)) {
        state->loop.reset();
        auto complete = std::move(state->complete);
        delete state;
        complete();
        return;
    }
    // Queue only after the frame returns. During the migration an Asyncify
    // suspension inside a legacy dialog must not start a second frame.
    emscripten_async_call(scheduledFrame, state, state->loop->delay(SDL_GetTicks()));
}
}
void run(std::unique_ptr<Loop> loop, std::function<void()> complete)
{
    auto* state = new ScheduledLoop{std::move(loop), std::move(complete)};
    emscripten_async_call(scheduledFrame, state, 0);
}

void wait(std::uint32_t milliseconds)
{
    emscripten_sleep(milliseconds ? milliseconds : 1);
}
void screenChanged(const char* name)
{
    EM_ASM({ Module['glob2Screen'] = UTF8ToString($0); }, name);
}
void simulationAdvanced(std::uint32_t tick)
{
    EM_ASM({ Module['glob2Tick'] = $0; Module['glob2Screen'] = 'match'; }, tick);
}
void exited(int result)
{
    EM_ASM({
        Module['glob2Screen'] = 'exited';
        if (Module['onGameExit']) Module['onGameExit']($0);
    }, result);
}
void matchFrame(bool paused)
{
    EM_ASM({
        Module['glob2Frames'] = (Module['glob2Frames'] || 0) + 1;
        Module['glob2Paused'] = Boolean($0);
    }, paused);
}
}
