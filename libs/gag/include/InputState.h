// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <array>

namespace GAGCore
{
// Held input belongs to the processed event stream, not SDL's latest poll.
class InputState
{
  public:
	void observe(const SDL_Event &event)
	{
		if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
		{
			if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
			{
				clearHeld();
				focused = false;
			}
			else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
				focused = true;
		}
		if (focused && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
		{
			const auto code = event.key.scancode == SDL_SCANCODE_UNKNOWN
								  ? SDL_GetScancodeFromKey(event.key.key, nullptr)
								  : event.key.scancode;
			if (code > SDL_SCANCODE_UNKNOWN && code < SDL_SCANCODE_COUNT)
				keys[code] = event.type == SDL_EVENT_KEY_DOWN;
			mods = static_cast<SDL_Keymod>(event.key.mod);
		}
	}
	void clearHeld()
	{
		keys.fill(0);
		mods = SDL_KMOD_NONE;
	}
	const Uint8 *keyboard() const { return keys.data(); }
	SDL_Keymod modifiers() const { return mods; }
	bool hasFocus() const { return focused; }

  private:
	std::array<Uint8, SDL_SCANCODE_COUNT> keys{};
	SDL_Keymod mods = SDL_KMOD_NONE;
	bool focused = true;
};
} // namespace GAGCore
