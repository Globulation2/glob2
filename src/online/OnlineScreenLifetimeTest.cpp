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
#include "OnlineGeneratorsScreen.h"
#include "GeneratorControls.h"
#include "OnlineHandoff.h"
#include "SimVersion.h"
#include "OnlineProfileScreen.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "QuickMatchScreen.h"
#include "SettingsScreen.h"
#include "ui/ApiErrorText.h"
#include "ui/DiagnosticText.h"
#include "ui/HiveReportText.h"
#include "ui/LibraryText.h"
#include <FormatableString.h>

#include <ScreenStack.h>

#include <functional>
#include <algorithm>
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
	TEST_CASE("generator examples with omitted controls use engine defaults when selecting a room")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		ScriptedPlatform platform;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		OnlineGeneratorsScreen screen(stack);
		const auto previous = Online::takePendingRoomMap();
		screen.detail = Json{
			{"generator", {{"id", "11111111-1111-4111-8111-111111111111"}, {"name", "Example"}}},
			{"versions",
			 Json::array(
				 {Json{{"id", "22222222-2222-4222-8222-222222222222"},
					   {"hash", std::string(64, 'a')},
					   {"packageHash", std::string(64, 'a')},
					   {"example",
						{{"seed", 19},
						 {"params", {{"workers", 3}}},
						 {"candidates", 1},
						 {"startingUnitLevel", 0}}},
					   {"metadata",
						{{"id", "test:generator"},
						 {"revision", 1},
						 {"editorOnly", false},
						 {"controls", Json::array({{{"id", "layout"}, {"default", 2}}})}}},
					   {"validations",
						Json::array({{{"valid", true},
									  {"suite", 1},
									  {"simVersion", Online::SimVersion::local().key()}}})}}})}};
		screen.selectRelease(0);
		for (const auto &control : sharedGeneratorControls())
			CHECK(screen.settings["params"][control.id] ==
				  (control.id == "workers" ? 3 : control.defaultValue));
		CHECK(screen.settings["params"]["layout"] == 2);
		CHECK_NOTHROW(screen.useInRoom());
		const auto selected = Online::takePendingRoomMap();
		REQUIRE(selected);
		REQUIRE(selected->scriptDescriptor);
		CHECK(Json::parse(*selected->scriptDescriptor).at("params") ==
			  screen.settings.at("params"));
		CHECK(selected->teamCount == screen.settings.at("params").at("teams").get<int>());
		if (previous)
			Online::useMapInRoom(*previous);
	}

	TEST_CASE("generator installation clears pending state when restoring the library throws")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		ScriptedPlatform platform;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		OnlineGeneratorsScreen screen(stack);
		struct Failed : GAGCore::ApplicationHost::Persistence
		{
			GAGCore::ApplicationHost::PersistenceState state() const override
			{
				return GAGCore::ApplicationHost::PersistenceState::Failed;
			}
		};
		// An unreadable checkpoint forces rollback to throw before touching storage.
		// ScriptGenerator separately covers writes failing during rollback.
		screen.before = "unreadable checkpoint";
		screen.replacing = "test:generator";
		screen.persistence = std::make_unique<Failed>();
		CHECK_NOTHROW(screen.onTimer(0));
		CHECK_FALSE(screen.persistence);
		CHECK(screen.before.empty());
		CHECK(screen.replacing.empty());
		CHECK(screen.status.find("Could not restore") != std::string::npos);
		CHECK(screen.status.find("previous library is retained") == std::string::npos);
		const auto previous = screen.status;
		CHECK_NOTHROW(screen.onTimer(0));
		CHECK(screen.status == previous);
		screen.before = "unreadable checkpoint";
		CHECK_NOTHROW(screen.failInstallation("Download failed."));
		CHECK(screen.before.empty());
		CHECK(screen.status.find("Download failed.") == 0);
		CHECK(screen.status.find("Could not restore") != std::string::npos);
	}

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
		closeInFlight(platform, "generators",
					  [&] { return std::make_unique<OnlineGeneratorsScreen>(stack); });
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

TEST_CASE("Bulgarian library callbacks retain API messages and summarize malformed responses" *
		  doctest::test_suite("OnlineScreenLifetime"))
{
	glob2test::GlobalsOptions options{.loadStrings = true};
	options.beforeLoad = [](GlobalContainer &g) { g.settings.language = "bg"; };
	glob2test::HeadlessGlobals globals(options);
	ScriptedPlatform platform;
	GAGGUI::ScreenStack stack(*globalContainer->gfx);
	OnlineGeneratorsScreen screen(stack);
	// Find the pending catalogue without depending on the default filter preference.
	auto catalogue = [&]() {
		for (const auto &request : platform.world.http.exchanges)
			if (request->state == HttpFetch::State::Pending &&
				request->request.url.find("/api/v1/generators?") != std::string::npos)
				return request;
		return decltype(platform.world.http.exchanges)::value_type{};
	};
	auto request = catalogue();
	REQUIRE(request);
	request->reply(200, Json{{"items", Json::array()}, {"nextCursor", Json::object()}});
	platform.client.update();
	CHECK_FALSE(screen.loading);
	CHECK_EQ(screen.status, Glob2UI::onlineProblemText(""));
	screen.fetch();
	request = catalogue();
	REQUIRE(request);
	const Json api = {{"code", "unauthenticated"}, {"message", "raw provider auth detail"},
		{"messageKey", "Sign in first (session.authenticate)."}};
	request->reply(400, api);
	platform.client.update();
	CHECK_FALSE(screen.loading);
	CHECK_EQ(screen.status, Glob2UI::apiErrorText(Online::errorFromJson(api, "")));
	CHECK_NE(screen.status, Glob2UI::onlineProblemText(""));
	// A failed rollback must not report that the previous library was retained.
	screen.before = "not valid JSON";
	screen.replacing = "pending-replacement";
	screen.failInstallation(Glob2UI::libraryText("Storage failed."));
	CHECK(screen.before.empty());
	CHECK(screen.replacing.empty());
	CHECK_FALSE(screen.persistence);
	CHECK_EQ(screen.status, Glob2UI::libraryText("Storage failed.") +
		GAGCore::FormattableString(Glob2UI::libraryText(" Could not restore the previous library: %0"))
			.arg(Glob2UI::onlineProblemText("")));
	CHECK(screen.status.find(Glob2UI::libraryText(" The previous library is retained.")) == std::string::npos);
	// Only a successful rollback may append the retained-state notice.
	screen.before = screen.library.checkpoint();
	screen.failInstallation(Glob2UI::libraryText("Storage failed."));
	CHECK_EQ(screen.status, Glob2UI::libraryText("Storage failed.") +
		Glob2UI::libraryText(" The previous library is retained."));
}

