// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <array>

namespace GAGCore
{
// Marks keyboard and mouse events that a local automation client (the MCP UI
// server) injected, in the event's `which` field. Remote events reach the game
// whether or not the window has OS focus; human events need focus.
inline constexpr Uint32 RemoteInputDevice = 0x474c4f42;
inline bool remoteInput(const SDL_Event &e)
{
	switch (e.type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return e.key.which == RemoteInputDevice;
	case SDL_EVENT_MOUSE_MOTION:
		return e.motion.which == RemoteInputDevice;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return e.button.which == RemoteInputDevice;
	case SDL_EVENT_MOUSE_WHEEL:
		return e.wheel.which == RemoteInputDevice;
	default:
		return false;
	}
}

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
		if (accepts(event) && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
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
	// Whether a keyboard or mouse event counts: with focus, or injected remotely.
	bool accepts(const SDL_Event &event) const { return focused || remoteInput(event); }

  private:
	std::array<Uint8, SDL_SCANCODE_COUNT> keys{};
	SDL_Keymod mods = SDL_KMOD_NONE;
	bool focused = true;
};
} // namespace GAGCore
