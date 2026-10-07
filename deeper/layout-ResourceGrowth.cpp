#include "StageProbe.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceGrowth.h"
#include "Map.h"
#include "Game.h"
#include <Stream.h>
#include <bit>
#include <limits>

namespace ResourceGrowth
{
namespace
{
Uint64 now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
bool emptyAllowed(const MapState::View &v, size_t i, const ResourceProperties &p)
{
	const auto &o = v.occupancy[i];
	return (!p.blocksBuilding || o.building == 0xffff) &&
		   (!p.blocksGround || o.groundUnit == 0xffff) && (!p.blocksAir || o.airUnit == 0xffff);
}
} // namespace
void calculate(const MapState::View &v, MersenneTwister &rng, Batch &out)
{
 StageProbe::Scope stageProbe(2);
	out.proposals.clear();
	out.sampled = 0;
	if (v.resourceGrowthDisabled)
		return;
	const unsigned scarcity = 1u << v.resourceScarcityLevel;
	auto opportunities = [&](Uint32 rate)
	{ return Fertility::growthOpportunities(rate, [&] { return rng(); }); };
	auto propose = [&](size_t i, const Resource &source, bool spread)
	{
		if (!MapState::resourcesMayGrow(v, i) ||
			!MapState::terrainSupportsResource(v, i, static_cast<ResourceId>(source.type)))
			return;
		const auto &target = v.resources[i];
		const auto &p = v.resourceProperties(source.type);
		if (target.resource.type != NO_RES_TYPE && target.resource.type != source.type)
			return;
		if (target.resource.type == NO_RES_TYPE && !emptyAllowed(v, i, p))
			return;
		Proposal proposal;
		proposal.tile = i;
		proposal.type = source.type;
		proposal.variety = source.variety;
		proposal.spread = spread;
		proposal.incarnation = v.resourceIncarnations[i];
		const auto &yields = v.resourceRegistry->yields(static_cast<ResourceId>(source.type));
		for (unsigned mask = p.materialMask; mask; mask &= mask - 1)
		{
			const auto m = std::countr_zero(mask);
			// Empty destinations also need increments if another seed arrives first.
			if (target.resource.type == NO_RES_TYPE ||
				MapState::materialAmountAt(v, i, m) < yields[m].capacity)
				if (opportunities(yields[m].growthRate))
					proposal.incrementMask |= MaterialMask(1u << m);
		}
		if (proposal.incrementMask || target.resource.type == NO_RES_TYPE)
			out.proposals.push_back(proposal);
	};
	constexpr int directions[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0},
									  {1, 1},   {0, 1},  {-1, 1}, {-1, 0}};
	for (int y = int(rng() & 3); y < v.height; y += 4)
		for (int x = int(rng() & 15); x < v.width; x += int(rng() & 31))
		{
			++out.sampled;
			const auto i = v.index(x, y);
			const auto &r = v.resources[i].resource;
			if (r.type == NO_RES_TYPE)
				continue;
			const auto &p = v.resourceProperties(r.type);
			const auto attempts = opportunities(MapState::resourceGrowthRate(v, i, r.type));
			for (unsigned attempt = 0; attempt < attempts; ++attempt)
			{
				if (scarcity != 1 && rng() % scarcity)
					continue;
				const bool local =
					!p.stockDependentGrowth || r.amount <= rng() % p.stockBranchDivisor;
				if (local)
					propose(i, r, false);
				if (p.spreadRate && (!p.stockDependentGrowth || !local))
				{
					const auto spreads = opportunities(p.spreadRate);
					for (unsigned n = 0; n < spreads; ++n)
					{
						const auto &d = directions[rng() & 7];
						propose(v.index(x + d[0], y + d[1]), r, true);
					}
				}
			}
		}
}

