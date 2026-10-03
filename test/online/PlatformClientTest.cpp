// SPDX-License-Identifier: GPL-3.0-or-later
// PlatformClient against a scripted socket, HTTP and clock: sign-in, hello,
// correlation, timeouts, events, reconnect backoff, token refresh, keepalive,
// browser sign-in and sign-out.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "OnlineFakes.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"

using namespace Online;
using OnlineFakes::Json;

namespace
{
const std::string ORIGIN = "https://play.example.org";
const std::string CREDENTIAL(43, 'c');

ClientOptions testOptions()
{
	ClientOptions options;
	options.clientVersion = "test";
	options.platform = "desktop";
	options.backoffInitialMs = 1000;
	options.backoffMaxMs = 8000;
	return options;
}

struct Fixture
{
	OnlineFakes::World world;
	MemoryStorage storage;
	InstanceConfig config{storage};
	PlatformClient client{config, testOptions(), world.environment()};

	void advance(std::int64_t ms)
	{
		world.now += ms;
		world.wall += ms;
		client.update();
	}
	// Answers the pending guest sign-in with a new guest.
	void answerGuest(const std::string &refresh = "r1", std::int64_t lifetime = 600)
	{
		auto exchange = world.http.pending("/api/v1/auth/guest");
		REQUIRE(exchange);
		exchange->reply(200, Json{{"account", OnlineFakes::account()},
								  {"tokens", OnlineFakes::tokens(refresh, 1790000000, lifetime)},
								  {"deviceCredential", CREDENTIAL}});
		client.update();
	}
	// Connects the current socket and answers session.hello.
	void connect(bool withAccount = true)
	{
		world.socket().state = NetTransport::State::Connected;
		client.update();
		auto hello = world.socket().find("session.hello");
		REQUIRE(!hello.is_null());
		Json result = {{"sessionId", "00000000-0000-4000-8000-0000000000aa"},
					   {"serverTime", "2026-09-21T14:13:20Z"},
					   {"simSupported", true}};
		if (withAccount)
			result["account"] = OnlineFakes::account();
		world.socket().respond(hello, result);
		client.update();
	}
	// From nothing to an online guest.
	void startOnline()
	{
		client.start(ORIGIN);
		answerGuest();
		connect();
		REQUIRE(client.connection() == PlatformClient::Connection::Online);
	}
};
} // namespace

