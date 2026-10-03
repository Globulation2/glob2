// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "PlatformLink.h"

#include "HttpClient.h"
#include "RelayLog.h"
#include "RelayMetrics.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace Relay
{
namespace asio = boost::asio;
using json = nlohmann::json;

PlatformLink::PlatformLink(asio::any_io_executor executor, const RelayConfig& config, HttpClient& http,
                           RelayMetrics& metrics, std::function<LoadSnapshot()> snapshot)
	: executor(executor), config(config), http(http), metrics(metrics), snapshot(std::move(snapshot)), wake(executor)
{
}

std::map<std::string, std::string> PlatformLink::authHeaders(const std::string& contentType) const
{
	std::map<std::string, std::string> h;
	h["Authorization"] = "Bearer " + config.relayKey;
	h["Accept"] = "application/json";
	if (!contentType.empty())
		h["Content-Type"] = contentType;
	return h;
}

namespace
{
	std::string describe(const HttpResponse& r)
	{
		if (!r.error.empty())
			return r.error;
		return "HTTP " + std::to_string(r.status) + (r.body.empty() ? "" : ": " + r.body.substr(0, 200));
	}
}

asio::awaitable<bool> PlatformLink::registerRelay()
{
	const LoadSnapshot s = snapshot();
	RegistrationInfo info;
	info.relayId = config.relayId;
	info.publicUrl = config.publicUrl;
	info.region = config.region;
	info.build = build;
	info.maxMatches = config.maxMatches;
	HttpRequest request;
	request.method = "POST";
	request.url = url("/internal/v1/relays/register");
	request.headers = authHeaders("application/json");
	request.body = registrationJson(info, s.load, s.draining);
	const HttpResponse response = co_await http.fetch(request);
	if (!response.ok())
	{
		++metrics.registrationsFailed;
		logLine("warning", "Platform registration failed: " + describe(response));
		co_return false;
	}
	const json body = json::parse(response.body, nullptr, false);
	if (body.is_discarded() || !body.is_object())
	{
		++metrics.registrationsFailed;
		logLine("warning", "Platform registration response is not a JSON object");
		co_return false;
	}
	if (body.contains("heartbeatIntervalSeconds") && body["heartbeatIntervalSeconds"].is_number_integer())
		heartbeatInterval = std::chrono::seconds(std::clamp<long long>(body["heartbeatIntervalSeconds"].get<long long>(), 1, 3600));
	if (body.contains("jwksUrl") && body["jwksUrl"].is_string() && onJwksUrl)
		onJwksUrl(body["jwksUrl"].get<std::string>());
	++metrics.registrationsOk;
	metrics.registered = true;
	logLine("info", "Registered with the platform as " + config.relayId + "; heartbeat every " +
	                    std::to_string(heartbeatInterval.count()) + " s");
	co_return true;
}

asio::awaitable<bool> PlatformLink::heartbeat(bool& reregister)
{
	reregister = false;
	const LoadSnapshot s = snapshot();
	HttpRequest request;
	request.method = "POST";
	request.url = url("/internal/v1/relays/heartbeat");
	request.headers = authHeaders("application/json");
	request.body = heartbeatJson(config.relayId, s.load, s.draining, s.activeMatchIds);
	const HttpResponse response = co_await http.fetch(request);
	if (response.status == 404)
	{
		// An unknown relay: the platform forgot us (restart, expiry).
		reregister = true;
		++metrics.heartbeatsFailed;
		co_return false;
	}
	if (!response.ok())
	{
		++metrics.heartbeatsFailed;
		logLine("warning", "Heartbeat failed: " + describe(response));
		co_return false;
	}
	const json body = json::parse(response.body, nullptr, false);
	if (!body.is_discarded() && body.is_object() && body.value("reregister", false))
		reregister = true;
	++metrics.heartbeatsOk;
	co_return true;
}

asio::awaitable<void> PlatformLink::run()
{
	if (!enabled())
		co_return;
	bool registered = false;
	std::chrono::seconds backoff(1);
	while (!stopped)
	{
		std::chrono::steady_clock::duration wait{};
		if (!registered)
		{
			registered = co_await registerRelay();
			if (!registered)
			{
				metrics.registered = false;
				wait = backoff;
				backoff = std::min(backoff * 2, std::chrono::seconds(30));
			}
			else
			{
				backoff = std::chrono::seconds(1);
				wait = heartbeatInterval; // as the registration response set it
			}
		}
		else
		{
			bool reregister = false;
			co_await heartbeat(reregister);
			wait = heartbeatInterval;
			if (reregister)
			{
				registered = false;
				metrics.registered = false;
				wait = std::chrono::seconds(0);
			}
		}
		if (stopped)
			break;
		wake.expires_after(wait);
		boost::system::error_code ignored;
		co_await wake.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
	}
}

void PlatformLink::poke()
{
	wake.cancel();
}

void PlatformLink::stop()
{
	stopped = true;
	wake.cancel();
}

asio::awaitable<std::optional<std::string>> PlatformLink::fetchSetup(const std::string& matchId,
                                                                     std::chrono::steady_clock::time_point giveUpAt,
                                                                     std::function<bool()> wanted)
{
	std::chrono::seconds backoff(1);
	for (unsigned attempt = 1;; ++attempt)
	{
		if (auto setup = co_await fetchSetupOnce(matchId))
		{
			if (attempt > 1)
				logLine("info", "Setup of match " + matchId + " arrived on attempt " + std::to_string(attempt));
			co_return setup;
		}
		auto limit = giveUpAt;
		if (shutdownDeadline)
			limit = std::min(limit, *shutdownDeadline);
		if (stopped || (wanted && !wanted()) || std::chrono::steady_clock::now() + backoff > limit)
			co_return std::nullopt;
		asio::steady_timer timer(executor, backoff);
		boost::system::error_code ignored;
		co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
		backoff = std::min(backoff * 2, std::chrono::seconds(30));
		if (stopped || (wanted && !wanted()))
			co_return std::nullopt;
	}
}

asio::awaitable<std::optional<std::string>> PlatformLink::fetchSetupOnce(const std::string& matchId)
{
	if (!enabled())
		co_return std::nullopt;
	HttpRequest request;
	request.url = url("/internal/v1/matches/" + matchId + "/setup");
	request.headers = authHeaders("");
	request.maxResponseBytes = Turn::MatchRecord::MAX_SETUP_BYTES;
	const HttpResponse response = co_await http.fetch(request);
	if (!response.ok())
	{
		logLine("warning", "Setup lookup for match " + matchId + " failed: " + describe(response));
		co_return std::nullopt;
	}
	co_return response.body;
}

std::string PlatformLink::spoolPath(const std::string& matchId, const char* suffix) const
{
	return (std::filesystem::path(config.spoolDirectory) / (matchId + suffix)).string();
}

void PlatformLink::submit(FinishedMatch match)
{
	if (!config.spoolDirectory.empty())
	{
		try
		{
			std::filesystem::create_directories(config.spoolDirectory);
			// The report is written last: a spool entry counts only once both exist.
			const std::string recordPath = spoolPath(match.matchId, ".g2mr");
			{
				std::ofstream out(recordPath + ".tmp", std::ios::binary | std::ios::trunc);
				out.write(reinterpret_cast<const char*>(match.record.data()), static_cast<std::streamsize>(match.record.size()));
				if (!out)
					throw std::runtime_error("cannot write " + recordPath);
			}
			std::filesystem::rename(recordPath + ".tmp", recordPath);
			const std::string endPath = spoolPath(match.matchId, ".end.json");
			{
				std::ofstream out(endPath + ".tmp", std::ios::binary | std::ios::trunc);
				out << match.endedJson;
				if (!out)
					throw std::runtime_error("cannot write " + endPath);
			}
			std::filesystem::rename(endPath + ".tmp", endPath);
		}
		catch (const std::exception& e)
		{
			logLine("error", "Could not spool match " + match.matchId + ": " + e.what());
		}
	}
	if (!enabled())
	{
		logLine("info", "Match " + match.matchId + " ended; no platform configured" +
		                    (config.spoolDirectory.empty() ? ", record discarded" : ", record kept in the spool"));
		return;
	}
	++pending;
	metrics.pendingUploads = static_cast<std::int64_t>(pending);
	asio::co_spawn(executor, upload(std::move(match)), asio::detached);
}

void PlatformLink::resubmitSpooled()
{
	if (config.spoolDirectory.empty() || !enabled())
		return;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(config.spoolDirectory, ec))
	{
		const std::string name = entry.path().filename().string();
		const std::string suffix = ".end.json";
		if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
			continue;
		const std::string matchId = name.substr(0, name.size() - suffix.size());
		std::ifstream recordIn(spoolPath(matchId, ".g2mr"), std::ios::binary);
		std::ifstream endIn(entry.path(), std::ios::binary);
		if (!recordIn || !endIn)
			continue;
		FinishedMatch match;
		match.matchId = matchId;
		match.record.assign(std::istreambuf_iterator<char>(recordIn), std::istreambuf_iterator<char>());
		match.endedJson.assign(std::istreambuf_iterator<char>(endIn), std::istreambuf_iterator<char>());
		logLine("info", "Retrying spooled upload of match " + matchId);
		++pending;
		metrics.pendingUploads = static_cast<std::int64_t>(pending);
		asio::co_spawn(executor, upload(std::move(match)), asio::detached);
	}
}

