// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <emscripten.h>

namespace GAGCore::ApplicationHost
{
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