TEST_CASE("Bulgarian AI catalogue callback uses reviewed response errors and keeps API specificity" *
		  doctest::test_suite("OnlineScreenLifetime"))
{
	glob2test::GlobalsOptions options{.loadStrings = true};
	options.beforeLoad = [](GlobalContainer &g) { g.settings.language = "bg"; };
	glob2test::HeadlessGlobals globals(options);
	ScriptedPlatform platform;
	SettingsScreen screen;
	screen.selectCategory(SettingsScreen::Category::CustomAIs);
	screen.buildCustomAIs();
	screen.openCustomAILibrary();
	const std::string path = "/api/v1/ais?limit=24&sort=likes&q=&tags=";
	auto hasNotice = [&](const std::string &text)
	{
		screen.form.clear();
		screen.buildCustomAIs();
		return std::any_of(screen.form.begin(), screen.form.end(),
			[&](const auto &row) { return row.label == text; });
	};
	for (const auto &malformed : {
		Json{{"items", false}}, Json{{"items", Json::array({{{"name", false}}})}},
		Json::object(), Json{{"items", Json::array()}, {"nextCursor", Json::object()}}})
	{
		auto request = platform.world.http.pending(path);
		REQUIRE(request);
		request->reply(200, malformed);
		platform.client.update();
		CHECK(hasNotice(Glob2UI::tr("[Invalid AI catalogue response]")));
		screen.fetchCustomAIs();
	}
	auto request = platform.world.http.pending(path);
	REQUIRE(request);
	const Json api = {{"code", "unauthenticated"}, {"message", "raw auth reason"},
		{"messageKey", "Sign in first (session.authenticate)."}};
	request->reply(400, api);
	platform.client.update();
	CHECK(hasNotice(Glob2UI::apiErrorText(Online::errorFromJson(api, ""))));
	CHECK_FALSE(hasNotice(Glob2UI::onlineProblemText("")));
}

TEST_CASE("Diagnostic UI preserves reviewed native summaries, raw values and explicit report provenance" *
		  doctest::test_suite("OnlineScreenLifetime"))
{
	glob2test::GlobalsOptions options{.loadStrings = true};
	options.beforeLoad = [](GlobalContainer &g) { g.settings.language = "bg"; };
	glob2test::HeadlessGlobals globals(options);
	Glob2UI::DiagnosticText diagnostic;
	const std::string raw = "Unknown option preserved-id";
	CHECK_EQ(diagnostic(raw), Glob2UI::onlineProblemText(""));
	CHECK_EQ(diagnostic(raw), Glob2UI::onlineProblemText(""));
	CHECK_EQ(raw, "Unknown option preserved-id");
	CHECK_EQ(diagnostic("Give at least one terrain type a nonzero weight."),
		Glob2UI::nativeSurfaceText("[Give at least one terrain type a nonzero weight.]", ""));
	const Glob2UI::PresentedError deliberate("already presented detail", "raw transport");
	CHECK_EQ(diagnostic.exception(deliberate), "already presented detail");
	CHECK_EQ(Glob2UI::literalSurfaceFormat("%1 / %0 / %1", {"literal %1", "user {p0}"}),
		"user {p0} / literal %1 / user {p0}");
	const std::string text = "Order received: literal %0 {p0}";
	CHECK_EQ(Glob2UI::hiveReportText(Hive::Report(text)), text);
	const Hive::Report local("Order received: literal %0 {p0}", Hive::ReportSource::OrderReceived,
		"literal %0 {p0}");
	CHECK_EQ(Glob2UI::hiveReportText(local), Glob2UI::literalSurfaceFormat(
		Glob2UI::nativeSurfaceText("[hive order received %0]", "Order received: %0"), {local.parameter}));
	CHECK_EQ(local.text, text);
	// The old status words in a remote report remain user/commander text.
	CHECK_EQ(Glob2UI::hiveReportText(Hive::Report("Stopped")), "Stopped");
	CHECK_EQ(Glob2UI::hiveReportText(Hive::Report("Stopped", Hive::ReportSource::Stopped)),
		Glob2UI::nativeSurfaceText("[hive stopped]", "Stopped"));
}