asio::awaitable<bool> PlatformLink::uploadOnce(const FinishedMatch& match)
{
	HttpRequest put;
	put.method = "PUT";
	put.url = url("/internal/v1/matches/" + match.matchId + "/record");
	put.headers = authHeaders("application/vnd.glob2.match-record");
	put.body.assign(match.record.begin(), match.record.end());
	put.timeout = std::chrono::seconds(60);
	const HttpResponse stored = co_await http.fetch(put);
	if (!stored.ok())
	{
		logLine("warning", "Record upload for match " + match.matchId + " failed: " + describe(stored));
		co_return false;
	}
	HttpRequest end;
	end.method = "POST";
	end.url = url("/internal/v1/matches/" + match.matchId + "/end");
	end.headers = authHeaders("application/json");
	end.body = match.endedJson;
	const HttpResponse ended = co_await http.fetch(end);
	if (!ended.ok())
	{
		logLine("warning", "Match end report for " + match.matchId + " failed: " + describe(ended));
		co_return false;
	}
	co_return true;
}

asio::awaitable<void> PlatformLink::upload(FinishedMatch match)
{
	std::chrono::seconds backoff(1);
	bool done = false;
	for (unsigned attempt = 0; attempt < config.uploadAttempts && !done; ++attempt)
	{
		if (attempt > 0)
		{
			if (shutdownDeadline && std::chrono::steady_clock::now() + backoff > *shutdownDeadline)
				break;
			asio::steady_timer timer(executor, backoff);
			boost::system::error_code ignored;
			co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
			backoff = std::min(backoff * 2, std::chrono::seconds(60));
		}
		done = co_await uploadOnce(match);
		if (!done)
			++metrics.uploadsFailed;
	}
	if (done)
	{
		++metrics.uploadsOk;
		logLine("info", "Uploaded the record of match " + match.matchId + " (" + std::to_string(match.record.size()) + " bytes)");
		if (!config.spoolDirectory.empty())
		{
			std::error_code ec;
			std::filesystem::remove(spoolPath(match.matchId, ".end.json"), ec);
			std::filesystem::remove(spoolPath(match.matchId, ".g2mr"), ec);
		}
	}
	else
		logLine("error", "Gave up uploading match " + match.matchId +
		                     (config.spoolDirectory.empty() ? "; the record is lost" : "; it stays in the spool"));
	--pending;
	metrics.pendingUploads = static_cast<std::int64_t>(pending);
}
}
