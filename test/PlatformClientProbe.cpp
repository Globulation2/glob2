// SPDX-License-Identifier: GPL-3.0-or-later
// Drives PlatformClient against a real platform instance for
// tests/online/test_platform_client.py. Prints one JSON object per line.
//
//   platform-client-probe <origin> <state directory> <scenario>
//
// Scenarios: session (sign in, requests, reconnect, refresh, browser sign-in
// start and cancel), returning (sign in with what the state directory holds),
// link (a browser sign-in the test completes on the web page), signout (sign
// in, then sign out).
#include "InstanceConfig.h"
#include "NetTransport.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"
#include <FileManager.h>
#include <Toolkit.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <thread>

using namespace Online;
using Json = nlohmann::json;
namespace fs = std::filesystem;

// This probe has no game data; SimVersion::local() reports the unsupported
// zero data hash, just as the other standalone online probe does.
namespace GAGCore
{
FileManager *Toolkit::fileManager = nullptr;
StreamBackend *FileManager::openInputStreamBackend(const std::string)
{
	return nullptr;
}
} // namespace GAGCore

namespace
{
class DirectoryStorage final : public OnlineStorage
{
	fs::path root;

  public:
	explicit DirectoryStorage(fs::path root) : root(std::move(root)) {}
	bool read(const std::string &path, std::string &contents) override
	{
		std::ifstream file(root / path, std::ios::binary);
		if (!file)
			return false;
		std::ostringstream buffer;
		buffer << file.rdbuf();
		contents = buffer.str();
		return true;
	}
	bool write(const std::string &path, const std::string &contents) override
	{
		const auto target = root / path;
		fs::create_directories(target.parent_path());
		const auto temporary = target.string() + ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!(file << contents))
				return false;
		}
		fs::rename(temporary, target);
		return true;
	}
	void remove(const std::string &path) override
	{
		std::error_code ignored;
		fs::remove(root / path, ignored);
	}
	std::vector<std::string> list(const std::string &directory) override
	{
		std::vector<std::string> names;
		std::error_code ignored;
		for (const auto &entry : fs::directory_iterator(root / directory, ignored))
			names.push_back(entry.path().filename().string());
		return names;
	}
};

std::int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

void emit(const Json &line)
{
	std::cout << line.dump() << std::endl;
}

[[noreturn]] void fail(const std::string &why)
{
	emit({{"event", "failed"}, {"reason", why}});
	std::exit(1);
}

struct Probe
{
	DirectoryStorage storage;
	InstanceConfig config{storage};
	std::vector<std::string> opened;
	PlatformClient client;
	std::string origin;

	Probe(const std::string &origin, const fs::path &state)
		: storage(state), client(config, options(), environment()), origin(origin)
	{
		config.load();
	}
	static ClientOptions options()
	{
		ClientOptions options;
		options.clientVersion = "probe";
		options.backoffInitialMs = 500;
		options.backoffMaxMs = 4000;
		return options;
	}
	ClientEnvironment environment()
	{
		ClientEnvironment env;
		env.makeTransport = [] { return makeNetTransport({}, NetMessageMode::Text); };
		env.startFetch = [](HttpFetch::Request request) { return HttpFetch::start(std::move(request)); };
		env.now = nowMs;
		env.wallClock = []
		{
			return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch())
									.count());
		};
		auto generator = std::make_shared<std::mt19937_64>(std::random_device{}());
		env.random = [generator] { return std::uniform_real_distribution<double>(0, 1)(*generator); };
		env.openUrl = [this](const std::string &url)
		{
			opened.push_back(url);
			return true;
		};
		return env;
	}
	template <class Done> void until(Done done, std::int64_t timeoutMs, const std::string &what)
	{
		const auto deadline = nowMs() + timeoutMs;
		while (!done())
		{
			if (nowMs() > deadline)
				fail("timed out waiting for " + what + " (connection " +
					 connectionName(client.connection()) + ", auth " + authName(client.auth()) +
					 ", last error: " + client.lastError() + ")");
			client.update();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
	PlatformClient::Response call(const std::string &method, Json params)
	{
		std::optional<PlatformClient::Response> result;
		client.request(method, std::move(params), [&](const PlatformClient::Response &r) { result = r; });
		until([&] { return result.has_value(); }, 20000, method);
		return *result;
	}
	PlatformClient::Response rest(HttpFetch::Method method, const std::string &path, Json body = {})
	{
		std::optional<PlatformClient::Response> result;
		client.rest(method, path, std::move(body), [&](const PlatformClient::Response &r) { result = r; });
		until([&] { return result.has_value(); }, 20000, path);
		return *result;
	}
	void online()
	{
		until([&] { return client.connection() == PlatformClient::Connection::Online &&
						   client.auth() != PlatformClient::Auth::SigningIn; },
			  30000, "online");
	}
	Json state()
	{
		const auto *record = config.find(origin);
		return {{"connection", connectionName(client.connection())},
				{"auth", authName(client.auth())},
				{"accountId", client.account() ? client.account()->id : ""},
				{"displayName", client.account() ? client.account()->displayName : ""},
				{"kind", client.account() ? client.account()->kind : ""},
				{"simSupported", client.simSupported()},
				{"sessionId", client.sessionId()},
				{"refreshToken", record ? record->refreshToken : ""},
				{"deviceCredential", record ? record->deviceCredential : ""},
				{"autoSignIn", record ? record->autoSignIn : true}};
	}
	static Json summary(const PlatformClient::Response &response)
	{
		return response.ok ? Json{{"ok", true}, {"result", response.result}}
						   : Json{{"ok", false}, {"code", response.error.code}};
	}
};
} // namespace

