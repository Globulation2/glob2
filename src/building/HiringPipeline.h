// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiringPlanner.h"
#include "ComputeExecutor.h"
#include <deque>
#include <optional>
class Game;
class Building;
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore

// Owner-managed continuation state. Workers only rank immutable observations;
// buildings validate and commit suggestions against current live state.
class HiringPipeline
{
	friend class HiringPipelineHarness;
	struct Work;
	struct Batch;
	ComputeExecutor &executor;
	std::optional<Uint32> lastObserved;
	std::array<std::shared_ptr<const HiringPlanner::Output>, 16> published;
	std::array<Uint64, 16> publishedBounds{};
	std::deque<std::unique_ptr<Batch>> pending;
	mutable std::optional<Uint32> cachedChecksum;
	std::exception_ptr failure;
	static void compute(void *, std::size_t);
	static Uint32 checksumOutput(const HiringPlanner::Output &);

  public:
	struct Metrics
	{
		Uint64 observations = 0, submitted = 0, published = 0, routeHandlesNs = 0,
			   preparationNs = 0, submissionNs = 0, publicationNs = 0, deadlineWaitNs = 0,
			   plannerNs = 0;
		HiringPlanner::Metrics planner;
	};
	mutable Metrics metrics;
	static constexpr unsigned Delay = Hiring::PublicationDelay, Refresh = Hiring::RefreshInterval,
							  Extras = Hiring::CandidateExtras, Capacity = 128;
	static constexpr std::size_t MaxOutputBytes = 96u * 1024 * 1024;
	std::size_t outputReservation() const;
	static HiringPlanner::Inputs bindRoutes(Game &, const SimulationSnapshot::Handle &,
											unsigned teamMask = ~0u);
	explicit HiringPipeline(ComputeExecutor &);
	~HiringPipeline();
	void observe(Game &, const SimulationSnapshot::Handle &);
	void publishDue(Uint32 tick);
	void drain(JoinReason = JoinReason::Teardown);
	const HiringPlanner::BuildingLists *suggestions(const Building &) const;
	void save(GAGCore::OutputStream *);
	void load(Game &, GAGCore::InputStream *);
	Uint32 checkSum() const;
};
