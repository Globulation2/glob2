// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "HttpFetch.h"
#include "OnlineResources.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Measures the round trip to each relay region before queue.join and
// room.create (docs/multiplayer/ratings-and-matchmaking.md, "Relay probes").
//
// It fetches GET <origin>/api/v1/relays/regions, then times `attempts`
// requests to each region's probeUrl (regions in parallel, attempts in turn).
// Any HTTP response counts. The estimate is the fastest attempt divided by
// `roundTripsPerRequest`: a native request opens a new TCP and TLS
// connection each time (three round trips), while a browser reuses its
// connection (one). Regions that never answer are left out; an empty result
// is valid (the platform then picks any relay). Poll update() from the UI
// thread until done().
namespace Online
{
class RelayProbe
{
  public:
	using FetchStarter = std::function<std::unique_ptr<HttpFetch::Fetch>(HttpFetch::Request)>;
	struct Options
	{
		int attempts = 3;
		int roundTripsPerRequest = defaultRoundTrips();
		std::int64_t requestTimeoutMs = 3000;
		static int defaultRoundTrips();
	};
	RelayProbe(std::string origin, FetchStarter startFetch, std::function<std::int64_t()> now,
			   Options options);
	RelayProbe(std::string origin, FetchStarter startFetch, std::function<std::int64_t()> now);
	// HttpFetch::start and a steady clock.
	static std::unique_ptr<RelayProbe> native(const std::string &origin);

	void update();
	bool done() const { return finished; }
	// Measured regions, fastest first.
	const std::vector<RegionRtt> &results() const { return measured; }
	// Why the region list could not be read (results are then empty).
	const std::string &error() const { return problem; }

  private:
	struct Target
	{
		RelayRegionInfo region;
		std::unique_ptr<HttpFetch::Fetch> fetch;
		std::int64_t startedAt = 0;
		int attempts = 0;
		std::optional<std::int64_t> best;
		bool over = false;
	};
	void startAttempt(Target &target);
	void finish();

	std::string origin;
	FetchStarter startFetch;
	std::function<std::int64_t()> now;
	Options options;
	std::unique_ptr<HttpFetch::Fetch> listing;
	std::vector<Target> targets;
	std::vector<RegionRtt> measured;
	std::string problem;
	bool finished = false;
};
} // namespace Online
