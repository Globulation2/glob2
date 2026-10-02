// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "JwksStore.h"

#include "HttpClient.h"
#include "RelayLog.h"
#include "RelayMetrics.h"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Relay
{
namespace asio = boost::asio;

JwksStore::JwksStore(asio::any_io_executor executor, HttpClient& http, RelayMetrics& metrics,
                     std::chrono::seconds minRefresh, std::chrono::seconds refreshEvery)
	: executor(executor), http(http), metrics(metrics), minRefresh(minRefresh), refreshEvery(refreshEvery),
	  periodic(executor)
{
}

namespace
{
	std::string readFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error("Cannot read JWKS file " + path);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
}

void JwksStore::useFile(const std::string& path)
{
	file = path;
	jwksUrl.clear();
	keys = KeySet::parse(readFile(path));
	metrics.jwksKeys = static_cast<std::int64_t>(keys->size());
	++metrics.jwksRefreshOk;
	lastAttempt = std::chrono::steady_clock::now();
}

void JwksStore::useUrl(const std::string& url)
{
	if (!file.empty())
		return; // a static file wins
	jwksUrl = url;
}

asio::awaitable<bool> JwksStore::load()
{
	lastAttempt = std::chrono::steady_clock::now();
	try
	{
		std::string text;
		if (!file.empty())
			text = readFile(file);
		else if (!jwksUrl.empty())
		{
			HttpRequest request;
			request.url = jwksUrl;
			request.headers["Accept"] = "application/json";
			request.maxResponseBytes = 256 * 1024;
			const HttpResponse response = co_await http.fetch(request);
			if (!response.ok())
				throw std::runtime_error("JWKS fetch from " + jwksUrl + " failed: " +
				                         (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error));
			text = response.body;
		}
		else
			co_return false;
		auto parsed = KeySet::parse(text);
		keys = parsed;
		metrics.jwksKeys = static_cast<std::int64_t>(keys->size());
		++metrics.jwksRefreshOk;
		co_return true;
	}
	catch (const std::exception& e)
	{
		++metrics.jwksRefreshFailed;
		logLine("warning", std::string("JWKS refresh failed: ") + e.what());
	}
	co_return false;
}

asio::awaitable<bool> JwksStore::refresh()
{
	if (loading)
	{
		auto waiter = std::make_shared<asio::steady_timer>(executor, asio::steady_timer::time_point::max());
		waiters.push_back(waiter);
		boost::system::error_code ignored;
		co_await waiter->async_wait(asio::redirect_error(asio::use_awaitable, ignored));
		co_return ready();
	}
	loading = true;
	const bool ok = co_await load();
	loading = false;
	auto done = std::move(waiters);
	waiters.clear();
	for (auto& w : done)
		w->cancel();
	co_return ok;
}

asio::awaitable<void> JwksStore::refreshForUnknownKid(const std::string& kid)
{
	if (keys && keys->has(kid))
		co_return;
	if (!loading && std::chrono::steady_clock::now() - lastAttempt < minRefresh)
		co_return;
	co_await refresh();
}

asio::awaitable<void> JwksStore::run()
{
	while (!stopped)
	{
		if (!jwksUrl.empty() || !file.empty())
			co_await refresh();
		// Retry an empty key set quickly; otherwise refresh on the regular schedule.
		periodic.expires_after(ready() ? refreshEvery : std::chrono::seconds(5));
		boost::system::error_code ignored;
		co_await periodic.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
	}
}

void JwksStore::stop()
{
	stopped = true;
	periodic.cancel();
}
}
