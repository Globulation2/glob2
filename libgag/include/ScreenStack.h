// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <functional>
#include <memory>
#include <vector>

namespace GAGGUI
{
// Owns screens through completion. Mutations requested by callbacks become
// visible at the next frame boundary; a child never receives its opening input.
class ScreenStack
{
public:
    using Completion = std::function<void(Screen&, int)>;
    explicit ScreenStack(GAGCore::DrawableSurface& surface) : surface(surface) {}
    ~ScreenStack();
    void push(std::unique_ptr<Screen> screen, Completion completed = {});
    void frame(Uint32 tick, const std::vector<SDL_Event>& events);
    bool running() const { return !stopped && (!screens.empty() || !pending.empty()); }
    Uint32 delay(Uint32 now, Uint32 fallback);
    int result() const { return lastResult; }
    // Transitional SDL host. Browser scheduling will call frame directly.
    int execute(unsigned stepLength = 40);
    void stop();
private:
    struct Entry { std::unique_ptr<Screen> screen; Completion completed; Screen* owner; };
    GAGCore::DrawableSurface& surface;
    std::vector<Entry> screens, pending;
    int lastResult = 0;
    bool stopped = false, dispatching = false;
    void boundary();
};
}
