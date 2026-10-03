// SPDX-License-Identifier: GPL-3.0-or-later
// The real side effects of PlatformClient: WebSocket text transport, HttpFetch,
// clocks and the system browser. Kept apart so unit tests link the client
// against fakes.
#include "NetTransport.h"
#include "PlatformClient.h"

#include <ApplicationHost.h>

#include <chrono>
#include <random>

namespace Online
{
ClientEnvironment ClientEnvironment::native()
{
	ClientEnvironment env;
	env.makeTransport = [] { return makeNetTransport({}, NetMessageMode::Text); };
	env.startFetch = [](HttpFetch::Request request) { return HttpFetch::start(std::move(request)); };
	env.now = []
	{
		return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now().time_since_epoch())
								.count());
	};
	env.wallClock = []
	{
		return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch())
								.count());
	};
	auto generator = std::make_shared<std::mt19937_64>(std::random_device{}());
	env.random = [generator]
	{ return std::uniform_real_distribution<double>(0.0, 1.0)(*generator); };
	env.openUrl = [](const std::string &url) { return GAGCore::ApplicationHost::openUrl(url); };
	return env;
}
} // namespace Online
