// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <array>

namespace GAGCore
{
// Held input belongs to the processed event stream, not SDL's latest poll.
class InputState
{
public:
    void observe(const SDL_Event& event)
    {
        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                clearHeld(); focused = false;
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
        }
        if (focused && (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)) {
            const auto code = event.key.keysym.scancode == SDL_SCANCODE_UNKNOWN
                ? SDL_GetScancodeFromKey(event.key.keysym.sym) : event.key.keysym.scancode;
            if (code > SDL_SCANCODE_UNKNOWN && code < SDL_NUM_SCANCODES)
                keys[code] = event.type == SDL_KEYDOWN;
            mods = static_cast<SDL_Keymod>(event.key.keysym.mod);
        }
    }
    void clearHeld() { keys.fill(0); mods = KMOD_NONE; }
    const Uint8* keyboard() const { return keys.data(); }
    SDL_Keymod modifiers() const { return mods; }
    bool hasFocus() const { return focused; }
private:
    std::array<Uint8, SDL_NUM_SCANCODES> keys{};
    SDL_Keymod mods = KMOD_NONE;
    bool focused = true;
};
}
