// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceGrowth.h"
#include "Map.h"
#include "Game.h"
#include "Team.h"
#include <Stream.h>
#include <bit>
#include <limits>
#include "FileFormatVersions.h"

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
// Publication already knows the one changed material and its exact delta.
// Avoid constructing and comparing all material stocks for each unit operation.
void recordDelta(Map &map, const Proposal &p, bool newTile)
{
	const int x = p.tile % map.getW(), y = p.tile / map.getW();
	std::array<Uint32, GROWTH_COVERAGE_BANDS> coverage;
	for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
		coverage[band] = map.teamsWithBuildingsNear(x, y, band);
	for (int t = 0; t < map.game->mapHeader.getNumberOfTeams(); ++t)
	{
		auto *team = map.game->teams[t];
		if (!team)
			continue;
		auto &m = team->stats.measurements;
		m.growthGlobal[0][p.material] += newTile;
		m.growthGlobal[1][p.material] += p.delta > 0;
		m.growthGlobal[2][p.material] += p.delta < 0;
		for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
			if (coverage[band] & (Uint32(1) << t))
			{
				m.growthTiles[band][p.material] += newTile;
				m.growthAmount[band][p.material] += p.delta > 0;
				m.growthReduction[band][p.material] += p.delta < 0;
			}
	}
}
} // namespace
void calculate(const MapState::View &v, MersenneTwister &rng, Batch &out)
{
	out.proposals.clear();
	out.sampled = 0;
	out.capacityGrew = false;
	const auto initialCapacity = out.proposals.capacity();
	if (v.resourceGrowthDisabled)
		return;
	const unsigned scarcity = 1u << v.resourceScarcityLevel;
	auto opportunities = [&](Uint32 rate)
	{ return Fertility::growthOpportunities(rate, [&] { return rng(); }); };
	auto propose = [&](size_t i, const Resource &source)
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
		const auto &yields = v.resourceRegistry->yields(static_cast<ResourceId>(source.type));
		if (target.resource.type == NO_RES_TYPE)
        {
            out.proposals.push_back({Uint32(i), source.type, 255, 1});
            return;
        }
		for (unsigned mask = p.materialMask; mask; mask &= mask - 1)
		{
			const auto m = std::countr_zero(mask);
			if ((target.resource.type == NO_RES_TYPE ||
				 MapState::materialAmountAt(v, i, m) < yields[m].capacity) &&
				opportunities(yields[m].growthRate))
				out.proposals.push_back({Uint32(i), source.type, Uint8(m), 1});
		}

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
					propose(i, r);
				if (p.spreadRate && (!p.stockDependentGrowth || !local))
				{
					const auto spreads = opportunities(p.spreadRate);
					for (unsigned n = 0; n < spreads; ++n)
					{
						const auto &d = directions[rng() & 7];
						propose(v.index(x + d[0], y + d[1]), r);
					}
				}
			}
		}
	out.capacityGrew = out.proposals.capacity() != initialCapacity;
}

