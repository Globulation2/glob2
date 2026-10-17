// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiringPlanner.h"
#include "ComputeExecutor.h"
#include "Version.h"
#include <deque>
#include <optional>
class Game;
class Building;
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore

// Owner-managed continuation state. Workers rank and prepare routes from immutable observations;
// buildings validate and commit suggestions against current live state.
class HiringPipeline
{
	friend class HiringPipelineHarness;
	struct Work;
	struct Batch;
	ComputeExecutor &executor;
	Game *game = nullptr;
	std::optional<Uint32> lastObserved;
	std::array<std::shared_ptr<const HiringPlanner::Output>, 16> published;
	std::deque<std::unique_ptr<Batch>> pending;
	static void compute(void *, std::size_t);

  public:
	// Minimal submission/publication status.
	struct Metrics
	{
		Uint64 submitted = 0, published = 0;
	};
	Metrics metrics;
	static constexpr unsigned Delay = Hiring::PublicationDelay, Refresh = Hiring::RefreshInterval,
							  Extras = Hiring::CandidateExtras, Capacity = 128;
	static HiringPlanner::Inputs bindRoutes(Game &, const SimulationSnapshot::Handle &,
											unsigned teamMask = ~0u);
	explicit HiringPipeline(ComputeExecutor &);
	~HiringPipeline();
	void observe(Game &, const SimulationSnapshot::Handle &);
	void publishDue(Uint32 tick);
	void drain(JoinReason = JoinReason::Teardown);
	const HiringPlanner::BuildingLists *suggestions(const Building &) const;
	void save(GAGCore::OutputStream *);
	void load(Game &, GAGCore::InputStream *, int versionMinor = VERSION_MINOR);
	Uint32 checkSum() const;
};