void apply(Map &map, const Batch &batch, Metrics &metrics)
{
 StageProbe::Scope stageProbe(3);
	const auto start = now();
	if (batch.proposals.empty())
		return;
	if (map.game->gameHeader.isResourceGrowthDisabled())
	{
		metrics.rejected += batch.proposals.size();
		metrics.publicationNs += now() - start;
		return;
	}
	const auto v = map.cellView();
	map.rebuildGrowthCoverage();
	for (const auto &p : batch.proposals)
	{
		auto reject = [&] { ++metrics.rejected; };
		if (map.game->gameHeader.isResourceGrowthDisabled() || p.tile >= v.resources.size() ||
			!MapState::resourcesMayGrow(v, p.tile) ||
			!MapState::terrainSupportsResource(v, p.tile, static_cast<ResourceId>(p.type)))
		{
			reject();
			continue;
		}
		const auto &cell = v.resources[p.tile];
		const auto oldType = cell.resource.type;
		if ((!p.spread && (v.resourceIncarnations[p.tile] != p.incarnation || oldType != p.type)) ||
			(oldType != NO_RES_TYPE && oldType != p.type))
		{
			reject();
			continue;
		}
		const auto &properties = v.resourceProperties(p.type);
		if (oldType == NO_RES_TYPE && !emptyAllowed(v, p.tile, properties))
		{
			reject();
			continue;
		}
		const auto before = map.materialStocksAt(p.tile);
		if (oldType == NO_RES_TYPE)
		{
			const auto initial = v.resourceRegistry
									 ->yields(static_cast<ResourceId>(
										 p.type))[materialIndex(properties.primaryMaterial)]
									 .initial;
			map.replaceResource(p.tile, Resource{p.type, p.variety, initial, 0});
		}
		else
		{
			const auto &yields = v.resourceRegistry->yields(static_cast<ResourceId>(p.type));
			for (unsigned mask = p.incrementMask; mask; mask &= mask - 1)
			{
				const auto m = std::countr_zero(mask);
				const unsigned amount = unsigned(before[m]) + 1;
				metrics.clamped += amount > yields[m].capacity;
				map.setMaterialAmountSlot(p.tile, m,
										  std::min<unsigned>(amount, yields[m].capacity));
			}
		}
		const auto after = map.materialStocksAt(p.tile);
		if (after == before && oldType == v.resources[p.tile].resource.type)
		{
			reject();
			continue;
		}
		++metrics.accepted;
		metrics.tilesAdded += oldType == NO_RES_TYPE;
		for (unsigned m = 0; m < MaterialCount; ++m)
			metrics.stockAdded += after[m] - before[m];
		map.recordNaturalGrowth(p.tile % v.width, p.tile / v.width, p.type, oldType, before);
	}
	metrics.publicationNs += now() - start;
}

