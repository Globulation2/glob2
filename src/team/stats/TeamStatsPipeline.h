// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ComputeExecutor.h"
#include "Version.h"
#include "TeamStat.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <deque>
#include <optional>
class Game;

// The simulation owner submits and publishes batches at fixed tick boundaries.
// One executor lane serializes calculation and the cumulative measurement chain.
class TeamStatsPipeline
{
	struct Pending
	{
		Uint32 tick = 0, due = 0;
		SimulationSnapshot::Handle snapshot;
		std::vector<TeamStatsSample> samples;
		std::vector<unsigned> phases;
		std::vector<std::shared_ptr<GameplayMeasurements>> *previousFrames = nullptr;
		std::vector<std::vector<TeamSmoothedStat>> smoothingWindows;
		ComputeExecutor::Batch ticket;
	};
	ComputeExecutor &executor;
	std::exception_ptr failure;
	std::deque<std::unique_ptr<Pending>> pending;
	// A joined batch can be reused at the next submission; at most horizon + 1 live.
	std::unique_ptr<Pending> spare;
	std::optional<Uint32> lastSubmitted;
	Uint64 worldIdentity = 0;
	std::vector<unsigned> phases;
	// Owner seeds this chain before submission/loading; only the ordered worker
	// updates it while jobs are pending. Publication reads completed batch frames.
	std::vector<std::shared_ptr<GameplayMeasurements>> previousFrames;
	static void compute(void *, size_t);

  public:
	struct Metrics
	{
		Uint64 submitted = 0, published = 0;
		Uint64 peakPending = 0;
	} metrics;
	explicit TeamStatsPipeline(ComputeExecutor &);
	~TeamStatsPipeline();
	bool publishDue(Game &);
	bool matchesWorld(Uint64 identity) const { return !worldIdentity || identity == worldIdentity; }
	bool hasState() const { return lastSubmitted.has_value(); }
	bool needsSample(Uint32 tick) const { return !lastSubmitted || *lastSubmitted != tick; }
	void prepareBoundary(Game &, const SimulationSnapshot::Handle &);
	void drain(JoinReason reason = JoinReason::Teardown);
	void save(GAGCore::OutputStream *);
	void load(Game &, GAGCore::InputStream *, int versionMinor = VERSION_MINOR);
	// Full verification joins computation without advancing publication.
	Uint32 checkSum(bool heavy = false);
};