int main(int argc, char **argv)
{
	if (argc != 4)
	{
		std::cerr << "usage: platform-client-probe <origin> <state directory> <scenario>\n";
		return 2;
	}
	auto origin = normalizeOrigin(argv[1]);
	if (!origin)
		fail("invalid origin");
	Probe probe(*origin, argv[2]);
	const std::string scenario = argv[3];
	probe.client.start(*origin);
	probe.online();
	auto start = probe.state();
	start["event"] = "online";
	emit(start);

	if (scenario == "session")
	{
		emit({{"event", "ping"}, {"response", Probe::summary(probe.call("session.ping", Json::object()))}});
		emit({{"event", "unsupported"},
			  {"response", Probe::summary(probe.call("room.join", Json{{"code", "ABCDEF12"}}))}});
		emit({{"event", "bad-params"},
			  {"response", Probe::summary(probe.call("session.authenticate", Json::object()))}});
		emit({{"event", "me"}, {"response", Probe::summary(probe.rest(HttpFetch::Method::Get, "/api/v1/accounts/me"))}});

		// The test drops every connection now; the client must come back.
		const auto session = probe.client.sessionId();
		emit({{"event", "await-drop"}});
		probe.until([&] { return probe.client.connection() != PlatformClient::Connection::Online; }, 30000,
					"the connection to drop");
		probe.online();
		emit({{"event", "reconnected"},
			  {"newSession", probe.client.sessionId() != session},
			  {"state", probe.state()}});

		// Access tokens live one minute on the test instance: wait for the
		// scheduled refresh and check the socket and REST still work.
		const auto before = probe.state()["refreshToken"].get<std::string>();
		probe.until([&] { return probe.state()["refreshToken"] != before; }, 90000, "a token refresh");
		const auto afterRefresh = probe.call("session.ping", Json::object());
		probe.until([&] { return probe.client.auth() == PlatformClient::Auth::SignedIn; }, 5000, "signed in");
		emit({{"event", "refreshed"},
			  {"previousRefreshToken", before},
			  {"state", probe.state()},
			  {"ping", Probe::summary(afterRefresh)},
			  {"me", Probe::summary(probe.rest(HttpFetch::Method::Get, "/api/v1/accounts/me"))}});

		probe.client.beginBrowserSignIn("link");
		probe.until([&] { return probe.client.handoff().state != PlatformClient::Handoff::State::Starting; },
					20000, "auth.handoff.begin");
		const auto handoff = probe.client.handoff();
		emit({{"event", "handoff"},
			  {"state", int(handoff.state)},
			  {"signInUrl", handoff.signInUrl},
			  {"confirmationCode", handoff.confirmationCode},
			  {"opened", probe.opened}});
		probe.client.cancelBrowserSignIn();
		probe.until([&] { return probe.client.handoff().failure == "cancelled"; }, 20000, "cancel");
		// The server confirms with auth.handoff.failed (cancelled) or not at all.
		const auto pause = nowMs() + 500;
		probe.until([&] { return nowMs() > pause; }, 2000, "pause");
		emit({{"event", "handoff-cancelled"}, {"failure", probe.client.handoff().failure}});
	}
	else if (scenario == "link")
	{
		// The test signs in on the web page (dropping the socket first, as a
		// phone switching to its browser would) while the client waits.
		probe.client.beginBrowserSignIn("link");
		probe.until([&] { return probe.client.handoff().state != PlatformClient::Handoff::State::Starting; },
					20000, "auth.handoff.begin");
		emit({{"event", "handoff-ready"},
			  {"signInUrl", probe.client.handoff().signInUrl},
			  {"confirmationCode", probe.client.handoff().confirmationCode}});
		probe.until([&] { return probe.client.handoff().state == PlatformClient::Handoff::State::Completed ||
								 probe.client.handoff().state == PlatformClient::Handoff::State::Failed; },
					90000, "the browser sign-in");
		const auto handoff = probe.client.handoff();
		auto after = probe.state();
		after["event"] = "handoff-finished";
		after["completed"] = handoff.state == PlatformClient::Handoff::State::Completed;
		after["linked"] = handoff.linked;
		after["failure"] = handoff.failure;
		after["me"] = Probe::summary(probe.rest(HttpFetch::Method::Get, "/api/v1/accounts/me"));
		emit(after);
	}
	else if (scenario == "signout")
	{
		probe.client.signOut();
		probe.until([&] { return probe.client.connection() != PlatformClient::Connection::Online; }, 5000,
					"disconnect");
		probe.online();
		const auto pause = nowMs() + 1000; // let the sign-out request finish
		probe.until([&] { return nowMs() > pause; }, 3000, "pause");
		auto after = probe.state();
		after["event"] = "signed-out";
		emit(after);
	}
	probe.client.stop();
	emit({{"event", "done"}});
	return 0;
}
