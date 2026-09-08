// SPDX-License-Identifier: GPL-3.0-or-later
#include <ScreenStack.h>
#include <ApplicationHost.h>
#include <stdexcept>
#include <typeinfo>
#include <algorithm>

namespace GAGGUI
{
ScreenStack::~ScreenStack()
{
    stop();
    boundary();
}

void ScreenStack::push(std::unique_ptr<Screen> screen, Completion completed)
{
    if (!screen) throw std::invalid_argument("Cannot push a null screen");
    if (stopped) throw std::logic_error("Cannot push onto a stopped screen stack");
    pending.push_back({std::move(completed), std::move(screen),
                       screens.empty() ? nullptr : screens.back().screen.get()});
}

void ScreenStack::suspendExecution()
{
    for (auto& entry : screens) entry.screen->suspendExecution();
    for (auto& entry : pending) entry.screen->suspendExecution();
}

void ScreenStack::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    for (auto& entry : screens) entry.screen->viewportResized(oldWidth, oldHeight, width, height);
    for (auto& entry : pending) entry.screen->viewportResized(oldWidth, oldHeight, width, height);
}

void ScreenStack::stop()
{
    stopped = true;
    // Destruction is deferred until outside screen callbacks.
}

void ScreenStack::boundary()
{
    if (!screens.empty() && !screens.back().screen->isExecutionRunning()) {
        Entry completed = std::move(screens.back());
        screens.pop_back();
        // A parent that completes before its queued child is admitted cancels
        // that child, including continuations capturing the parent.
        std::erase_if(pending, [&](const Entry& entry) { return entry.owner == completed.screen.get(); });
        lastResult = completed.screen->finishExecution();
        if (lastResult == Screen::QUIT_APPLICATION) stopped = true;
        if (!stopped && completed.completed) completed.completed(*completed.screen, lastResult);
        if (!screens.empty()) {
            Screen& resumed = *screens.back().screen;
            GAGCore::ApplicationHost::screenChanged(typeid(resumed).name());
        }
    }
    if (stopped) {
        pending.clear();
        while (!screens.empty()) {
            screens.back().screen->endExecute(Screen::QUIT_APPLICATION);
            screens.back().screen->finishExecution();
            screens.pop_back();
        }
        lastResult = Screen::QUIT_APPLICATION;
        return;
    }
    // Creation callbacks can queue another child, but cannot recurse into it.
    auto additions = std::move(pending);
    pending.clear();
    for (auto& entry : additions) {
        screens.push_back(std::move(entry));
        screens.back().screen->beginExecution(&surface);
    }
}

void ScreenStack::frame(Uint32 tick, const std::vector<SDL_Event>& events)
{
    if (dispatching) throw std::logic_error("Screen stack frames cannot recurse");
    struct Guard { bool& flag; Guard(bool& f): flag(f) { flag = true; } ~Guard() { flag = false; } } guard(dispatching);
    if (std::any_of(events.begin(), events.end(), [](const SDL_Event& e) { return e.type == SDL_QUIT; })) stop();
    boundary();
    if (screens.empty() || stopped) return;
    Screen& screen = *screens.back().screen;
    // Pending child transitions suspend the parent immediately.
    if (pending.empty()) screen.updateExecution(tick);
    for (const auto& event : events) {
        if (event.type == SDL_QUIT) { stop(); break; }
        if (stopped || !pending.empty() || !screen.isExecutionRunning()) break;
        screen.handleExecutionEvent(event);
    }
    if (!stopped) screen.drawExecution();
    if (stopped) boundary();
}

Uint32 ScreenStack::delay(Uint32 now, Uint32 fallback)
{
    return screens.empty() ? fallback : screens.back().screen->executionDelay(now, fallback);
}

int ScreenStack::execute(unsigned stepLength)
{
    while (running()) {
        const Uint64 start = SDL_GetTicks64();
        std::vector<SDL_Event> events;
        SDL_Event event;
        while (SDL_PollEvent(&event)) events.push_back(event);
        frame(static_cast<Uint32>(start), events);
        if (running()) {
            const Uint64 elapsed = SDL_GetTicks64() - start;
            const Uint32 fallback = elapsed < stepLength ? stepLength - elapsed : 0;
            GAGCore::ApplicationHost::wait(delay(static_cast<Uint32>(SDL_GetTicks64()), fallback));
        }
    }
    return result();
}
}
