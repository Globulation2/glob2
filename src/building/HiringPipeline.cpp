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
	s->writeEnterSection(name);
	s->writeUint32(value >> 32, "high");
	s->writeUint32(Uint32(value), "low");
	s->writeLeaveSection();
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
	s->readEnterSection(name);
	const auto high = s->readUint32("high");
	const auto value = (Uint64(high) << 32) | s->readUint32("low");
	s->readLeaveSection();
	return value;
}
void saveOutput(GAGCore::OutputStream *s, const Output &o)
{
	s->writeUint32(o.tick, "tick");
	s->writeUint32(o.team, "team");
	s->writeUint32(o.buildings.size(), "buildings");
	s->writeEnterSection("buildingLists");
	for (unsigned i = 0; i < o.buildings.size(); ++i)
	{
		s->writeEnterSection(i);
		const auto &b = o.buildings[i];
		s->writeUint16(b.building.gid, "gid");
		s->writeUint32(b.building.generation, "generation");
		s->writeSint32(b.type, "type");
		write64(s, b.configuration, "configuration");
		s->writeUint32(b.jobs.size(), "jobs");
		s->writeEnterSection("jobLists");
		for (unsigned n = 0; n < b.jobs.size(); ++n)
		{
			s->writeEnterSection(n);
			const auto &j = b.jobs[n];
			s->writeUint8(unsigned(j.key.job), "job");
			s->writeSint32(j.key.material, "material");
			s->writeUint32(j.candidates.size(), "candidates");
			s->writeEnterSection("suggestions");
			for (unsigned k = 0; k < j.candidates.size(); ++k)
			{
				s->writeEnterSection(k);
				const auto &c = j.candidates[k];
				s->writeUint16(c.unit.gid, "gid");
				s->writeUint32(c.unit.generation, "generation");
				write64(s, Uint64(c.level), "level");
				write64(s, Uint64(c.value), "value");
				s->writeLeaveSection();
			}
			s->writeLeaveSection();
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
Uint32 outputChecksum(const Output &output)
{
	GAGCore::ChecksumStream stream;
	saveOutput(&stream, output);
	return stream.value;
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
	s->readEnterSection("buildingLists");
	for (unsigned i = 0; i < count; ++i)
	{
		s->readEnterSection(i);
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
		s->readEnterSection("jobLists");
		for (unsigned n = 0; n < jobs; ++n)
		{
			s->readEnterSection(n);
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
			s->readEnterSection("suggestions");
			for (unsigned k = 0; k < candidates; ++k)
			{
				s->readEnterSection(k);
				HiringPlanner::Candidate c;
				c.unit.gid = s->readUint16("gid");
				c.unit.generation = s->readUint32("generation");
				c.level = std::bit_cast<Sint64>(read64(s, "level"));
				c.value = std::bit_cast<Sint64>(read64(s, "value"));

				if (Unit::GIDtoTeam(c.unit.gid) != int(o->team) || !c.unit.generation ||
					!units.insert(c.unit.gid).second)
					throw std::runtime_error("Invalid hiring unit");
				j.candidates.push_back(c);
				s->readLeaveSection();
			}
			s->readLeaveSection(2);
			b.jobs.push_back(std::move(j));
		}
		s->readLeaveSection(2);
		o->buildings.push_back(std::move(b));
	}
	s->readLeaveSection();
	o->checksum = outputChecksum(*o);
	return o;
}
} // namespace
struct HiringPipeline::Work
{
	std::shared_ptr<const HiringPlanner::Inputs> input;
	unsigned team = 0;
	std::size_t bound = 0;
	std::shared_ptr<const Output> output;
};
struct HiringPipeline::Batch
{
	Uint32 tick = 0, due = 0;
	ComputeExecutor::Batch ticket;
	std::vector<Work> work;
};
HiringPipeline::HiringPipeline(ComputeExecutor &e) : executor(e) {}
Uint32 HiringPipeline::checksumOutput(const HiringPlanner::Output &output)
{
	return outputChecksum(output);
}
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
	auto output = std::make_shared<Output>(HiringPlanner::Planner{}.rank(*w.input, w.team, Capacity));
	output->checksum = outputChecksum(*output);
	w.output = std::move(output);
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
	cachedChecksum.reset();
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
	batch->work.reserve(std::popcount(teamMask));
	for (unsigned t = 0; t < unsigned(game.teamsCount()); ++t)
		if (tick % Refresh == t % Refresh)
		{
			batch->work.push_back({input, t, 2048 + input->outputBounds[t], {}});
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
		std::array<ComputeExecutor::Group, Team::MAX_COUNT> groups;
		for (unsigned i = 0; i < b.work.size(); ++i)
		{
			auto &w = b.work[i];
			groups[i] = {1, {compute, &w}, ComputeExecutor::HiringLaneBase + w.team};
		}
		metrics.preparationNs += elapsed(prepareStart) - routeNs;
		const auto submitStart = startTiming();
		try
		{
			b.ticket = executor.submit(std::span(groups).first(b.work.size()), ComputeExecutor::boundaryDue(b.due),
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
		cachedChecksum.reset();
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
			metrics.planner.add(w.output->metrics);
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
	s->writeEnterSection("published");
	for (unsigned t = 0; t < published.size(); ++t)
	{
		s->writeEnterSection(t);
		const auto &o = published[t];
		s->writeUint8(bool(o), "present");
		if (o)
		{
			write64(s, publishedBounds[t], "bound");
			saveOutput(s, *o);
		}
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
	s->writeUint32(pending.size(), "pending");
	s->writeEnterSection("admissions");
	for (unsigned i = 0; i < pending.size(); ++i)
	{
		s->writeEnterSection(i);
		const auto &b = pending[i];
		s->writeUint32(b->tick, "tick");
		s->writeUint32(b->due, "due");
		s->writeUint32(b->work.size(), "teams");
		s->writeEnterSection("work");
		for (unsigned t = 0; t < b->work.size(); ++t)
		{
			s->writeEnterSection(t);
			const auto &w = b->work[t];
			write64(s, w.bound, "bound");
			saveOutput(s, *w.output);
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
	s->writeLeaveSection();
}
void HiringPipeline::load(Game &game, GAGCore::InputStream *s)
{
	cachedChecksum.reset();
	GAGCore::BinaryInputStream::CheckedReads checked(s);
	s->readEnterSection("HiringPipeline");
	if (readFlag(s, "hasLast"))
		lastObserved = s->readUint32("last");
	if ((lastObserved && *lastObserved > game.stepCounter) || s->readUint32("delay") != Delay ||
		s->readUint32("refresh") != Refresh || s->readUint32("extras") != Extras ||
		s->readUint32("capacity") != Capacity)
		throw std::runtime_error("Invalid hiring rules");
	s->readEnterSection("published");
	for (unsigned t = 0; t < published.size(); ++t)
	{
		s->readEnterSection(t);
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
		s->readLeaveSection();
	}
	s->readLeaveSection();
	const auto count = s->readCount("pending");
	if (count > Delay)
		throw std::runtime_error("Invalid hiring queue");
	s->readEnterSection("admissions");
	for (unsigned i = 0; i < count; ++i)
	{
		s->readEnterSection(i);
		auto b = std::make_unique<Batch>();
		b->tick = s->readUint32("tick");
		b->due = s->readUint32("due");
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
		s->readEnterSection("work");
		for (unsigned t = 0; t < teams; ++t)
		{
			s->readEnterSection(t);
			const auto bound = read64(s, "bound");
			if (bound < 2048 || bound > MaxOutputBytes - reserved)
				throw std::runtime_error("Invalid hiring pending bound");
			auto o = loadOutput(s, game, bound);
			if (o->tick != b->tick || o->team != expectedTeam)
				throw std::runtime_error("Invalid hiring admission");
			reserved += bound;
			expectedTeam += Refresh;
			b->work.push_back({{}, o->team, std::size_t(bound), o});
			s->readLeaveSection();
		}
		s->readLeaveSection(2);
		pending.push_back(std::move(b));
	}
	s->readLeaveSection(2);
}
Uint32 HiringPipeline::checkSum() const
{
	if (cachedChecksum)
		return *cachedChecksum;
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
			s.writeUint32(o->checksum, "output");
		}
	}
	// Pending identity is independent of hardware readiness. Outputs enter the
	// checksum only at the fixed publication boundary, including after loading.
	for (const auto &b : pending)
	{
		s.writeUint32(b->tick, "tick");
		s.writeUint32(b->due, "due");
		for (const auto &w : b->work)
		{
			s.writeUint32(w.team, "team");
			write64(&s, w.bound, "bound");
		}
	}
	cachedChecksum = s.value;
	return *cachedChecksum;
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

HiringPlanner::Inputs HiringPipeline::bindRoutes(Game &game, const SimulationSnapshot::Handle &world,
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
		if (!(teamMask & (1u << b.team)))
			continue;
		// Reservation uses the maintained maximum, not worker-computed vacancy
		// or material apportionment. Final lists are at most vacancies + two.
		const auto limit = std::min(128u, unsigned(b.desiredMaxUnitWorking) + 2);
		inputs.outputBounds[b.team] += 2 * 64 + (2 * MaterialCount + 3) * (2 * 128 + limit * 32);
		auto *live = game.resolveBuilding(b.identity);
		if (!live)
			throw std::logic_error("Hiring capture lost building incarnation");
		// The owner borrows existing generations. Workers expand them under
		// the existing locks; observation never prepares a cold or stale field.
		const auto &type = inputs.world.catalogs->buildings->at(b.typeNum).resolvedType;
		const bool footprint =
			std::any_of(std::begin(type.maxMaterial), std::begin(type.maxMaterial) + MaterialCount,
						[](int value) { return value > 0; }) ||
			std::any_of(std::begin(b.wishedMaterials), std::begin(b.wishedMaterials) + MaterialCount,
						[](int value) { return value > 0; });
		const bool clearing = type.attractsRole(0);
		const bool combat = type.attractsRole(1) || type.attractsRole(2);
		for (unsigned c = 0; c < SWIM_CLASS_COUNT; ++c)
			if (inputs.world.entities->availableGroundClasses[b.team] & (1u << c))
			{
				for (auto route :
					 {BuildingRoute::Footprint, BuildingRoute::Clearing, BuildingRoute::Combat})
				{
					if (route == BuildingRoute::Footprint && !footprint)
						continue;
					if (route == BuildingRoute::Clearing && !clearing)
						continue;
					if (route == BuildingRoute::Combat && !combat)
						continue;
					auto field = game.map.leaseBuildingRoute(live, c, route);
					if (!field) continue;
					routes->push_back({b.identity, live->routeSlot(c, route), std::move(field)});
				}
			}
	}
	inputs.routes = std::move(routes);
	return inputs;
}