TEST_SUITE("PlatformClient")
{
	TEST_CASE("first start creates a guest, keeps its credential and says hello with the token")
	{
		Fixture f;
		f.client.start(ORIGIN);
		CHECK(f.client.connection() == PlatformClient::Connection::Connecting);
		CHECK(f.client.auth() == PlatformClient::Auth::SigningIn);
		CHECK(f.world.sockets.empty()); // waits for the token
		auto guest = f.world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		CHECK_EQ(guest->request.url, ORIGIN + "/api/v1/auth/guest");
		CHECK_EQ(guest->body(), Json{{"platform", "desktop"}});
		f.answerGuest("r1");
		CHECK(f.client.auth() == PlatformClient::Auth::SignedIn);
		CHECK_EQ(f.config.find(ORIGIN)->deviceCredential, CREDENTIAL);
		CHECK_EQ(f.config.find(ORIGIN)->refreshToken, "r1");
		CHECK(f.storage.files.count(InstanceConfig::FILE_NAME));
		REQUIRE(f.world.sockets.size() == 1);
		CHECK_EQ(f.world.socket().url, "wss://play.example.org/realtime");

		f.world.socket().state = NetTransport::State::Connected;
		f.client.update();
		CHECK(f.client.connection() == PlatformClient::Connection::Handshaking);
		auto hello = f.world.socket().find("session.hello");
		CHECK_EQ(hello["params"]["protocol"], 1);
		CHECK_EQ(hello["params"]["client"]["platform"], "desktop");
		CHECK_EQ(hello["params"]["client"]["version"], "test");
		CHECK(hello["params"]["client"]["simVersion"].contains("dataHash"));
		CHECK_EQ(hello["params"]["accessToken"], f.client.accessToken());
		f.world.socket().respond(hello, Json{{"sessionId", "s-1"},
											  {"serverTime", "2026-09-21T14:13:20Z"},
											  {"simSupported", false},
											  {"account", OnlineFakes::account("Guest-7")}});
		f.client.update();
		CHECK(f.client.connection() == PlatformClient::Connection::Online);
		CHECK_EQ(f.client.sessionId(), "s-1");
		CHECK_FALSE(f.client.simSupported());
		CHECK_EQ(f.client.account()->displayName, "Guest-7");
	}

	TEST_CASE("a returning player signs in with the device credential or refresh token")
	{
		Fixture f;
		f.config.record(ORIGIN).deviceCredential = CREDENTIAL;
		f.client.start(ORIGIN);
		CHECK_EQ(f.world.http.pending("/api/v1/auth/guest")->body()["deviceCredential"], CREDENTIAL);
		f.client.stop();

		Fixture g;
		g.config.record(ORIGIN).refreshToken = "stored";
		g.client.start(ORIGIN);
		auto refresh = g.world.http.pending("/api/v1/auth/refresh");
		REQUIRE(refresh);
		CHECK_EQ(refresh->body(), Json{{"refreshToken", "stored"}});
		CHECK_EQ(g.world.http.count("/api/v1/auth/guest"), 0);
		refresh->reply(200, OnlineFakes::tokens("rotated"));
		g.client.update();
		CHECK(g.client.auth() == PlatformClient::Auth::SignedIn);
		CHECK_EQ(g.config.find(ORIGIN)->refreshToken, "rotated");
		CHECK_EQ(g.world.sockets.size(), 1);
	}

	TEST_CASE("without consent to sign in, the socket connects anonymously")
	{
		Fixture f;
		f.config.record(ORIGIN).autoSignIn = false;
		f.config.record(ORIGIN).deviceCredential = CREDENTIAL;
		f.client.start(ORIGIN);
		CHECK(f.world.http.exchanges.empty());
		CHECK(f.client.auth() == PlatformClient::Auth::SignedOut);
		f.connect(false);
		CHECK_FALSE(f.world.socket().find("session.hello")["params"].contains("accessToken"));
		CHECK(f.client.connection() == PlatformClient::Connection::Online);
		CHECK_FALSE(f.client.account().has_value());
	}

	TEST_CASE("responses reach their own requests; timeouts and events are delivered")
	{
		Fixture f;
		f.startOnline();
		Json first, second, third;
		f.client.request("room.join", Json{{"code", "ABCDEF"}},
						 [&](const PlatformClient::Response &r) { first = r.ok ? r.result : Json(r.error.code); });
		f.client.request("room.leave", Json{{"roomId", "x"}},
						 [&](const PlatformClient::Response &r) { second = r.ok ? r.result : Json(r.error.code); });
		f.client.request("queue.join", Json::object(),
						 [&](const PlatformClient::Response &r) { third = r.ok ? r.result : Json(r.error.code); },
						 500);
		auto sent = f.world.socket().requests();
		REQUIRE(sent.size() >= 4);
		auto join = f.world.socket().find("room.join");
		auto leave = f.world.socket().find("room.leave");
		CHECK_NE(join["id"], leave["id"]);
		f.world.socket().fail(leave, "not_found");
		f.world.socket().respond(join, Json{{"room", {{"id", "r"}}}});
		f.client.update();
		CHECK_EQ(first["room"]["id"], "r");
		CHECK_EQ(second, "not_found");
		CHECK(third.is_null());
		f.advance(600);
		CHECK_EQ(third, "timeout");
		// A late answer to a timed-out request is ignored.
		f.world.socket().respond(f.world.socket().find("queue.join"), Json::object());
		f.client.update();
		CHECK_EQ(third, "timeout");

		std::vector<std::string> all, rooms;
		auto any = f.client.addListener("", [&](const std::string &name, const Json &) { all.push_back(name); });
		f.client.addListener("room.state", [&](const std::string &, const Json &data)
							 { rooms.push_back(data["room"]["id"]); });
		f.world.socket().event("room.state", Json{{"room", {{"id", "r"}}}});
		f.world.socket().event("queue.status", Json{{"ticketId", "t"}});
		f.client.update();
		CHECK_EQ(all, std::vector<std::string>{"room.state", "queue.status"});
		CHECK_EQ(rooms, std::vector<std::string>{"r"});
		f.client.removeListener(any);
		f.world.socket().event("room.state", Json{{"room", {{"id", "s"}}}});
		f.client.update();
		CHECK_EQ(all.size(), 2);
		CHECK_EQ(rooms.size(), 2);

		// Cancelled requests never call back.
		bool called = false;
		auto id = f.client.request("room.chat", Json::object(), [&](const PlatformClient::Response &) { called = true; });
		f.client.cancelRequest(id);
		f.world.socket().respond(f.world.socket().find("room.chat"), Json::object());
		f.advance(60000);
		CHECK_FALSE(called);
	}

	TEST_CASE("requests made before the socket is online wait for it")
	{
		Fixture f;
		f.client.start(ORIGIN);
		Json result;
		f.client.request("room.create", Json{{"name", "Mine"}},
						 [&](const PlatformClient::Response &r) { result = r.result; });
		f.answerGuest();
		f.world.socket().state = NetTransport::State::Connected;
		f.client.update();
		CHECK(f.world.socket().find("room.create").is_null()); // not before hello
		f.world.socket().respond(f.world.socket().find("session.hello"),
								 Json{{"sessionId", "s"}, {"serverTime", "x"}, {"simSupported", true}});
		f.client.update();
		auto create = f.world.socket().find("room.create");
		REQUIRE_FALSE(create.is_null());
		CHECK_EQ(create["params"]["name"], "Mine");
		f.world.socket().respond(create, Json{{"room", 1}});
		f.client.update();
		CHECK_EQ(result["room"], 1);
	}

	TEST_CASE("lost connections retry with growing, jittered backoff")
	{
		Fixture f;
		f.startOnline();
		std::string lost;
		f.client.request("room.join", Json::object(),
						 [&](const PlatformClient::Response &r) { lost = r.error.code; });
		f.world.socket().state = NetTransport::State::Closed;
		f.world.socket().error = "reset by peer";
		f.client.update();
		CHECK_EQ(lost, "disconnected");
		CHECK(f.client.connection() == PlatformClient::Connection::Waiting);
		CHECK_EQ(f.client.lastError(), "reset by peer");
		CHECK_EQ(f.client.retryInMs(), 1000);

		// Each failed attempt waits twice as long, up to the cap.
		std::vector<std::int64_t> waits;
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			const auto sockets = f.world.sockets.size();
			f.advance(f.client.retryInMs());
			REQUIRE(f.world.sockets.size() == sockets + 1);
			CHECK(f.client.connection() == PlatformClient::Connection::Connecting);
			f.world.socket().state = NetTransport::State::Closed;
			f.client.update();
			waits.push_back(f.client.retryInMs());
		}
		CHECK_EQ(waits, std::vector<std::int64_t>{2000, 4000, 8000, 8000, 8000});

		// Jitter shortens the wait by up to half.
		f.world.random = 0.5;
		f.advance(f.client.retryInMs());
		f.world.socket().state = NetTransport::State::Closed;
		f.client.update();
		CHECK_EQ(f.client.retryInMs(), 6000);

		// A successful hello resets the backoff.
		f.advance(f.client.retryInMs());
		f.connect();
		f.world.socket().state = NetTransport::State::Closed;
		f.world.random = 0.0;
		f.client.update();
		CHECK_EQ(f.client.retryInMs(), 1000);

		// retryNow skips the wait.
		const auto sockets = f.world.sockets.size();
		f.client.retryNow();
		f.client.update();
		CHECK_EQ(f.world.sockets.size(), sockets + 1);
	}

	TEST_CASE("a connection that never opens times out and retries")
	{
		Fixture f;
		f.client.start(ORIGIN);
		f.answerGuest();
		f.advance(20001);
		CHECK(f.client.connection() == PlatformClient::Connection::Waiting);
		CHECK(f.world.socket().closed);
	}

	TEST_CASE("tokens refresh before expiry, rotate the stored token and re-authenticate the socket")
	{
		Fixture f;
		f.client.start(ORIGIN);
		f.answerGuest("r1", 600);
		f.connect();
		const auto firstToken = f.client.accessToken();
		f.advance(479000);
		CHECK_EQ(f.world.http.count("/api/v1/auth/refresh"), 0);
		f.world.socket().respond(f.world.socket().last(), Json{{"serverTime", "x"}}); // keepalive answers
		f.advance(1000);
		auto refresh = f.world.http.pending("/api/v1/auth/refresh");
		REQUIRE(refresh);
		CHECK_EQ(refresh->body(), Json{{"refreshToken", "r1"}});

		// Refreshes are serialised: a rejected REST call waits for this one.
		Json me;
		f.client.rest(HttpFetch::Method::Get, "/api/v1/accounts/me", Json(),
					  [&](const PlatformClient::Response &r) { me = r.result; });
		auto get = f.world.http.pending("/api/v1/accounts/me");
		REQUIRE(get);
		CHECK_EQ(get->header("Authorization"), "Bearer " + firstToken);
		get->reply(401, Json{{"code", "unauthenticated"}, {"message", "expired"}});
		f.client.update();
		CHECK_EQ(f.world.http.count("/api/v1/auth/refresh"), 1);

		refresh->reply(200, OnlineFakes::tokens("r2", 1790000480, 600));
		f.client.update();
		CHECK_EQ(f.config.find(ORIGIN)->refreshToken, "r2");
		CHECK_NE(f.client.accessToken(), firstToken);
		auto authenticate = f.world.socket().find("session.authenticate");
		REQUIRE_FALSE(authenticate.is_null());
		CHECK_EQ(authenticate["params"]["accessToken"], f.client.accessToken());
		// The waiting call was retried with the new token.
		auto retried = f.world.http.pending("/api/v1/accounts/me");
		REQUIRE(retried);
		CHECK_EQ(retried->header("Authorization"), "Bearer " + f.client.accessToken());
		retried->reply(200, OnlineFakes::account("Guest-9"));
		f.client.update();
		CHECK_EQ(me["displayName"], "Guest-9");

		// The next refresh is scheduled from the new token's lifetime.
		f.world.socket().respond(f.world.socket().last(), Json{{"serverTime", "x"}});
		f.advance(479000);
		CHECK_EQ(f.world.http.count("/api/v1/auth/refresh"), 1);
		f.advance(2000);
		CHECK_EQ(f.world.http.count("/api/v1/auth/refresh"), 2);
	}

	TEST_CASE("a refused refresh token falls back to the device credential")
	{
		Fixture f;
		f.config.record(ORIGIN).refreshToken = "reused";
		f.config.record(ORIGIN).deviceCredential = CREDENTIAL;
		f.client.start(ORIGIN);
		f.world.http.pending("/api/v1/auth/refresh")
			->reply(401, Json{{"code", "unauthenticated"}, {"message", "reused"}});
		f.client.update();
		CHECK(f.config.find(ORIGIN)->refreshToken.empty());
		auto guest = f.world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		CHECK_EQ(guest->body()["deviceCredential"], CREDENTIAL);
		f.answerGuest("fresh");
		CHECK(f.client.auth() == PlatformClient::Auth::SignedIn);
		CHECK_EQ(f.config.find(ORIGIN)->refreshToken, "fresh");
	}

	TEST_CASE("a refresh that fails on the network is retried while the token lasts")
	{
		Fixture f;
		f.client.start(ORIGIN);
		f.answerGuest("r1", 600);
		f.connect();
		f.world.socket().respond(f.world.socket().last(), Json::object());
		f.advance(480000);
		auto refresh = f.world.http.pending("/api/v1/auth/refresh");
		REQUIRE(refresh);
		refresh->state = HttpFetch::State::Failed;
		refresh->error = "connection refused";
		f.client.update();
		CHECK(f.client.auth() == PlatformClient::Auth::SignedIn);
		CHECK_EQ(f.config.find(ORIGIN)->refreshToken, "r1");
		f.advance(3000);
		CHECK_EQ(f.world.http.count("/api/v1/auth/refresh"), 1);
		f.advance(2000);
		CHECK_EQ(f.world.http.count("/api/v1/auth/refresh"), 2);
	}

	TEST_CASE("a refused token at hello continues anonymously and signs in again")
	{
		Fixture f;
		f.config.record(ORIGIN).refreshToken = "r1";
		f.client.start(ORIGIN);
		f.world.http.pending("/api/v1/auth/refresh")->reply(200, OnlineFakes::tokens("r2"));
		f.client.update();
		f.world.socket().state = NetTransport::State::Connected;
		f.client.update();
		f.world.socket().fail(f.world.socket().find("session.hello"), "unauthenticated");
		f.client.update();
		auto hellos = 0;
		for (const auto &request : f.world.socket().requests())
			hellos += request["method"] == "session.hello";
		CHECK_EQ(hellos, 2);
		CHECK_FALSE(f.world.socket().find("session.hello")["params"].contains("accessToken"));
		auto refresh = f.world.http.pending("/api/v1/auth/refresh");
		REQUIRE(refresh);
		CHECK_EQ(refresh->body()["refreshToken"], "r2");
	}

	TEST_CASE("update_required stops retrying")
	{
		Fixture f;
		f.client.start(ORIGIN);
		f.answerGuest();
		f.world.socket().state = NetTransport::State::Connected;
		f.client.update();
		f.world.socket().fail(f.world.socket().find("session.hello"), "update_required");
		f.client.update();
		CHECK(f.client.connection() == PlatformClient::Connection::Stopped);
		f.advance(120000);
		CHECK_EQ(f.world.sockets.size(), 1);
	}

	TEST_CASE("an idle socket is checked with session.ping and replaced when silent")
	{
		Fixture f;
		f.startOnline();
		f.advance(24999);
		CHECK(f.world.socket().find("session.ping").is_null());
		f.advance(1);
		auto ping = f.world.socket().find("session.ping");
		REQUIRE_FALSE(ping.is_null());
		f.world.socket().respond(ping, Json{{"serverTime", "x"}});
		f.client.update();
		CHECK(f.client.connection() == PlatformClient::Connection::Online);
		f.advance(25000);
		f.advance(10000);
		CHECK(f.client.connection() == PlatformClient::Connection::Waiting);
		CHECK_EQ(f.client.lastError(), "The server stopped answering.");
	}

	TEST_CASE("browser sign-in opens the page, survives a reconnect and adopts the session")
	{
		Fixture f;
		f.startOnline();
		f.client.beginBrowserSignIn("link", "google");
		CHECK(f.client.handoff().state == PlatformClient::Handoff::State::Starting);
		auto begin = f.world.socket().find("auth.handoff.begin");
		CHECK_EQ(begin["params"], Json{{"mode", "link"}, {"provider", "google"}});
		const std::string resume(43, 'R');
		f.world.socket().respond(begin, Json{{"attemptId", "a-1"},
											  {"signInUrl", ORIGIN + "/signin?attempt=a-1"},
											  {"confirmationCode", "ABCD1234"},
											  {"expiresAt", "2026-09-21T14:23:20Z"},
											  {"resumeToken", resume}});
		f.client.update();
		CHECK(f.client.handoff().state == PlatformClient::Handoff::State::Waiting);
		CHECK_EQ(f.client.handoff().confirmationCode, "ABCD1234");
		CHECK(f.client.handoff().browserOpened);
		CHECK_EQ(f.world.opened, std::vector<std::string>{ORIGIN + "/signin?attempt=a-1"});

		// The phone drops the socket while the browser is in front.
		f.world.socket().state = NetTransport::State::Closed;
		f.client.update();
		f.advance(f.client.retryInMs());
		f.connect();
		auto resumed = f.world.socket().find("auth.handoff.resume");
		REQUIRE_FALSE(resumed.is_null());
		CHECK_EQ(resumed["params"], Json{{"attemptId", "a-1"}, {"resumeToken", resume}});
		f.world.socket().respond(resumed, Json{{"status", "finished"}});

		bool seen = false;
		f.client.addListener("auth.handoff.completed", [&](const std::string &, const Json &) { seen = true; });
		f.world.socket().event("auth.handoff.completed",
							   Json{{"attemptId", "a-1"},
									{"linked", true},
									{"session",
									 {{"account", OnlineFakes::account("Alice", "registered")},
									  {"tokens", OnlineFakes::tokens("registered-refresh")}}}});
		f.client.update();
		CHECK(seen);
		CHECK(f.client.handoff().state == PlatformClient::Handoff::State::Completed);
		CHECK(f.client.handoff().linked);
		CHECK_EQ(f.client.account()->displayName, "Alice");
		CHECK_EQ(f.client.account()->kind, "registered");
		CHECK_EQ(f.config.find(ORIGIN)->refreshToken, "registered-refresh");
		CHECK_EQ(f.config.find(ORIGIN)->deviceCredential, CREDENTIAL); // kept
	}

	TEST_CASE("browser sign-in failures, cancellation and expiry")
	{
		Fixture f;
		f.startOnline();
		f.world.openSucceeds = false;
		f.client.beginBrowserSignIn();
		auto begin = f.world.socket().find("auth.handoff.begin");
		CHECK_EQ(begin["params"], Json::object());
		f.world.socket().respond(begin, Json{{"attemptId", "a-2"},
											  {"signInUrl", ORIGIN + "/signin?attempt=a-2"},
											  {"confirmationCode", "CODE0002"},
											  {"expiresAt", "2026-09-21T14:23:20Z"},
											  {"resumeToken", std::string(43, 'x')}});
		f.client.update();
		CHECK_FALSE(f.client.handoff().browserOpened);
		f.world.openSucceeds = true;
		CHECK(f.client.openSignInPage());
		CHECK(f.client.handoff().browserOpened);
		f.world.socket().event("auth.handoff.failed",
							   Json{{"attemptId", "a-2"}, {"reason", "conflict"}, {"conflict", {{"reason", "identity_in_use"}}}});
		f.client.update();
		CHECK(f.client.handoff().state == PlatformClient::Handoff::State::Failed);
		CHECK_EQ(f.client.handoff().failure, "conflict");
		CHECK_EQ(f.client.handoff().conflict["reason"], "identity_in_use");

		f.client.beginBrowserSignIn("signin");
		auto second = f.world.socket().find("auth.handoff.begin");
		f.world.socket().respond(second, Json{{"attemptId", "a-3"},
											   {"signInUrl", ORIGIN + "/signin?attempt=a-3"},
											   {"confirmationCode", "CODE0003"},
											   {"expiresAt", "2026-09-21T14:23:20Z"},
											   {"resumeToken", std::string(43, 'y')}});
		f.client.update();
		f.client.cancelBrowserSignIn();
		CHECK_EQ(f.world.socket().find("auth.handoff.cancel")["params"], Json{{"attemptId", "a-3"}});
		CHECK_EQ(f.client.handoff().failure, "cancelled");

		// Locally expired when no result arrives (10 minutes plus a grace minute).
		f.client.beginBrowserSignIn();
		f.world.socket().respond(f.world.socket().find("auth.handoff.begin"),
								 Json{{"attemptId", "a-4"},
									  {"signInUrl", ORIGIN + "/signin?attempt=a-4"},
									  {"confirmationCode", "CODE0004"},
									  {"expiresAt", "2026-09-21T14:23:20Z"},
									  {"resumeToken", std::string(43, 'z')}});
		f.client.update();
		for (int i = 0; i < 70; ++i)
		{
			f.world.socket().respond(f.world.socket().last(), Json::object());
			f.advance(10000);
		}
		CHECK(f.client.handoff().state == PlatformClient::Handoff::State::Failed);
		CHECK_EQ(f.client.handoff().failure, "expired");

		// A sign-in page on another origin is never opened.
		f.client.beginBrowserSignIn();
		f.world.socket().respond(f.world.socket().find("auth.handoff.begin"),
								 Json{{"attemptId", "a-5"},
									  {"signInUrl", "https://evil.example/signin"},
									  {"confirmationCode", "CODE0005"},
									  {"expiresAt", "2026-09-21T15:23:20Z"},
									  {"resumeToken", std::string(43, 'w')}});
		const auto opened = f.world.opened.size();
		f.client.update();
		CHECK_EQ(f.world.opened.size(), opened);
	}

	TEST_CASE("session.revoked signs out and stops automatic sign-in")
	{
		Fixture f;
		f.startOnline();
		f.world.socket().event("session.revoked", Json{{"reason", "Signed out elsewhere."}});
		f.client.update();
		CHECK(f.client.auth() == PlatformClient::Auth::SignedOut);
		CHECK(f.client.accessToken().empty());
		CHECK(f.config.find(ORIGIN)->refreshToken.empty());
		CHECK_FALSE(f.config.find(ORIGIN)->autoSignIn);
		CHECK_EQ(f.client.lastError(), "Signed out elsewhere.");
	}

	TEST_CASE("sign-out revokes the refresh token, reconnects anonymously and stays signed out")
	{
		Fixture f;
		f.startOnline();
		f.client.signOut();
		auto signOut = f.world.http.pending("/api/v1/auth/sign-out");
		REQUIRE(signOut);
		CHECK_EQ(signOut->body(), Json{{"refreshToken", "r1"}});
		CHECK(f.client.auth() == PlatformClient::Auth::SignedOut);
		CHECK_FALSE(f.config.find(ORIGIN)->autoSignIn);
		CHECK_EQ(f.config.find(ORIGIN)->deviceCredential, CREDENTIAL);
		CHECK(f.config.find(ORIGIN)->refreshToken.empty());
		f.client.update();
		REQUIRE(f.world.sockets.size() == 2);
		f.connect(false);
		CHECK_FALSE(f.world.socket().find("session.hello")["params"].contains("accessToken"));
		CHECK_EQ(f.world.http.count("/api/v1/auth/guest"), 1);

		// Signing in as guest again uses the kept credential.
		f.client.signInAsGuest();
		auto guest = f.world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		CHECK_EQ(guest->body()["deviceCredential"], CREDENTIAL);
		f.answerGuest("again");
		CHECK(f.config.find(ORIGIN)->autoSignIn);
		CHECK_FALSE(f.world.socket().find("session.authenticate").is_null());
	}

	TEST_CASE("stop cancels everything and credentials stay per instance")
	{
		Fixture f;
		f.startOnline();
		std::string code;
		f.client.request("room.join", Json::object(), [&](const PlatformClient::Response &r) { code = r.error.code; });
		f.client.stop();
		CHECK_EQ(code, "cancelled");
		CHECK(f.client.connection() == PlatformClient::Connection::Stopped);
		CHECK(f.world.socket().closed);
		f.client.start("https://other.example");
		auto guest = f.world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		CHECK_EQ(guest->request.url, "https://other.example/api/v1/auth/guest");
		CHECK_FALSE(guest->body().contains("deviceCredential"));
	}

	TEST_CASE("rename patches the account")
	{
		Fixture f;
		f.startOnline();
		f.client.rename("Alice");
		auto patch = f.world.http.pending("/api/v1/accounts/me");
		REQUIRE(patch);
		CHECK(patch->request.method == HttpFetch::Method::Patch);
		CHECK_EQ(patch->body(), Json{{"displayName", "Alice"}});
		patch->reply(200, OnlineFakes::account("Alice", "registered"));
		f.client.update();
		CHECK_EQ(f.client.account()->displayName, "Alice");
		CHECK_EQ(f.config.find(ORIGIN)->lastDisplayName, "Alice");
	}
}
