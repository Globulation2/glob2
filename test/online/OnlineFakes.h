// SPDX-License-Identifier: GPL-3.0-or-later
// Scripted transport, HTTP and clock for PlatformClient and MapCache tests.
#pragma once

#include "HttpFetch.h"
#include "NetTransport.h"
#include "PlatformClient.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace OnlineFakes
{
using Json = nlohmann::json;

// What the test sees of the transport the client currently owns.
struct Socket
{
	std::string url;
	NetTransport::State state = NetTransport::State::Connecting;
	std::deque<std::string> incoming;
	std::vector<std::string> sent;
	bool closed = false;
	std::string error;

	// Sent frames decoded as JSON, in order.
	std::vector<Json> requests() const
	{
		std::vector<Json> out;
		for (const auto &text : sent)
			out.push_back(Json::parse(text));
		return out;
	}
	Json last() const
	{
		return Json::parse(sent.back());
	}
	// The last request with this method.
	Json find(const std::string &method) const
	{
		for (auto i = sent.rbegin(); i != sent.rend(); ++i)
		{
			auto request = Json::parse(*i);
			if (request["method"] == method)
				return request;
		}
		return Json();
	}
	void respond(const Json &request, const Json &result)
	{
		incoming.push_back(
			Json{{"type", "response"}, {"id", request["id"]}, {"ok", true}, {"result", result}}.dump());
	}
	void fail(const Json &request, const std::string &code)
	{
		incoming.push_back(Json{{"type", "response"},
								{"id", request["id"]},
								{"ok", false},
								{"error", {{"code", code}, {"message", code}}}}
							   .dump());
	}
	void event(const std::string &name, const Json &data)
	{
		incoming.push_back(Json{{"type", "event"}, {"event", name}, {"data", data}}.dump());
	}
};

class FakeTransport final : public NetTransport
{
  public:
	explicit FakeTransport(std::shared_ptr<Socket> socket) : socket(std::move(socket)) {}
	void open(const std::string &endpoint, uint16_t) override
	{
		socket->url = endpoint;
	}
	void close() override
	{
		socket->closed = true;
		socket->state = State::Closed;
	}
	State state() const override
	{
		return socket->state;
	}
	bool send(std::vector<uint8_t>) override
	{
		return false;
	}
	bool receive(std::vector<uint8_t> &) override
	{
		return false;
	}
	bool sendText(std::string text) override
	{
		if (socket->state != State::Connected)
			return false;
		socket->sent.push_back(std::move(text));
		return true;
	}
	bool receiveText(std::string &text) override
	{
		if (socket->incoming.empty())
			return false;
		text = std::move(socket->incoming.front());
		socket->incoming.pop_front();
		return true;
	}
	std::string error() const override
	{
		return socket->error;
	}

  private:
	std::shared_ptr<Socket> socket;
};

struct Exchange
{
	HttpFetch::Request request;
	HttpFetch::State state = HttpFetch::State::Pending;
	HttpFetch::Response response;
	std::string error;
	bool cancelled = false;
	Json body() const
	{
		return request.body.empty() ? Json() : Json::parse(request.body);
	}
	std::string header(const std::string &name) const
	{
		for (const auto &[key, value] : request.headers)
			if (key == name)
				return value;
		return {};
	}
	void reply(int status, const Json &json)
	{
		response.status = status;
		response.body = json.is_null() ? std::string() : json.dump();
		state = HttpFetch::State::Done;
	}
	void replyRaw(int status, std::string body)
	{
		response.status = status;
		response.body = std::move(body);
		state = HttpFetch::State::Done;
	}
};

class FakeFetch final : public HttpFetch::Fetch
{
  public:
	explicit FakeFetch(std::shared_ptr<Exchange> exchange) : exchange(std::move(exchange)) {}
	HttpFetch::State state() override
	{
		return exchange->state;
	}
	const HttpFetch::Response &response() const override
	{
		return exchange->response;
	}
	std::string error() const override
	{
		return exchange->error;
	}
	void cancel() override
	{
		exchange->cancelled = true;
		if (exchange->state == HttpFetch::State::Pending)
			exchange->state = HttpFetch::State::Cancelled;
	}

  private:
	std::shared_ptr<Exchange> exchange;
};

// Records every HTTP request; tests answer them.
struct Http
{
	std::vector<std::shared_ptr<Exchange>> exchanges;
	std::unique_ptr<HttpFetch::Fetch> start(HttpFetch::Request request)
	{
		auto exchange = std::make_shared<Exchange>();
		exchange->request = std::move(request);
		exchanges.push_back(exchange);
		return std::make_unique<FakeFetch>(exchange);
	}
	// The oldest unanswered request whose URL ends with path.
	std::shared_ptr<Exchange> pending(const std::string &path) const
	{
		for (const auto &exchange : exchanges)
			if (exchange->state == HttpFetch::State::Pending &&
				exchange->request.url.size() >= path.size() &&
				exchange->request.url.compare(exchange->request.url.size() - path.size(),
											  path.size(), path) == 0)
				return exchange;
		return nullptr;
	}
	int count(const std::string &path) const
	{
		int n = 0;
		for (const auto &exchange : exchanges)
			if (exchange->request.url.size() >= path.size() &&
				exchange->request.url.compare(exchange->request.url.size() - path.size(),
											  path.size(), path) == 0)
				++n;
		return n;
	}
};

// One fake world: sockets the client opened, HTTP, clock, browser.
struct World
{
	std::vector<std::shared_ptr<Socket>> sockets;
	Http http;
	std::int64_t now = 1000;
	std::int64_t wall = 1790000000000; // 2026-09-21
	double random = 0.0;
	std::vector<std::string> opened;
	bool openSucceeds = true;

	Online::ClientEnvironment environment()
	{
		Online::ClientEnvironment env;
		env.makeTransport = [this]
		{
			sockets.push_back(std::make_shared<Socket>());
			return std::make_unique<FakeTransport>(sockets.back());
		};
		env.startFetch = [this](HttpFetch::Request request) { return http.start(std::move(request)); };
		env.now = [this] { return now; };
		env.wallClock = [this] { return wall; };
		env.random = [this] { return random; };
		env.openUrl = [this](const std::string &url)
		{
			opened.push_back(url);
			return openSucceeds;
		};
		return env;
	}
	Socket &socket()
	{
		return *sockets.back();
	}
};

// An unsigned JWT-shaped token with iat/exp claims (the client only reads them).
inline std::string token(std::int64_t iat, std::int64_t exp, const std::string &tag = "a")
{
	auto base64url = [](const std::string &input)
	{
		static const char table[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string out;
		unsigned buffer = 0;
		int bits = 0;
		for (unsigned char c : input)
		{
			buffer = (buffer << 8) | c;
			bits += 8;
			while (bits >= 6)
			{
				bits -= 6;
				out.push_back(table[(buffer >> bits) & 63]);
			}
		}
		if (bits)
			out.push_back(table[(buffer << (6 - bits)) & 63]);
		return out;
	};
	return base64url(R"({"alg":"EdDSA","typ":"at+jwt"})") + "." +
		   base64url(Json{{"iat", iat}, {"exp", exp}, {"sub", tag}}.dump()) + ".sig";
}

inline Json account(const std::string &name = "Guest-1234", const std::string &kind = "guest")
{
	return Json{{"id", "00000000-0000-4000-8000-000000000001"},
				{"displayName", name},
				{"kind", kind},
				{"createdAt", "2026-10-01T00:00:00Z"},
				{"role", "user"},
				{"status", "active"},
				{"identities", Json::array()},
				{"entitlements", Json::array()}};
}

inline Json tokens(const std::string &refresh, std::int64_t iat = 1790000000,
				   std::int64_t lifetime = 600)
{
	return Json{{"tokenType", "Bearer"},
				{"accessToken", token(iat, iat + lifetime, refresh)},
				{"accessTokenExpiresAt", "2026-09-21T15:00:00Z"},
				{"refreshToken", refresh},
				{"refreshTokenExpiresAt", "2026-11-21T15:00:00Z"}};
}
} // namespace OnlineFakes
