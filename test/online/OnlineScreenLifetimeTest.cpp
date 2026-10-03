// SPDX-License-Identifier: GPL-3.0-or-later
// The online screens closed while their platform calls are in flight: the
// shared client (Online::services()) outlives every screen, so a call
// that still held a screen when its answer arrived would run on freed memory.
// Each screen is opened on a scripted platform, made to start its requests,
// destroyed, and then every request is answered. The screen's calls must be
// gone from the client (and its HTTP fetches cancelled) the moment it closes;
// under AddressSanitizer a stray callback would also fail as a use-after-free.
//
// Built with -fno-access-control (test/tests.py) to start the settings
// screen's calls without a display.

#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "OnlineFakes.h"
#include "OnlineHubScreen.h"
#include "OnlineMapsScreen.h"
#include "OnlineProfileScreen.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "QuickMatchScreen.h"
#include "SettingsScreen.h"

#include <ScreenStack.h>

#include <functional>
#include <memory>

using OnlineFakes::Json;

namespace
{
const std::string ORIGIN = "https://play.example.org";

struct ScriptedPlatform
{
	OnlineFakes::World world;
	Online::PlatformClient &client = Online::services().client;

	ScriptedPlatform()
	{
		client.replaceEnvironment(world.environment());
		client.start(ORIGIN);
		auto guest = world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		guest->reply(200, Json{{"account", OnlineFakes::account()},
							   {"tokens", OnlineFakes::tokens("r1", 1790000000, 600)},
							   {"deviceCredential", std::string(43, 'c')}});
		client.update();
		world.socket().state = NetTransport::State::Connected;
		client.update();
		auto hello = world.socket().find("session.hello");
		REQUIRE(!hello.is_null());
		world.socket().respond(hello, Json{{"sessionId", "00000000-0000-4000-8000-0000000000aa"},
										   {"serverTime", "2026-09-21T14:13:20Z"},
										   {"simSupported", true},
										   {"account", OnlineFakes::account()}});
		client.update();
		REQUIRE(client.connection() == Online::PlatformClient::Connection::Online);
	}
	~ScriptedPlatform()
	{
		// Back to the real network for any later test in this process.
		client.replaceEnvironment(Online::ClientEnvironment::native());
	}
	// Answers every outstanding HTTP request and realtime request, then lets the
	// client deliver the answers.
	void answerEverything()
	{
		for (const auto &exchange : world.http.exchanges)
			if (exchange->state == HttpFetch::State::Pending)
				exchange->reply(200, Json{{"items", Json::array()}, {"entries", Json::array()}, {"name", "Example"}});
		for (const auto &request : world.socket().requests())
			if (request.value("method", "") != "session.hello")
				world.socket().respond(request, Json::object());
		world.socket().event("match.updated", Json{{"match", {{"id", "m"}}}});
		world.socket().event("queue.status", Json{{"ticketId", "t"}});
		for (int i = 0; i < 3; ++i)
		{
			world.now += 10;
			client.update();
			Online::pump();
		}
	}
};

// Opens a screen, lets it start its calls, closes it, and checks nothing of it
// is left on the client.
void closeInFlight(ScriptedPlatform &platform, const std::string &name,
				   const std::function<std::unique_ptr<GAGGUI::Screen>()> &open)
{
	INFO(name);
	const auto baseline = platform.client.pendingCalls();
	const auto firstExchange = platform.world.http.exchanges.size();
	auto screen = open();
	REQUIRE(screen);
	CHECK(platform.client.pendingCalls() > baseline);
	screen.reset();
	CHECK_EQ(platform.client.pendingCalls(), baseline);
	for (std::size_t i = firstExchange; i < platform.world.http.exchanges.size(); ++i)
	{
		const auto &exchange = platform.world.http.exchanges[i];
		// The shared /api/v1/instance fetch outlives its callers; it is cached.
		if (exchange->request.url.find("/api/v1/instance") == std::string::npos)
			CHECK_MESSAGE(exchange->cancelled, exchange->request.url);
	}
	platform.answerEverything();
	CHECK_EQ(platform.client.pendingCalls(), baseline);
}
} // namespace

TEST_SUITE("OnlineScreenLifetime")
{
	TEST_CASE("online screens closed with platform calls in flight are never called back")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		ScriptedPlatform platform;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);

		closeInFlight(platform, "hub", [&] { return std::make_unique<OnlineHubScreen>(stack, true); });
		closeInFlight(platform, "quick match", [&] { return std::make_unique<QuickMatchScreen>(stack); });
		closeInFlight(platform, "profile", [&]
		{
			auto screen = std::make_unique<OnlineProfileScreen>(stack);
			screen->onTimer(0);
			return screen;
		});
		closeInFlight(platform, "maps", [&]
		{
			auto screen = std::make_unique<OnlineMapsScreen>(stack, OnlineMapsScreen::Tab::Browse);
			screen->onTimer(0);
			return screen;
		});
		closeInFlight(platform, "settings: online", [&]
		{
			auto screen = std::make_unique<SettingsScreen>();
			screen->selectCategory(SettingsScreen::Category::Online);
			screen->buildOnline();
			screen->unlinkProvider("google", "Google");
			return screen;
		});
		// A screen closed after its answers arrived is also fine, and the hub
		// shares the cached instance description instead of fetching it again.
		const auto fetches = platform.world.http.count("/api/v1/instance");
		{
			OnlineHubScreen hub(stack, true);
			platform.answerEverything();
		}
		CHECK_EQ(platform.world.http.count("/api/v1/instance"), fetches);
		QuickMatchPresenter::detach(stack);
		platform.client.stop();
	}
}
