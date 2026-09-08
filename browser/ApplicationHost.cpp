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
bool takeVisibilityChange(bool& hidden)
{
    const int state = EM_ASM_INT({
        if (!Module.visibilityPending) return -1;
        Module.visibilityPending = false;
        return document.hidden ? 1 : 0;
    });
    if (state < 0) return false;
    hidden = state != 0;
    return true;
}
bool takeViewportSize(int& width, int& height)
{
    return EM_ASM_INT({
        const size = Module.pendingViewport;
        Module.pendingViewport = null;
        if (!size || size.width <= 0 || size.height <= 0) return 0;
        HEAP32[$0 >> 2] = size.width;
        HEAP32[$1 >> 2] = size.height;
        return 1;
    }, &width, &height);
}
namespace {
class BrowserPersistence : public Persistence {
    int id;
public:
    BrowserPersistence() {
        id = EM_ASM_INT({
            Module.persistenceResults ||= new Map();
            const id = Module.nextPersistenceId = (Module.nextPersistenceId || 0) + 1;
            Module.persistenceResults.set(id, 0);
            const complete = state => { if (Module.persistenceResults.has(id)) Module.persistenceResults.set(id, state); };
            Module.storage.flush().then(() => complete(1), () => complete(2));
            return id;
        });
    }
    ~BrowserPersistence() override { EM_ASM({ Module.persistenceResults.delete($0); }, id); }
    PersistenceState state() const override {
        return static_cast<PersistenceState>(EM_ASM_INT({ return Module.persistenceResults.get($0); }, id));
    }
};
}
std::unique_ptr<Persistence> persistStorage() { return std::make_unique<BrowserPersistence>(); }
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
