// SPDX-License-Identifier: GPL-3.0-or-later
// The shared quick-match search of the running game: native relay probes,
// window attention and the hand-off to the connecting flow.
#include "QuickMatch.h"

#include "OnlineHandoff.h"
#include "OnlineServices.h"
#include "RelayProbe.h"

#include <SDL3/SDL.h>

#include <chrono>

namespace Online
{
namespace
{
// Flashes the game window until it has focus (taskbar or dock attention).
// Phones have no equivalent while the app is in front; a backgrounded app
// would need a local notification, which the platform layer does not offer.
void requestAttention()
{
#if SDL_VERSION_ATLEAST(2, 0, 16) && !defined(__EMSCRIPTEN__)
	for (Uint32 id = 1; id <= 8; ++id)
		if (SDL_Window *window = SDL_GetWindowFromID(id))
		{
			SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);
			return;
		}
#endif
}
} // namespace

std::unique_ptr<RelayProbe> RelayProbe::native(const std::string &origin)
{
	return std::make_unique<RelayProbe>(
		origin, [](HttpFetch::Request request) { return HttpFetch::start(std::move(request)); },
		[]
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now().time_since_epoch())
				.count();
		});
}

QuickMatch::Environment QuickMatch::Environment::native()
{
	Environment env;
	env.probe = [](const std::string &origin) { return RelayProbe::native(origin); };
	env.wallClock = []
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	};
	env.attention = requestAttention;
	env.handoff = [](const MatchAssignment &assignment) { beginMatch(assignment); };
	return env;
}

QuickMatch &quickMatch()
{
	static QuickMatch *shared = nullptr;
	if (!shared)
	{
		shared = new QuickMatch(services().client, QuickMatch::Environment::native());
		addPumpHook([] { shared->update(); });
	}
	return *shared;
}
} // namespace Online