SimulationSnapshot::Requirements Pipeline::requirements()
{
	using namespace SimulationSnapshot;
	return bit(Component::Resources) | bit(Component::Terrain) | bit(Component::Growth) |
		   bit(Component::Catalogs) | bit(Component::Rules) | bit(Component::Occupancy);
}
void Pipeline::stage(Uint32 tick, Uint32 seed)
{
	if (reservation)
		throw std::logic_error("Unprepared resource growth reservation");
	if (!pending.empty() && Sint32(tick - pending.back()->output.sourceTick) <= 0)
		throw std::logic_error("Resource growth tick did not advance");
	reservation = std::pair{tick, seed};
}
void Pipeline::run(void *context, std::size_t)
{
	auto &j = *static_cast<Job *>(context);
	const auto start = now();
	j.queueNs = start - j.submittedNs;
	try
	{
		MersenneTwister rng(j.seed);
		calculate(j.snapshot->view(), rng, j.output);
	}
	catch (...)
	{
		j.error = std::current_exception();
	}
	j.snapshot.reset();
	j.computeNs = now() - start;
}
void Pipeline::prepare(const SimulationSnapshot::Handle &snapshot, ComputeExecutor &target)
{
 StageProbe::Scope stageProbe(4);
	if (!reservation)
		return;
	if (snapshot.tick != reservation->first)
		throw std::logic_error("Stale growth observation");
	if (pending.size() > delay)
		throw std::logic_error("Growth publication did not advance");
	executor = &target;
	auto j = spare.empty() ? std::make_unique<Job>() : std::move(spare.back());
	if (!spare.empty())
		spare.pop_back();
	j->output.sourceTick = snapshot.tick;
	j->output.dueTick = snapshot.tick + delay;
	j->seed = reservation->second;
	j->catalog = snapshot.catalogs->resources->checksum();
	j->world = snapshot.worldIdentity;
	j->snapshot = snapshot.project(requirements());
	j->error = {};
	j->collected = false;
	j->work = {};
	j->submittedNs = now();
	auto *job = j.get();
	pending.push_back(std::move(j));
	reservation.reset();
	const ComputeExecutor::Group group{1, {run, job}, ComputeExecutor::NoLane};
	try
	{
		job->work =
			executor->submit(std::span(&group, 1), shared ? ComputeExecutor::Placement::Shared
														  : ComputeExecutor::Placement::OwnerOnly);
	}
	catch (...)
	{
		job->snapshot.reset();
		job->error = std::current_exception();
		throw;
	}
	++metrics.submitted;
	metrics.maxPending = std::max<Uint64>(metrics.maxPending, pending.size());
}
void Pipeline::join(Job &j)
{
	if (!j.work.empty())
	{
		const auto start = now();
		executor->join(j.work);
		j.work = {};
		metrics.waitNs += now() - start;
	}
	if (j.error)
		std::rethrow_exception(j.error);
	if (!j.collected)
	{
		metrics.computeNs += j.computeNs;
		metrics.queueNs += j.queueNs;
		metrics.sampled += j.output.sampled;
		metrics.proposals += j.output.proposals.size();
		j.collected = true;
		Uint64 bytes = 0;
		for (const auto &p : pending)
			if (p->collected)
				bytes += p->output.proposals.capacity() * sizeof(Proposal);
		for (const auto &p : spare)
			bytes += p->output.proposals.capacity() * sizeof(Proposal);
		metrics.maxProposalBytes = std::max(metrics.maxProposalBytes, bytes);
	}
}
void Pipeline::publish(Map &map, Uint32 tick)
{
	while (!pending.empty() && Sint32(tick - pending.front()->output.dueTick) >= 0)
	{
		auto &j = *pending.front();
		join(j);
		if (j.world == map.identity() && j.catalog == map.resourceRegistry().checksum())
			apply(map, j.output, metrics);
		else
			metrics.rejected += j.output.proposals.size();
		++metrics.published;
		spare.push_back(std::move(pending.front()));
		pending.pop_front();
	}
}
void Pipeline::finish()
{
	for (auto &j : pending)
		join(*j);
}
void Pipeline::reset() noexcept
{
	for (auto &j : pending)
		if (executor && !j->work.empty())
		{
			try
			{
				executor->join(j->work);
			}
			catch (...)
			{
			}
		}
	pending.clear();
	spare.clear();
	reservation.reset();
	executor = nullptr;
	metrics = {};
	delay = 8;
	shared = true;
}
void Pipeline::configure(unsigned ticks, bool workers)
{
	if (ticks < 1 || ticks > 16 || (ticks != delay && (!pending.empty() || reservation)))
		throw std::invalid_argument("Cannot change pending resource growth delay");
	finish();
	delay = ticks;
	shared = workers;
}
void Pipeline::save(GAGCore::OutputStream *s, Uint32 tick)
{
	if (reservation)
		throw std::logic_error("Saving unprepared growth");
	finish();
	s->writeEnterSection("resourceGrowth");
	s->writeUint8(delay, "delay");
	s->writeUint8(pending.size(), "count");
	for (unsigned n = 0; n < pending.size(); ++n)
	{
		const auto &b = pending[n]->output;
		s->writeEnterSection(n);
		s->writeUint32(b.sourceTick, "sourceTick");
		s->writeUint32(pending[n]->seed, "seed");
		s->writeUint32(pending[n]->catalog, "catalog");
		s->writeUint8(b.dueTick - tick, "remaining");
		s->writeUint32(b.proposals.size(), "proposals");
		for (unsigned k = 0; k < b.proposals.size(); ++k)
		{
			const auto &p = b.proposals[k];
			s->writeEnterSection(k);
			s->writeUint32(p.tile, "tile");
			s->writeUint32(p.incarnation, "incarnation");
			s->writeUint16(p.type, "type");
			s->writeUint8(p.variety, "variety");
			s->writeUint8(p.spread, "spread");
			s->writeUint16(p.incrementMask, "incrementMask");
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
void Pipeline::load(GAGCore::InputStream *s, Map &map, Uint32 tick)
{
	reset();
	s->readEnterSection("resourceGrowth");
	delay = s->readUint8("delay");
	const auto count = s->readUint8("count");
	if (delay < 1 || delay > 16 || count > delay + 1)
		throw std::runtime_error("Invalid growth queue");
	unsigned previous = 0;
	for (unsigned n = 0; n < count; ++n)
	{
		s->readEnterSection(n);
		auto j = std::make_unique<Job>();
		auto &b = j->output;
		b.sourceTick = s->readUint32("sourceTick");
		j->seed = s->readUint32("seed");
		j->catalog = s->readUint32("catalog");
		if (j->catalog != map.resourceRegistry().checksum())
			throw std::runtime_error("Growth catalog mismatch");
		const auto remaining = s->readUint8("remaining");
		if (remaining > delay || (n && remaining <= previous) ||
			b.sourceTick + delay != tick + remaining)
			throw std::runtime_error("Invalid growth deadline");
		previous = remaining;
		b.dueTick = tick + remaining;
		j->world = map.identity();
		j->collected = true;
		const auto proposals = s->readUint32("proposals");
		if (proposals > Uint64(map.getW()) * map.getH() * 64)
			throw std::runtime_error("Excessive growth proposals");
		b.proposals.reserve(proposals);
		for (unsigned k = 0; k < proposals; ++k)
		{
			s->readEnterSection(k);
			Proposal p;
			p.tile = s->readUint32("tile");
			p.incarnation = s->readUint32("incarnation");
			p.type = s->readUint16("type");
			p.variety = s->readUint8("variety");
			const auto spread = s->readUint8("spread");
			p.spread = spread;
			if (p.tile >= Uint64(map.getW()) * map.getH() ||
				!map.resourceRegistry().valid(p.type) || spread > 1)
				throw std::runtime_error("Invalid growth proposal");
			p.incrementMask = s->readUint16("incrementMask");
			if (p.incrementMask & ~map.resourcePropertiesByIndex(p.type).materialMask)
				throw std::runtime_error("Invalid growth increment mask");
			s->readLeaveSection();
			b.proposals.push_back(p);
		}
		s->readLeaveSection();
		pending.push_back(std::move(j));
	}
	s->readLeaveSection();
}
Uint32 Pipeline::checksum(bool heavy)
{
	if (heavy)
		finish();
	Uint32 hash = 2166136261u;
	auto mix = [&](Uint64 v)
	{
		hash = (hash ^ Uint32(v)) * 16777619u;
		hash = (hash ^ Uint32(v >> 32)) * 16777619u;
	};
	mix(delay);
	mix(pending.size());
	mix(reservation.has_value());
	if (reservation)
	{
		mix(reservation->first);
		mix(reservation->second);
	}
	for (const auto &j : pending)
	{
		mix(j->output.sourceTick);
		mix(j->output.dueTick);
		mix(j->seed);
		mix(j->catalog);
		if (!heavy)
			continue;
		mix(j->output.proposals.size());
		for (const auto &p : j->output.proposals)
		{
			mix(p.tile);
			mix(p.incarnation);
			mix(p.type);
			mix(p.variety);
			mix(p.spread);
			mix(p.incrementMask);
		}
	}
	return hash;
}
} // namespace ResourceGrowth

#include "gradient/GradientRuntime.h"
#include "Utilities.h"

SimulationSnapshot::Requirements Map::pendingWorldRequirements() const
{
	return pendingGradientRequirements() |
		   (gradientRuntime->growth.needsPreparation() ? ResourceGrowth::Pipeline::requirements()
													   : 0);
}
void Map::preparePendingWorld()
{
	if (const auto requirements = pendingWorldRequirements())
	{
		auto &store = game->snapshotStore();
		store.invalidateBoundary();
		preparePendingWorld(store.captureBoundary(*game, requirements));
	}
}
void Map::preparePendingWorld(const SimulationSnapshot::Handle &snapshot)
{
	if (hasPendingGradientPreparation())
		preparePendingGradient(snapshot);
	gradientRuntime->growth.prepare(snapshot, compute);
}
void Map::stageResourceGrowth()
{
	if (!game->gameHeader.isResourceGrowthDisabled())
		gradientRuntime->growth.stage(game->stepCounter, syncRand());
}
void Map::finishResourceGrowth()
{
	gradientRuntime->growth.finish();
}
void Map::configureResourceGrowth(unsigned delay, bool shared)
{
	preparePendingWorld();
	gradientRuntime->growth.configure(delay, shared);
}
unsigned Map::resourceGrowthDelay() const
{
	return gradientRuntime->growth.delay;
}
const ResourceGrowth::Metrics &Map::resourceGrowthMetrics() const
{
	return gradientRuntime->growth.metrics;
}
