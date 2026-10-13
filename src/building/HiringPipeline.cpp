// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPipeline.h"
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "Unit.h"
#include "HiringDiagnostics.h"
#include "SharedBuildingGradient.h"
#include <chrono>
#include <bit>
#include <BinaryStream.h>
#include <ChecksumStream.h>
#include <limits>
#include <algorithm>
#include <set>

namespace
{
using Output = HiringPlanner::Output;
using Clock = std::chrono::steady_clock;
Uint64 elapsed(Clock::time_point start)
{
	return start == Clock::time_point{}
			   ? 0
			   : std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
Clock::time_point startTiming()
{
	return HiringDiagnostics::enabled() ? Clock::now() : Clock::time_point{};
}
static_assert(sizeof(Output) <= 2048 && sizeof(HiringPlanner::BuildingLists) <= 64 &&
			  sizeof(HiringPlanner::List) <= 128 && sizeof(HiringPlanner::Candidate) <= 32);
void write64(GAGCore::OutputStream *s, Uint64 value, const char *name)
{
	s->writeUint32(value >> 32, name);
	s->writeUint32(Uint32(value), name);
}
bool readFlag(GAGCore::InputStream *s, const char *name)
{
	const auto value = s->readUint8(name);
	if (value > 1)
		throw std::runtime_error("Invalid hiring flag");
	return value;
}
Uint64 read64(GAGCore::InputStream *s, const char *name)
{
	const auto high = s->readUint32(name);
	return (Uint64(high) << 32) | s->readUint32(name);
}
void saveOutput(GAGCore::OutputStream *s, const Output &o)
{
	s->writeUint32(o.tick, "tick");
	s->writeUint32(o.team, "team");
	s->writeUint32(o.buildings.size(), "buildings");
	for (const auto &b : o.buildings)
	{
		s->writeUint16(b.building.gid, "gid");
		s->writeUint32(b.building.generation, "generation");
		s->writeSint32(b.type, "type");
		write64(s, b.configuration, "configuration");
		s->writeUint32(b.jobs.size(), "jobs");
		for (const auto &j : b.jobs)
		{
			s->writeUint8(unsigned(j.key.job), "job");
			s->writeSint32(j.key.material, "material");
			s->writeUint32(j.candidates.size(), "candidates");
			for (const auto &c : j.candidates)
			{
				s->writeUint16(c.unit.gid, "gid");
				s->writeUint32(c.unit.generation, "generation");
				write64(s, Uint64(c.level), "level");
				write64(s, Uint64(c.value), "value");
			}
		}
	}
}
std::shared_ptr<const Output> loadOutput(GAGCore::InputStream *s, const Game &game,
										 std::size_t budget = HiringPipeline::MaxOutputBytes)
{
	auto o = std::make_shared<Output>();
	o->tick = s->readUint32("tick");
	o->team = s->readUint32("team");
	if (o->team >= unsigned(game.teamsCount()) || o->tick > game.stepCounter)
		throw std::runtime_error("Invalid hiring observation");
	auto count = s->readCount("buildings");
	if (count > Building::MAX_COUNT)
		throw std::runtime_error("Invalid hiring building count");
	o->retainedBytes = 2048 + 2 * count * 64;
	if (o->retainedBytes > budget)
		throw std::runtime_error("Hiring output memory limit");
	o->buildings.reserve(count);
	int previousBuilding = -1;
	for (unsigned i = 0; i < count; ++i)
	{
		HiringPlanner::BuildingLists b;
		b.building.gid = s->readUint16("gid");
		b.building.generation = s->readUint32("generation");
		b.type = s->readSint32("type");
		b.configuration = read64(s, "configuration");
		if (Building::GIDtoTeam(b.building.gid) != int(o->team) || b.type < 0 ||
			b.type >= int(game.buildingsTypes.size()) || int(b.building.gid) <= previousBuilding ||
			!b.building.generation)
			throw std::runtime_error("Invalid hiring building");
		previousBuilding = b.building.gid;
		const auto jobs = s->readCount("jobs");
		if (jobs > 2 * MaterialCount + 3)
			throw std::runtime_error("Invalid hiring jobs");
		o->retainedBytes += 2 * jobs * 128;
		if (o->retainedBytes > budget)
			throw std::runtime_error("Hiring output memory limit");
		b.jobs.reserve(jobs);
		int previousJob = -1;
		for (unsigned n = 0; n < jobs; ++n)
		{
			HiringPlanner::List j;
			j.key.job = Hiring::Job(s->readUint8("job"));
			j.key.material = s->readSint32("material");
			if (unsigned(j.key.job) > unsigned(Hiring::Job::Defend) ||
				(j.key.job <= Hiring::Job::Fetch
					 ? j.key.material < 0 || j.key.material >= MaterialCount
					 : j.key.material != -1))
				throw std::runtime_error("Invalid hiring job");
			const int jobOrder = j.key.job <= Hiring::Job::Fetch
									 ? j.key.material * 2 + int(j.key.job)
									 : 2 * MaterialCount + int(j.key.job) - int(Hiring::Job::Clear);
			if (jobOrder <= previousJob)
				throw std::runtime_error("Noncanonical hiring jobs");
			previousJob = jobOrder;
			const auto candidates = s->readCount("candidates");
			if (candidates > HiringPipeline::Capacity)
				throw std::runtime_error("Invalid hiring shortlist");
			o->retainedBytes += candidates * 32;
			if (o->retainedBytes > budget)
				throw std::runtime_error("Hiring output memory limit");
			j.candidates.reserve(candidates);
			std::set<Uint16> units;
			for (unsigned k = 0; k < candidates; ++k)
			{
				HiringPlanner::Candidate c;
				c.unit.gid = s->readUint16("gid");
				c.unit.generation = s->readUint32("generation");
				c.level = std::bit_cast<Sint64>(read64(s, "level"));
				c.value = std::bit_cast<Sint64>(read64(s, "value"));

				if (Unit::GIDtoTeam(c.unit.gid) != int(o->team) || !c.unit.generation ||
					!units.insert(c.unit.gid).second)
					throw std::runtime_error("Invalid hiring unit");
				j.candidates.push_back(c);
			}
			b.jobs.push_back(std::move(j));
		}
		o->buildings.push_back(std::move(b));
	}
	return o;
}
} // namespace
struct HiringPipeline::Work
{
	std::shared_ptr<const HiringPlanner::Inputs> input;
	HiringPlanner::Cache *cache = nullptr;
	unsigned team = 0;
	std::size_t bound = 0;
	std::shared_ptr<const Output> output;
};
struct HiringPipeline::Batch
{
	Uint32 tick = 0, due = 0;
	mutable std::optional<Uint32> digest;
	mutable SimulationSnapshot::Handle observation;
	Uint64 routeIdentity = 0;
	ComputeExecutor::Batch ticket;
	std::vector<Work> work;
};
HiringPipeline::HiringPipeline(ComputeExecutor &e) : executor(e) {}
HiringPipeline::~HiringPipeline()
{
	try
	{
		drain();
	}
	catch (...)
	{
	}
}
void HiringPipeline::compute(void *context, std::size_t)
{
	auto &w = *static_cast<Work *>(context);
	w.output = std::make_shared<Output>(w.cache->rank(*w.input, w.team, Capacity));
	if (w.output->retainedBytes > w.bound)
		throw std::length_error("Hiring output exceeds reservation");
	w.input.reset();
}
void HiringPipeline::observe(Game &game, const SimulationSnapshot::Handle &snapshot)
{
	if (failure)
		std::rethrow_exception(failure);
	const auto tick = game.stepCounter;
	if (lastObserved && *lastObserved == tick)
		return;
	if (snapshot.tick != tick)
		throw std::logic_error("Hiring boundary mismatch");
	if (tick > std::numeric_limits<Uint32>::max() - Delay)
		throw std::overflow_error("Hiring deadline overflow");
	const auto prepareStart = startTiming();
	unsigned teamMask = 0;
	for (unsigned t = 0; t < unsigned(game.teamsCount()); ++t)
		if (tick % Refresh == t % Refresh)
			teamMask |= 1u << t;
	if (!teamMask)
	{
		lastObserved = tick;
		return;
	}
	const auto routeStart = startTiming();
	auto input = std::make_shared<HiringPlanner::Inputs>(bindRoutes(game, snapshot, teamMask));
	const auto routeNs = elapsed(routeStart);
	metrics.routeHandlesNs += routeNs;
	auto batch = std::make_unique<Batch>();
	batch->tick = tick;
	batch->due = tick + Delay;
	batch->observation = input->world;
	batch->routeIdentity = input->routeIdentity;
	batch->work.reserve(game.teamsCount());
	for (unsigned t = 0; t < unsigned(game.teamsCount()); ++t)
		if (tick % Refresh == t % Refresh)
		{
			if (!caches[t])
				caches[t] = std::make_unique<HiringPlanner::Cache>(std::min<std::size_t>(
					HiringPlanner::Cache::MaxBytes, 64u * 1024 * 1024 / game.teamsCount()));
			batch->work.push_back({input, caches[t].get(), t, 2048 + input->outputBounds[t], {}});
		}
	if (!batch->work.empty())
	{
		std::size_t reserve = outputReservation();
		for (const auto &w : batch->work)
			reserve += w.bound;
		if (reserve > MaxOutputBytes)
			throw std::length_error("Hiring pending output reservations exceed match bound");
		if (pending.size() >= Delay)
			throw std::logic_error("Hiring publication horizon exceeded");
		pending.push_back(std::move(batch));
		auto &b = *pending.back();
		std::vector<ComputeExecutor::Group> groups;
		for (auto &w : b.work)
			groups.push_back({1, {compute, &w}, ComputeExecutor::HiringLaneBase + w.team});
		metrics.preparationNs += elapsed(prepareStart) - routeNs;
		const auto submitStart = startTiming();
		try
		{
			b.ticket = executor.submit(groups, ComputeExecutor::boundaryDue(b.due),
									   {ComputeProducer::Other, b.tick, b.due});
		}
		catch (...)
		{
			failure = std::current_exception();
			throw;
		}
		metrics.submissionNs += elapsed(submitStart);
		metrics.submitted += b.work.size();
	}
	++metrics.observations;
	lastObserved = tick;
}
void HiringPipeline::drain(JoinReason reason)
{
	for (auto &b : pending)
		if (!b->ticket.empty())
		{
			try
			{
				executor.join(b->ticket, {reason, ComputeTelemetry::NoTick});
			}
			catch (...)
			{
				if (!failure)
					failure = std::current_exception();
			}
			b->ticket = {};
		}
	if (failure)
		std::rethrow_exception(failure);
}
void HiringPipeline::publishDue(Uint32 tick)
{
	if (failure)
		std::rethrow_exception(failure);
	while (!pending.empty() && pending.front()->due <= tick)
	{
		const auto publishStart = startTiming();
		auto &b = *pending.front();
		const auto waitStart = startTiming();
		try
		{
			if (!b.ticket.empty())
				executor.join(b.ticket, {JoinReason::TickDeadline, tick});
		}
		catch (...)
		{
			failure = std::current_exception();
			throw;
		}
		b.ticket = {};
		metrics.deadlineWaitNs += elapsed(waitStart);
		for (const auto &w : b.work)
			if (!w.output)
				throw std::logic_error("Missing hiring result");
		for (const auto &w : b.work)
		{
			published[w.team] = w.output;
			publishedBounds[w.team] = w.bound;
			metrics.plannerNs += w.output->ns;
			++metrics.published;
		}
		pending.pop_front();
		metrics.publicationNs += elapsed(publishStart);
	}
}
const HiringPlanner::BuildingLists *HiringPipeline::suggestions(const Building &b) const
{
	if (b.buildingState != Building::ALIVE)
		return nullptr;
	const auto &o = published[b.owner->teamNumber];
	if (!o)
		return nullptr;
	const auto ref = Game::refOf(&b);
	const auto it =
		std::lower_bound(o->buildings.begin(), o->buildings.end(), ref.gid,
						 [](const auto &lists, unsigned gid) { return lists.building.gid < gid; });
	if (it != o->buildings.end() && it->building == ref && it->type == b.typeNum &&
		it->configuration == HiringPlanner::configuration(b))
		return &*it;
	return nullptr;
}
void HiringPipeline::save(GAGCore::OutputStream *s)
{
	drain(JoinReason::Save);
	s->writeEnterSection("HiringPipeline");
	s->writeUint8(lastObserved.has_value(), "hasLast");
	if (lastObserved)
		s->writeUint32(*lastObserved, "last");
	s->writeUint32(Delay, "delay");
	s->writeUint32(Refresh, "refresh");
	s->writeUint32(Extras, "extras");
	s->writeUint32(Capacity, "capacity");
	for (unsigned t = 0; t < published.size(); ++t)
	{
		const auto &o = published[t];
		s->writeUint8(bool(o), "present");
		if (o)
		{
			write64(s, publishedBounds[t], "bound");
			saveOutput(s, *o);
		}
	}
	s->writeUint32(pending.size(), "pending");
	for (const auto &b : pending)
	{
		s->writeUint32(b->tick, "tick");
		s->writeUint32(b->due, "due");
		s->writeUint32(inputDigest(*b), "inputDigest");
		s->writeUint32(b->work.size(), "teams");
		for (const auto &w : b->work)
		{
			write64(s, w.bound, "bound");
			saveOutput(s, *w.output);
		}
	}
	s->writeLeaveSection();
}
void HiringPipeline::load(Game &game, GAGCore::InputStream *s)
{
	GAGCore::BinaryInputStream::CheckedReads checked(s);
	s->readEnterSection("HiringPipeline");
	if (readFlag(s, "hasLast"))
		lastObserved = s->readUint32("last");
	if ((lastObserved && *lastObserved > game.stepCounter) || s->readUint32("delay") != Delay ||
		s->readUint32("refresh") != Refresh || s->readUint32("extras") != Extras ||
		s->readUint32("capacity") != Capacity)
		throw std::runtime_error("Invalid hiring rules");
	for (unsigned t = 0; t < published.size(); ++t)
		if (readFlag(s, "present"))
		{
			const auto bound = read64(s, "bound");
			if (bound < 2048 || bound > MaxOutputBytes - outputReservation())
				throw std::runtime_error("Invalid hiring output bound");
			auto o = loadOutput(s, game, bound);
			if (o->team != t || o->tick % Refresh != t % Refresh ||
				o->tick > std::numeric_limits<Uint32>::max() - Delay ||
				o->tick + Delay > game.stepCounter)
				throw std::runtime_error("Invalid published hiring team or deadline");
			published[t] = std::move(o);
			publishedBounds[t] = bound;
		}
	const auto count = s->readCount("pending");
	if (count > Delay)
		throw std::runtime_error("Invalid hiring queue");
	for (unsigned i = 0; i < count; ++i)
	{
		auto b = std::make_unique<Batch>();
		b->tick = s->readUint32("tick");
		b->due = s->readUint32("due");
		b->digest = s->readUint32("inputDigest");
		if (b->tick > std::numeric_limits<Uint32>::max() - Delay || b->due != b->tick + Delay ||
			b->due < game.stepCounter || !lastObserved || b->tick > *lastObserved ||
			(!pending.empty() && b->tick <= pending.back()->tick))
			throw std::runtime_error("Invalid hiring deadline");
		const auto teams = s->readCount("teams");
		unsigned expected = 0;
		for (unsigned t = 0; t < unsigned(game.teamsCount()); ++t)
			expected += t % Refresh == b->tick % Refresh;
		if (teams != expected)
			throw std::runtime_error("Invalid hiring team count");
		unsigned expectedTeam = b->tick % Refresh;
		std::size_t reserved = outputReservation();
		for (unsigned t = 0; t < teams; ++t)
		{
			const auto bound = read64(s, "bound");
			if (bound < 2048 || bound > MaxOutputBytes - reserved)
				throw std::runtime_error("Invalid hiring pending bound");
			auto o = loadOutput(s, game, bound);
			if (o->tick != b->tick || o->team != expectedTeam)
				throw std::runtime_error("Invalid hiring admission");
			reserved += bound;
			expectedTeam += Refresh;
			b->work.push_back({{}, nullptr, o->team, std::size_t(bound), o});
		}
		pending.push_back(std::move(b));
	}
	s->readLeaveSection();
}
Uint32 HiringPipeline::checkSum() const
{
	if (!lastObserved && pending.empty() &&
		std::none_of(published.begin(), published.end(), [](const auto &o) { return bool(o); }))
		return 0;
	GAGCore::ChecksumStream s;
	s.writeUint32(Delay, "delay");
	s.writeUint32(Refresh, "refresh");
	s.writeUint32(Extras, "extras");
	s.writeUint32(Capacity, "capacity");
	s.writeUint8(lastObserved.has_value(), "hasLast");
	if (lastObserved)
		s.writeUint32(*lastObserved, "last");
	for (unsigned t = 0; t < published.size(); ++t)
	{
		const auto &o = published[t];
		s.writeUint8(bool(o), "present");
		if (o)
		{
			write64(&s, publishedBounds[t], "bound");
			saveOutput(&s, *o);
		}
	}
	// Pending identity is independent of hardware readiness. Outputs enter the
	// checksum only at the fixed publication boundary, including after loading.
	for (const auto &b : pending)
	{
		s.writeUint32(b->tick, "tick");
		s.writeUint32(b->due, "due");
		s.writeUint32(inputDigest(*b), "inputDigest");
		for (const auto &w : b->work)
		{
			s.writeUint32(w.team, "team");
			write64(&s, w.bound, "bound");
		}
	}
	return s.value;
}

std::size_t HiringPipeline::outputReservation() const
{
	std::size_t bytes = 0;
	for (auto bound : publishedBounds)
		bytes += bound;
	for (const auto &b : pending)
		for (const auto &w : b->work)
			bytes += w.bound;
	return bytes;
}

Uint32 HiringPipeline::inputDigest(const Batch &batch) const
{
	// Verification/save work, never admission preparation. The immutable lease
	// supplies the same content whether the worker is ready or still queued.
	if (!batch.digest)
	{
		const auto start = startTiming();
		batch.digest = inputDigests.digest(batch.observation, batch.routeIdentity);
		batch.observation = {};
		metrics.inputDigestNs += elapsed(start);
	}
	return *batch.digest;
}

namespace
{
void mixRoute(Uint64 &hash, Uint64 value)
{
	hash ^= value;
	hash *= 1099511628211ull;
}
} // namespace
HiringPlanner::Inputs HiringPipeline::bindRoutes(Game &game, SimulationSnapshot::Handle world,
												 unsigned teamMask)
{
	if (world.tick != game.stepCounter)
		throw std::logic_error("Hiring input boundary mismatch");
	HiringPlanner::Inputs inputs;
	inputs.world = world.simulationView();
	auto routes = std::make_shared<std::vector<HiringPlanner::Inputs::Route>>();
	for (const auto &b : inputs.world.entities->buildings)
	{
		if (!HiringPlanner::needsCandidates(b))
			continue;
		++inputs.teamBuildings[b.team];
		if (!(teamMask & (1u << b.team)))
			continue;
		// Reservation uses the maintained maximum, not worker-computed vacancy
		// or material apportionment. Final lists are at most vacancies + two.
		const auto limit = std::min(128u, unsigned(b.desiredMaxUnitWorking) + 2);
		inputs.outputBounds[b.team] += 2 * 64 + (2 * MaterialCount + 3) * (2 * 128 + limit * 32);
		auto *live = game.resolveBuilding(b.identity);
		if (!live)
			throw std::logic_error("Hiring capture lost building incarnation");
		// The owner creates safe live generations; worker route queries only
		// expand the leased generations under their existing locks.
		const auto &type = inputs.world.catalogs->buildings->at(b.typeNum).resolvedType;
		for (unsigned c = 0; c < SWIM_CLASS_COUNT; ++c)
			if (inputs.world.entities->groundClasses[b.team] & (1u << c))
			{
				int distance = 0;
				for (auto route :
					 {BuildingRoute::Footprint, BuildingRoute::Clearing, BuildingRoute::Combat})
				{
					if (route == BuildingRoute::Footprint &&
						!std::any_of(std::begin(type.maxMaterial),
									 std::begin(type.maxMaterial) + MaterialCount,
									 [](int value) { return value > 0; }) &&
						!std::any_of(std::begin(b.wishedMaterials),
									 std::begin(b.wishedMaterials) + MaterialCount,
									 [](int value) { return value > 0; }))
						continue;
					if (route == BuildingRoute::Clearing && !type.attractsRole(0))
						continue;
					if (route == BuildingRoute::Combat &&
						!(type.attractsRole(1) || type.attractsRole(2)))
						continue;
					game.map.buildingAvailable(live, c, b.posX, b.posY, &distance, route);
				}
			}
		// Only lease existing generations. Workers choose movement classes.
		for (int slot = 0; slot < BUILDING_GRADIENT_COUNT; ++slot)
			if (auto field = live->leaseGradient(slot))
			{
				auto guard = SharedBuildingGradient::lock(field);
				auto bytes = std::size_t(field->width) * field->height * sizeof(Uint16) +
							 sizeof(SharedBuildingGradient);
				if (field->search)
					bytes += field->search->retainedBytes();
				inputs.routeBytes += bytes;
				inputs.routeAllocations += routes->size() == routes->capacity();
				routes->push_back({b.identity, slot, std::move(field), bytes});
				mixRoute(inputs.routeIdentity, b.identity.gid);
				mixRoute(inputs.routeIdentity, b.identity.generation);
				mixRoute(inputs.routeIdentity, slot);
				mixRoute(inputs.routeIdentity, routes->back().field->inputDigest);
			}
	}
	inputs.routes = std::move(routes);
	return inputs;
}
