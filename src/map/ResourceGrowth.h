// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "ComputeExecutor.h"
#include "MersenneTwister.h"
#include <deque>
#include <optional>

class Map;
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore

namespace ResourceGrowth
{
// Replenishment carries one signed material unit. A seed carries source variety
// and precomputed replenishment decisions for a matching destination at publication.
struct Proposal
{
	enum class Kind : Uint8 { Increment, Seed, LegacyIncrement };
	Uint32 tile = 0;
	Uint16 type = NO_RES_TYPE;
	Uint8 material = 0;
	Sint8 delta = 1;
	Uint8 variety = 0;
	Kind kind = Kind::Increment;
	Uint16 incrementMask = 0;
};
static_assert(sizeof(Proposal) == 12);
struct Batch
{
	Uint32 sourceTick = 0, dueTick = 0;
	std::vector<Proposal> proposals;
	Uint64 sampled = 0;
	bool capacityGrew = false;
};
struct Metrics
{
	Uint64 submitted = 0, published = 0, sampled = 0, proposals = 0, accepted = 0, rejected = 0;
	// Published outcomes include restored work; proposals counts only newly computed work.
	Uint64 publishedProposals = 0;
	Uint64 clamped = 0, stockAdded = 0, tilesAdded = 0, maxPending = 0, maxProposalBytes = 0;
	Uint64 capacityGrowthBatches = 0, maxProposals = 0;
	Uint64 computeNs = 0, queueNs = 0, waitNs = 0, publicationNs = 0;
};
// Pure calculation: all attempts see the same immutable input, including stocks.
void calculate(const MapState::View &view, MersenneTwister &random, Batch &output);
void apply(Map &map, const Batch &batch, Metrics &metrics);

// Lifecycle: stage -> prepare -> run -> join -> publish. Calculation uses the
// shared executor, with its zero-worker fallback. The simulation owner manages
// deadlines, accounting and publication.
// finish joins without publishing; reset drains callbacks and discards pending work.
class Pipeline
{
	struct Job
	{
		// run exclusively writes output, snapshot, error and timing fields until join.
		// Other fields are owner-managed and remain stable while work is running.
		Batch output;
		std::size_t reservedCapacity = 0; // Owner-side accounting; workers may grow the vector.
		std::optional<SimulationSnapshot::Handle> snapshot;
		ComputeExecutor::Batch work;
		Uint32 seed = 0, catalog = 0;
		Uint64 world = 0, submittedNs = 0, computeNs = 0, queueNs = 0;
		bool collected = false; // Metrics collected, not a worker-completion flag.
		std::exception_ptr error;
	};
	std::deque<std::unique_ptr<Job>> pending;
	std::vector<std::unique_ptr<Job>> spare;
	ComputeExecutor *executor = nullptr;
	std::size_t proposalReserve = 0;
	std::optional<std::pair<Uint32, Uint32>> reservation;
	static void run(void *, std::size_t);
	void join(Job &);

  public:
	unsigned delay = 8;
	Metrics metrics;
	~Pipeline() { reset(); }
	static SimulationSnapshot::Requirements requirements();
	bool needsPreparation() const { return reservation.has_value(); }
	std::size_t count() const { return pending.size(); }
	void stage(Uint32 tick, Uint32 seed);
	void prepare(const SimulationSnapshot::Handle &, ComputeExecutor &);
	void publish(Map &, Uint32 tick);
	void finish();
	void reset() noexcept;
	void setDelay(unsigned ticks);
	void save(GAGCore::OutputStream *, Uint32 tick);
	void load(GAGCore::InputStream *, Map &, Uint32 tick, int versionMinor);
	Uint32 checksum(bool heavy);
};
} // namespace ResourceGrowth
