// SPDX-License-Identifier: GPL-3.0-or-later
#include "RelayProbe.h"

#include "InstanceConfig.h"

#include <algorithm>
#include <chrono>

namespace Online
{
int RelayProbe::Options::defaultRoundTrips()
{
#ifdef __EMSCRIPTEN__
	return 1;
#else
	return 3;
#endif
}

RelayProbe::RelayProbe(std::string origin, FetchStarter startFetch, std::function<std::int64_t()> now,
					   Options options)
	: origin(std::move(origin)), startFetch(std::move(startFetch)), now(std::move(now)),
	  options(options)
{
	HttpFetch::Request request;
	request.url = apiUrl(this->origin, "/api/v1/relays/regions");
	request.headers.emplace_back("Accept", "application/json");
	request.timeout = std::chrono::milliseconds(this->options.requestTimeoutMs * 2);
	request.responseLimit = 64 * 1024;
	listing = this->startFetch(std::move(request));
}

RelayProbe::RelayProbe(std::string origin, FetchStarter startFetch, std::function<std::int64_t()> now)
	: RelayProbe(std::move(origin), std::move(startFetch), std::move(now), Options())
{
}

void RelayProbe::startAttempt(Target &target)
{
	HttpFetch::Request request;
	request.url = target.region.probeUrl;
	request.timeout = std::chrono::milliseconds(options.requestTimeoutMs);
	request.responseLimit = 4096;
	target.attempts++;
	target.startedAt = now();
	try
	{
		HttpFetch::parseUrl(request.url);
	}
	catch (const std::exception &)
	{
		target.over = true;
		return;
	}
	target.fetch = startFetch(std::move(request));
}

void RelayProbe::update()
{
	if (finished)
		return;
	if (listing)
	{
		const auto state = listing->state();
		if (state == HttpFetch::State::Pending)
			return;
		if (state == HttpFetch::State::Done && listing->response().status / 100 == 2)
		{
			const Json body = Json::parse(listing->response().body, nullptr, false);
			for (const auto &region : parseRelayRegions(body))
				targets.push_back(Target{region});
		}
		else
			problem = state == HttpFetch::State::Done
						  ? "HTTP " + std::to_string(listing->response().status)
						  : listing->error();
		listing.reset();
		for (auto &target : targets)
			startAttempt(target);
	}
	bool pending = false;
	for (auto &target : targets)
	{
		if (target.over)
			continue;
		if (target.fetch)
		{
			const auto state = target.fetch->state();
			if (state == HttpFetch::State::Pending)
			{
				pending = true;
				continue;
			}
			if (state == HttpFetch::State::Done)
			{
				const std::int64_t elapsed = std::max<std::int64_t>(0, now() - target.startedAt);
				target.best = target.best ? std::min(*target.best, elapsed) : elapsed;
			}
			target.fetch.reset();
		}
		if (target.attempts < options.attempts)
		{
			startAttempt(target);
			pending = pending || !target.over;
		}
		else
			target.over = true;
	}
	if (!pending)
		finish();
}

void RelayProbe::finish()
{
	finished = true;
	measured.clear();
	const int divisor = std::max(1, options.roundTripsPerRequest);
	for (const auto &target : targets)
		if (target.best)
			measured.push_back({target.region.region,
								static_cast<int>(std::min<std::int64_t>(60000, (*target.best + divisor / 2) / divisor))});
	std::stable_sort(measured.begin(), measured.end(),
					 [](const RegionRtt &a, const RegionRtt &b) { return a.rttMs < b.rttMs; });
}
} // namespace Online
