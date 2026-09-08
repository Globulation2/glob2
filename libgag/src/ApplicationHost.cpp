// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <SDL.h>

namespace GAGCore::ApplicationHost
{
void run(std::unique_ptr<Loop> loop, std::function<void()> complete)
{
    for (;;) {
        std::vector<SDL_Event> events;
        SDL_Event event;
        while (SDL_PollEvent(&event)) events.push_back(event);
        if (!loop->frame(SDL_GetTicks(), events)) break;
        wait(loop->delay(SDL_GetTicks()));
    }
    loop.reset();
    complete();
}

void wait(std::uint32_t milliseconds)
{
    if (milliseconds) SDL_Delay(milliseconds);
}
bool takeVisibilityChange(bool&) { return false; }
bool takeViewportSize(int&, int&) { return false; }
bool canExportFiles() { return false; }
bool exportFile(const std::string&, const std::vector<unsigned char>&) { return false; }
namespace {
class NativePersistence : public Persistence {
    PersistenceState state() const override { return PersistenceState::Succeeded; }
};
}
std::unique_ptr<Persistence> persistStorage() { return std::make_unique<NativePersistence>(); }
void screenChanged(const char*) {}
void simulationAdvanced(std::uint32_t) {}
void matchFrame(bool) {}
void exited(int) {}
}
