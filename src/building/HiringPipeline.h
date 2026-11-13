// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiringPlanner.h"
#include "ComputeExecutor.h"
#include <deque>
#include <optional>
#include <map>
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
	// Replaced publications, released on the next batch's worker rather than
	// destroyed on the owner. They are never read again.
	std::array<std::shared_ptr<const HiringPlanner::Output>, 16> retired;
	std::deque<std::unique_ptr<Batch>> pending;
	std::map<Uint16, BuildingRef> initializationRequests;
	struct Claim
	{
		BuildingRef building;
		int slot;
		Uint32 epoch, generation;
	};
	std::vector<Claim> initializing;
	static void compute(void *, std::size_t);

  public:
	// Minimal submission/publication status.
	struct Metrics
	{
		Uint64 submitted = 0, published = 0;
	};
	Metrics metrics;
	// Rules for new games; a loaded save keeps the rules it was saved with.
	static constexpr unsigned Delay = Hiring::PublicationDelay, Refresh = Hiring::RefreshInterval,
							  Extras = Hiring::CandidateExtras, Capacity = 128;
	static_assert(Hiring::MaxPublicationDelay <= ComputeExecutor::HiringHorizon);
	const Hiring::Rules &rules() const { return activeRules; }
	// Owner-only recycling of cold routes that no worker initialized. Reuse
	// saves their allocations; a route is reclaimed only once the owner holds
	// its last reference, so no worker can still observe it.
	struct RoutePool
	{
		std::vector<std::shared_ptr<HiringPlanner::PreparedRoute>> issued, spare;
		std::size_t lastRoutes = 0;
		void reclaim();
		std::shared_ptr<HiringPlanner::PreparedRoute> take();
	};
	static HiringPlanner::Inputs
	bindRoutes(Game &, const SimulationSnapshot::Handle &, unsigned teamMask = ~0u,
			   const std::map<Uint16, BuildingRef> *initializations = nullptr,
			   RoutePool *pool = nullptr, Hiring::Rules rules = {});
	explicit HiringPipeline(ComputeExecutor &);
	~HiringPipeline();
	// Legacy saved continuation support; new buildings initialize on demand.
	// Tests enqueue these requests explicitly to exercise old-save continuation.
	void requestInitialization(Building &);
	bool preparing(const Building &, int slot) const;
	void observe(Game &, const SimulationSnapshot::Handle &);
	void publishDue(Uint32 tick);
	void drain(JoinReason = JoinReason::Teardown);
	const HiringPlanner::BuildingLists *suggestions(const Building &) const;
	void save(GAGCore::OutputStream *);
	void load(Game &, GAGCore::InputStream *); // current format
	void load(Game &, GAGCore::InputStream *, int versionMinor);
	Uint32 checkSum() const;

  private:
	RoutePool routePool;
	Hiring::Rules activeRules;
};