void apply(Map &map, const Batch &batch, Metrics &metrics)
{
    const auto start = now();
    metrics.publishedProposals += batch.proposals.size();
    if (map.game->gameHeader.isResourceGrowthDisabled())
    {
        metrics.rejected += batch.proposals.size();
        metrics.publicationNs += now() - start;
        return;
    }
    map.rebuildGrowthCoverage();
    const auto v = map.cellView();
    for (const auto &op : batch.proposals)
    {
        const auto oldType = v.resources[op.tile].resource.type;
        if (oldType != op.type && (oldType != NO_RES_TYPE || op.delta < 0))
        {
            ++metrics.rejected;
            continue;
        }
        const auto &properties = map.resourcePropertiesByIndex(op.type);
        if (oldType == NO_RES_TYPE)
        {
            // Any positive operation creating a deposit, including a delayed
            // replenishment after removal, starts every configured material at one.
            std::array<Uint16, MaterialCount> stocks{};
            for (unsigned mask = properties.materialMask; mask; mask &= mask - 1)
                stocks[std::countr_zero(mask)] = 1;
            map.replaceResource(op.tile, Resource{op.type, 0, 1, 0}, &stocks);
            bool first = true;
            for (unsigned mask = properties.materialMask; mask; mask &= mask - 1)
            {
                Proposal unit{op.tile, op.type, Uint8(std::countr_zero(mask)), 1};
                recordDelta(map, unit, first);
                first = false;
                ++metrics.stockAdded;
            }
            ++metrics.tilesAdded;
            ++metrics.accepted;
            continue;
        }
        // 255 is an experimental seed marker, still in the existing eight bytes.
        // A seed that encounters a matching deposit adds one per material, with
        // independent caps. Other operations replenish only their named material.
        unsigned mask = op.material == 255 ? properties.materialMask : (1u << op.material);
        bool changed = false;
        for (; mask; mask &= mask - 1)
        {
            const auto material = std::countr_zero(mask);
            const auto capacity = v.resourceRegistry->yields(static_cast<ResourceId>(op.type))[material].capacity;
            const auto amount = map.materialAmountAtSlot(op.tile, material);
            if ((op.delta > 0 && amount >= capacity) || (op.delta < 0 && amount == 0)) continue;
            map.setMaterialAmountSlot(op.tile, material, amount + op.delta);
            Proposal unit{op.tile, op.type, Uint8(material), op.delta};
            recordDelta(map, unit, false);
            metrics.stockAdded += op.delta > 0;
            changed = true;
        }
        if (changed) ++metrics.accepted;
        else { ++metrics.rejected; metrics.clamped += op.delta > 0; }
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
	if (!reservation)
		return;
	if (snapshot.tick != reservation->first)
		throw std::logic_error("Stale growth observation");
	if (pending.size() > delay)
		throw std::logic_error("Growth publication did not advance");
	executor = &target;
	// Roughly four proposals per expected sampled cell (sampling averages 1/62
	// of the map). This is a headroom heuristic, not a proven percentile bound.
	// Warm every slot once; observed high-water demand then raises the reservation.
	if (!proposalReserve)
	{
		proposalReserve = std::max<std::size_t>(128, snapshot.view().resources.size() / 16);
		spare.reserve(17);
	}
	while (spare.size() + pending.size() < delay + 1)
	{
		auto job = std::make_unique<Job>();
		job->output.proposals.reserve(proposalReserve);
		job->reservedCapacity = job->output.proposals.capacity();
		spare.push_back(std::move(job));
	}
	auto j = spare.empty() ? std::make_unique<Job>() : std::move(spare.back());
	if (!spare.empty())
		spare.pop_back();
	j->output.proposals.reserve(proposalReserve);
	j->reservedCapacity = j->output.proposals.capacity();
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
	Uint64 bytes = 0;
	for (const auto &p : pending)
		bytes += p->reservedCapacity * sizeof(Proposal);
	for (const auto &p : spare)
		bytes += p->reservedCapacity * sizeof(Proposal);
	metrics.maxProposalBytes = std::max(metrics.maxProposalBytes, bytes);
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
		metrics.capacityGrowthBatches += j.output.capacityGrew;
		metrics.maxProposals = std::max<Uint64>(metrics.maxProposals, j.output.proposals.size());
		proposalReserve =
			std::max(proposalReserve, j.output.proposals.size() + j.output.proposals.size() / 4);
		metrics.proposals += j.output.proposals.size();
		j.collected = true;
		j.reservedCapacity = j.output.proposals.capacity();
		Uint64 bytes = 0;
		for (const auto &p : pending)
			bytes += p->reservedCapacity * sizeof(Proposal);
		for (const auto &p : spare)
			bytes += p->reservedCapacity * sizeof(Proposal);
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
		{
			metrics.publishedProposals += j.output.proposals.size();
			metrics.rejected += j.output.proposals.size();
		}
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
	proposalReserve = 0;
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
			s->writeUint16(p.type, "type");
			s->writeUint8(p.material, "material");
			s->writeSint8(p.delta, "delta");
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
void Pipeline::load(GAGCore::InputStream *s, Map &map, Uint32 tick, int versionMinor)
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
			if (versionMinor < FILE_FORMAT_VERSION_SIMPLE_RESOURCE_GROWTH)
			{
				s->readUint32("incarnation");
				p.type = s->readUint16("type");
				s->readUint8("variety");
				const auto spread = s->readUint8("spread");
				auto mask = s->readUint16("incrementMask");
				if (p.tile >= map.cellCount() || !map.resourceRegistry().valid(p.type) ||
					spread > 1 || (mask & ~map.resourcePropertiesByIndex(p.type).materialMask))
					throw std::runtime_error("Invalid legacy growth proposal");
				// A legacy empty-destination seed can have no yield increment bits.
				if (!mask)
					mask = materialBit(map.resourcePropertiesByIndex(p.type).primaryMaterial);
				for (; mask; mask &= mask - 1)
				{
					p.material = std::countr_zero(mask);
					b.proposals.push_back(p);
				}
			}
			else
			{
				p.type = s->readUint16("type");
				p.material = s->readUint8("material");
				p.delta = s->readSint8("delta");
				if (p.tile >= map.cellCount() || !map.resourceRegistry().valid(p.type) ||
					!validMaterial(p.material) || (p.delta != 1 && p.delta != -1) ||
					!(map.resourcePropertiesByIndex(p.type).materialMask & (1u << p.material)))
					throw std::runtime_error("Invalid growth proposal");
				b.proposals.push_back(p);
			}
			s->readLeaveSection();
		}
		s->readLeaveSection();
		j->reservedCapacity = b.proposals.capacity();
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
			mix(p.type);
			mix(p.material);
			mix(p.delta);
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
