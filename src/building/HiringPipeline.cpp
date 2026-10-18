// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPipeline.h"
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "Unit.h"
#include "SharedBuildingGradient.h"
#include "BuildingGradientJob.h"
#include "SnapshotGradient.h"
#include "BuildingGradientDepthPolicy.h"
#include "ai/observation/AIWorldView.h"
#include <PackedArray.h>
#include <bit>
#include <BinaryStream.h>
#include <ChecksumStream.h>
#include <limits>
#include <algorithm>
#include <set>

namespace
{
using Output = HiringPlanner::Output;
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
// Counts are bounded by entity, job and shortlist limits before allocation.
std::shared_ptr<const Output> loadOutput(GAGCore::InputStream *s, const Game &game)
{
	auto o = std::make_shared<Output>();
	o->tick = s->readUint32("tick");
	o->team = s->readUint32("team");
	if (o->team >= unsigned(game.teamsCount()) || o->tick > game.stepCounter)
		throw std::runtime_error("Invalid hiring observation");
	auto count = s->readCount("buildings");
	if (count > Building::MAX_COUNT)
		throw std::runtime_error("Invalid hiring building count");
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
		if (Building::GIDtoTeam(b.building.gid) != int(o->team) || b.type < 0 ||
			b.type >= int(game.buildingsTypes.size()) || int(b.building.gid) <= previousBuilding ||
			!b.building.generation)
			throw std::runtime_error("Invalid hiring building");
		previousBuilding = b.building.gid;
		const auto jobs = s->readCount("jobs");
		if (jobs > 2 * MaterialCount + 3)
			throw std::runtime_error("Invalid hiring jobs");
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
	return o;
}
} // namespace
// Only cold routes own outputs here. Existing (including stale) generations
// remain leased and are expanded under their usual lock, without refreshing.
struct HiringPlanner::PreparedRoute
{
	BuildingRef building;
	bool proactive = false;
	building_gradient::Job job;
	SimulationSnapshot::Handle snapshot;
	std::shared_ptr<SharedBuildingGradient> field = std::make_shared<SharedBuildingGradient>();

	void initialize()
	{
		if (job.data)
			return;
		field->width = snapshot.width;
		field->height = snapshot.height;
		job.data.reset(new Uint16[std::size_t(snapshot.width) * snapshot.height]);
		job.search = acquireBuildingGradientSearch();
		const auto result = gradient_preparation::buildBuilding(
			job.seed, snapshot, BuildingGradientSearch::Inputs::ofSnapshot(snapshot, job.swim),
			job.data.get(), *job.search, BuildingGradientDepth::MIN_DEPTH);
		job.locked = result.locked;
		job.resourceState = result.resourceState;
		if (job.locked)
			recycleBuildingGradientSearch(std::move(job.search));
		field->values = job.data.get();
		field->search = job.search.get();
		snapshot = {};
	}
	Building *destination(Game &game) const
	{
		auto *live = game.resolveBuilding(building);
		return live && live->buildingState == Building::ALIVE &&
					   live->refreshEpoch[job.slot] == job.epoch &&
					   game.map.topologyGeneration == job.generation &&
					   (!live->globalGradient[job.slot] ||
						live->lastGlobalGradientUpdateStepCounter[job.slot] < job.captureTick)
				   ? live
				   : nullptr;
	}
	// Like Map's scheduled fields, save a completed field from its frozen
	// inputs rather than serializing implementation-specific search buckets.
	void save(GAGCore::OutputStream *s)
	{
		if (job.search)
			job.search->finish();
		s->writeUint16(building.gid, "gid");
		s->writeUint32(building.generation, "identity");
		s->writeUint8(job.slot, "slot");
		s->writeUint32(job.captureTick, "captured");
		s->writeUint32(job.generation, "topology");
		s->writeUint8(job.locked, "locked");
		s->writeUint8(job.resourceState, "resourceState");
		s->writeUint8(proactive, "proactive");
		const auto cells = std::size_t(field->width) * field->height;
		if (GAGCore::PackedArray::binary(s))
			GAGCore::PackedArray::write<Uint16>(s, cells,
												[&](std::size_t i) { return job.data[i]; });
		else
			s->writeUint16Sections(job.data.get(), cells, "value");
	}
	static std::shared_ptr<PreparedRoute> load(Game &game, GAGCore::InputStream *s, unsigned team,
											   unsigned captured, int versionMinor)
	{
		auto result = std::make_shared<PreparedRoute>();
		result->building.gid = s->readUint16("gid");
		result->building.generation = s->readUint32("identity");
		auto &job = result->job;
		job.slot = s->readUint8("slot");
        if (!hasUnifiedMovement(versionMinor)) {
            if (job.slot >= BUILDING_ROUTE_COUNT * SWIM_CLASS_COUNT)
                throw std::runtime_error("Invalid legacy prepared hiring gradient slot");
            job.slot = (job.slot / SWIM_CLASS_COUNT) * MOVEMENT_CLASS_COUNT + job.slot % SWIM_CLASS_COUNT;
        }
		job.captureTick = s->readUint32("captured");
		job.generation = s->readUint32("topology");
		job.locked = readFlag(s, "locked");
		job.resourceState = s->readUint8("resourceState");
		if (versionMinor >= FILE_FORMAT_VERSION_PROACTIVE_BUILDINGS)
			result->proactive = readFlag(s, "proactive");
		if (Building::GIDtoTeam(result->building.gid) != int(team) ||
			job.slot >= BUILDING_GRADIENT_COUNT || job.captureTick != captured ||
			!result->building.generation || job.resourceState > 2)
			throw std::runtime_error("Invalid prepared hiring gradient");
		job.swim = job.slot % MOVEMENT_CLASS_COUNT;
		job.route = BuildingRoute(job.slot / MOVEMENT_CLASS_COUNT);
		auto *live = game.resolveBuilding(result->building);
		if (live)
			job.epoch = live->refreshEpoch[job.slot];
		auto &field = *result->field;
		field.width = game.map.getW();
		field.height = game.map.getH();
		const auto cells = std::size_t(field.width) * field.height;
		job.data.reset(new Uint16[cells]);
		if (GAGCore::PackedArray::binary(s))
			GAGCore::PackedArray::read<Uint16>(s, cells,
											   [&](std::size_t i, Uint16 v) { job.data[i] = v; });
		else
			for (std::size_t i = 0; i < cells; ++i)
			{
				s->readEnterSection(i);
				job.data[i] = s->readUint16("value");
				s->readLeaveSection();
			}
		field.values = job.data.get();
		return result;
	}
	void publish(Game &game, std::vector<const PreparedRoute *> &published)
	{
		auto *live = destination(game);
		if (!live)
			return;
		const int slot = job.slot;
		auto guard = SharedBuildingGradient::lock(live->sharedGradient[slot]);
		live->retireSharedGradient(slot);
		game.map.recycleBuildingGradientBuffer(live->globalGradient[slot]);
		recycleBuildingGradientSearch(std::move(live->globalGradientSearch[slot]));
		live->supersedeGradient(slot);
		live->globalGradient[slot] = job.data.release();
		live->globalGradientSearch[slot] = std::move(job.search);
		live->sharedGradient[slot] = field;
		live->dirtyGradient[slot] = false;
		live->gradientGeneration[slot] = job.generation;
		live->lastGlobalGradientUpdateStepCounter[slot] = job.captureTick;
		live->globalGradientUsedStep[slot] =
			std::max(live->globalGradientUsedStep[slot], job.captureTick);
		// Access metadata is shared by swim classes of one variant. Keep a
		// newer sibling's result, including an owner build later in this tick.
		bool newest = true;
		for (int swim = 0; swim < MOVEMENT_CLASS_COUNT; ++swim)
		{
			if (swim == job.swim || swimAccessVariant(swim) != swimAccessVariant(job.swim))
				continue;
			const int other = live->routeSlot(swim, job.route);
			if (!live->globalGradient[other])
				continue;
			const auto stamp = live->lastGlobalGradientUpdateStepCounter[other];
			const bool publishedHere = std::any_of(published.begin(), published.end(),
												   [&](const auto *p)
												   {
													   return p->building == building &&
															  p->job.slot == other &&
															  p->job.captureTick == job.captureTick;
												   });
			if (stamp > job.captureTick || (stamp == job.captureTick && !publishedHere))
				newest = false;
		}
		if (newest)
		{
			live->setLocked(live->routeAccess(job.swim, job.route), job.locked);
			if (job.route == BuildingRoute::Clearing)
				live->anyResourceToClear[swimAccessVariant(job.swim)] = job.resourceState;
		}
		published.push_back(this);
	}
};

struct HiringPipeline::Work
{
	std::shared_ptr<const HiringPlanner::Inputs> input;
	unsigned team = 0;
	std::shared_ptr<const Output> output;
	std::vector<std::shared_ptr<HiringPlanner::PreparedRoute>> gradients;
};
struct HiringPipeline::Batch
{
	Uint32 tick = 0, due = 0;
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
	if (w.team % Refresh == w.input->world.tick % Refresh)
		w.output =
			std::make_shared<Output>(HiringPlanner::Planner{}.rank(*w.input, w.team, Capacity));
	else
	{
		auto output = std::make_shared<Output>();
		output->tick = w.input->world.tick;
		output->team = w.team;
		w.output = std::move(output);
	}
	const AIEngine::AIWorldView world(w.input->world);
	for (const auto &building : w.output->buildings)
		for (const auto &list : building.jobs)
			for (unsigned i = 0; i < list.primaryCount; ++i)
			{
				const auto &candidate = list.candidates[i];
				const auto *unit = world.unitAtSlot(candidate.unit.gid);
				if (!unit || unit->identity != candidate.unit)
					continue;
				const auto kind =
					list.key.job == Hiring::Job::Delivery || list.key.job == Hiring::Job::Fetch
						? BuildingRoute::Footprint
					: list.key.job == Hiring::Job::Clear ? BuildingRoute::Clearing
														 : BuildingRoute::Combat;
				const int slot = int(kind) * MOVEMENT_CLASS_COUNT +
								 (unit->performance[FLY] ? AIR_MOVEMENT_CLASS : Map::swimClass(unit->performance[WALK], unit->performance[SWIM]));
				for (const auto &route : *w.input->routes)
					if (route.building == building.building && route.slot == slot)
					{
						if (route.preparation)
							route.preparation->initialize();
						route.field->distance(unit->posX, unit->posY);
						break;
					}
			}
	// Creation and the primary shortlist share one field; only unused proactive
	// routes still need their shallow seed after primary extensions finish.
	for (const auto &route : *w.input->routes)
		if (Building::GIDtoTeam(route.building.gid) == int(w.team) && route.preparation)
		{
			if (route.initialize)
				route.preparation->initialize();
			if (route.preparation->job.data)
				w.gradients.push_back(route.preparation);
		}
	w.input.reset();
}
void HiringPipeline::requestInitialization(Building &building)
{
	game = building.owner->game;
	initializationRequests[building.gid] = Game::refOf(&building);
}
bool HiringPipeline::preparing(const Building &building, int slot) const
{
	return game && std::any_of(initializing.begin(), initializing.end(),
							   [&](const auto &claim)
							   {
								   return claim.building == Game::refOf(&building) &&
										  claim.slot == slot &&
										  claim.epoch == building.refreshEpoch[slot] &&
										  claim.generation == game->map.topologyGeneration;
							   });
}
void HiringPipeline::observe(Game &game, const SimulationSnapshot::Handle &snapshot)
{
	this->game = &game;
	const auto tick = game.stepCounter;
	if (lastObserved && *lastObserved == tick)
		return;
	if (snapshot.tick != tick)
		throw std::logic_error("Hiring boundary mismatch");
	if (tick > std::numeric_limits<Uint32>::max() - Delay)
		throw std::overflow_error("Hiring deadline overflow");
	lastObserved = tick;
	unsigned teamMask = 0;
	for (unsigned t = tick % Refresh; t < unsigned(game.teamsCount()); t += Refresh)
		teamMask |= 1u << t;
	std::erase_if(initializationRequests,
				  [&](const auto &entry)
				  {
					  const auto *b = game.resolveBuilding(entry.second);
					  return !b || b->buildingState != Building::ALIVE;
				  });
	for (const auto &[gid, ref] : initializationRequests)
		teamMask |= 1u << Building::GIDtoTeam(gid);
	if (!teamMask)
		return;
	if (pending.size() >= Delay)
		throw std::logic_error("Hiring publication horizon exceeded");
	auto input = std::make_shared<HiringPlanner::Inputs>(
		bindRoutes(game, snapshot, teamMask, &initializationRequests));
	auto batch = std::make_unique<Batch>();
	batch->tick = tick;
	batch->due = tick + Delay;
	initializing.clear();
	for (const auto &route : *input->routes)
		if (route.initialize && route.preparation)
		{
			const auto &job = route.preparation->job;
			initializing.push_back({route.building, route.slot, job.epoch, job.generation});
		}
	initializationRequests.clear();
	for (unsigned t = 0; t < unsigned(game.teamsCount()); ++t)
		if (teamMask & (1u << t))
			batch->work.push_back({input, t, {}});
	auto &b = *pending.emplace_back(std::move(batch));
	std::array<ComputeExecutor::Group, Team::MAX_COUNT> groups;
	for (unsigned i = 0; i < b.work.size(); ++i)
		groups[i] = {1, {compute, &b.work[i]}, ComputeExecutor::HiringLaneBase + b.work[i].team};
	b.ticket =
		executor.submit(std::span(groups).first(b.work.size()), ComputeExecutor::boundaryDue(b.due),
						{ComputeProducer::Other, b.tick, b.due});
	metrics.submitted += b.work.size();
}
void HiringPipeline::drain(JoinReason reason)
{
	std::exception_ptr failure;
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
	while (!pending.empty() && pending.front()->due <= tick)
	{
		auto &b = *pending.front();
		if (!b.ticket.empty())
			executor.join(b.ticket, {JoinReason::TickDeadline, tick});
		b.ticket = {};
		std::vector<const HiringPlanner::PreparedRoute *> preparedHere;
		for (const auto &w : b.work)
		{
			if (!w.output)
				throw std::logic_error("Missing hiring result");
			for (const auto &gradient : w.gradients)
				gradient->publish(*game, preparedHere);
			if (w.team % Refresh == b.tick % Refresh)
				published[w.team] = w.output;
			++metrics.published;
		}
		pending.pop_front();
		initializing.clear();
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
	// Configuration changes need no invalidation: admission rechecks live state.
	if (it != o->buildings.end() && it->building == ref && it->type == b.typeNum)
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
			saveOutput(s, *o);
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
			saveOutput(s, *b->work[t].output);
			std::vector<std::shared_ptr<HiringPlanner::PreparedRoute>> gradients;
			for (const auto &gradient : b->work[t].gradients)
				if (gradient->destination(*game))
					gradients.push_back(gradient);
			s->writeUint32(gradients.size(), "gradients");
			s->writeEnterSection("preparedGradients");
			for (unsigned i = 0; i < gradients.size(); ++i)
			{
				s->writeEnterSection(i);
				gradients[i]->save(s);
				s->writeLeaveSection();
			}
			s->writeLeaveSection();
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
	s->writeUint32(initializationRequests.size(), "initializations");
	s->writeEnterSection("initializationRequests");
	unsigned requestIndex = 0;
	for (const auto &[gid, ref] : initializationRequests)
	{
		s->writeEnterSection(requestIndex++);
		s->writeUint16(gid, "gid");
		s->writeUint32(ref.generation, "identity");
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
	s->writeLeaveSection();
}
void HiringPipeline::load(Game &game, GAGCore::InputStream *s, int versionMinor)
{
	this->game = &game;
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
			auto o = loadOutput(s, game);
			if (o->team != t || o->tick % Refresh != t % Refresh ||
				o->tick > std::numeric_limits<Uint32>::max() - Delay ||
				o->tick + Delay > game.stepCounter)
				throw std::runtime_error("Invalid published hiring team or deadline");
			published[t] = std::move(o);
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
		if ((versionMinor < FILE_FORMAT_VERSION_PROACTIVE_BUILDINGS && teams != expected) ||
			teams < expected || teams > unsigned(game.teamsCount()))
			throw std::runtime_error("Invalid hiring team count");
		unsigned expectedTeam = b->tick % Refresh;
		int previousTeam = -1;
		unsigned regularTeams = 0;
		s->readEnterSection("work");
		for (unsigned t = 0; t < teams; ++t)
		{
			s->readEnterSection(t);
			auto o = loadOutput(s, game);
			if (o->tick != b->tick || int(o->team) <= previousTeam ||
				(versionMinor < FILE_FORMAT_VERSION_PROACTIVE_BUILDINGS &&
				 o->team != expectedTeam) ||
				(o->team % Refresh != b->tick % Refresh && !o->buildings.empty()))
				throw std::runtime_error("Invalid hiring admission");
			expectedTeam += Refresh;
			previousTeam = o->team;
			regularTeams += o->team % Refresh == b->tick % Refresh;
			b->work.push_back({{}, o->team, o});
			if (versionMinor >= FILE_FORMAT_VERSION_HIRING_GRADIENTS)
			{
				const auto count = s->readCount("gradients");
				if (count > unsigned(Building::MAX_COUNT * BUILDING_GRADIENT_COUNT))
					throw std::runtime_error("Too many prepared hiring gradients");
				s->readEnterSection("preparedGradients");
				std::set<std::pair<unsigned, unsigned>> destinations;
				for (unsigned i = 0; i < count; ++i)
				{
					s->readEnterSection(i);
					auto gradient =
						HiringPlanner::PreparedRoute::load(game, s, o->team, b->tick, versionMinor);
					if (!destinations.emplace(gradient->building.gid, gradient->job.slot).second)
						throw std::runtime_error("Duplicate prepared hiring gradient");
					if (gradient->proactive)
						initializing.push_back({gradient->building, gradient->job.slot,
												gradient->job.epoch, gradient->job.generation});
					b->work.back().gradients.push_back(std::move(gradient));
					s->readLeaveSection();
				}
				s->readLeaveSection();
			}
			s->readLeaveSection();
		}
		s->readLeaveSection(2);
		if (regularTeams != expected)
			throw std::runtime_error("Missing regular hiring team");
		pending.push_back(std::move(b));
	}
	s->readLeaveSection();
	if (versionMinor >= FILE_FORMAT_VERSION_PROACTIVE_BUILDINGS)
	{
		const auto count = s->readCount("initializations");
		if (count > unsigned(Team::MAX_COUNT * Building::MAX_COUNT))
			throw std::runtime_error("Too many building initializations");
		s->readEnterSection("initializationRequests");
		int previous = -1;
		for (unsigned i = 0; i < count; ++i)
		{
			// Requests are ordered by gid; sections use contiguous ordinal indices.
			s->readEnterSection(i);
			BuildingRef ref;
			ref.gid = s->readUint16("gid");
			ref.generation = s->readUint32("identity");
			if (int(ref.gid) <= previous || !ref.generation ||
				Building::GIDtoTeam(ref.gid) >= game.teamsCount())
				throw std::runtime_error("Invalid building initialization");
			previous = ref.gid;
			initializationRequests.emplace(ref.gid, ref);
			s->readLeaveSection();
		}
		s->readLeaveSection();
	}
	s->readLeaveSection();
}
// Diagnostic digest of published contents and the logical pending schedule,
// never worker readiness. Not part of the game checksum: outputs are a pure
// function of the snapshot, and divergence reaches world state within ticks.
Uint32 HiringPipeline::checkSum() const
{
	if (!lastObserved && pending.empty() && initializationRequests.empty() &&
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
	for (const auto &o : published)
	{
		s.writeUint8(bool(o), "present");
		if (o)
			s.writeUint32(outputChecksum(*o), "output");
	}
	for (const auto &b : pending)
	{
		s.writeUint32(b->tick, "tick");
		s.writeUint32(b->due, "due");
		for (const auto &w : b->work)
			s.writeUint32(w.team, "team");
	}
	for (const auto &[gid, ref] : initializationRequests)
	{
		s.writeUint16(gid, "initialization");
		s.writeUint32(ref.generation, "identity");
	}
	return s.value;
}

HiringPlanner::Inputs
HiringPipeline::bindRoutes(Game &game, const SimulationSnapshot::Handle &world, unsigned teamMask,
						   const std::map<Uint16, BuildingRef> *initializations)
{
	if (world.tick != game.stepCounter)
		throw std::logic_error("Hiring input boundary mismatch");
	HiringPlanner::Inputs inputs;
	inputs.world = world.simulationView();
	auto routes = std::make_shared<std::vector<HiringPlanner::Inputs::Route>>();
	std::array<unsigned, Team::MAX_COUNT> movementClasses{};
	if (initializations && !initializations->empty())
	{
		const AIEngine::AIWorldView view(inputs.world);
		unsigned initializationTeams = 0;
		for (const auto &[gid, ref] : *initializations)
			initializationTeams |= 1u << Building::GIDtoTeam(gid);
		for (unsigned team = 0; team < unsigned(game.teamsCount()); ++team)
			if (initializationTeams & (1u << team))
				for (const auto &unit : view.unitsForTeam(team))
					if (!unit->isDead)
						movementClasses[team] |=
							1u << (unit->performance[FLY] ? AIR_MOVEMENT_CLASS : Map::swimClass(unit->performance[WALK], unit->performance[SWIM]));
	}
	for (const auto &b : inputs.world.entities->buildings)
	{
		const auto requested = initializations ? initializations->find(b.identity.gid)
											   : std::map<Uint16, BuildingRef>::const_iterator{};
		const bool initialize = initializations && requested != initializations->end() &&
								requested->second == b.identity;
		const bool hire = (!initializations || b.team % Refresh == world.tick % Refresh) &&
						  HiringPlanner::needsCandidates(b);
		if (!(teamMask & (1u << b.team)) || (!initialize && !hire))
			continue;
		auto *live = game.resolveBuilding(b.identity);
		if (!live)
			throw std::logic_error("Hiring capture lost building incarnation");
		// The owner borrows existing generations. Workers expand them under
		// the existing locks. Cold routes capture O(1) inputs here and are
		// initialized for creation requests or selected primary candidates on the worker.
		const auto &type = inputs.world.catalogs->buildings->at(b.typeNum).resolvedType;
		const bool footprint =
			(initialize && !type.isVirtual) ||
			std::any_of(std::begin(type.maxMaterial), std::begin(type.maxMaterial) + MaterialCount,
						[](int value) { return value > 0; }) ||
			std::any_of(std::begin(b.wishedMaterials),
						std::begin(b.wishedMaterials) + MaterialCount,
						[](int value) { return value > 0; });
		const bool clearing = type.attractsRole(0);
		const bool combat = type.attractsRole(1) || type.attractsRole(2);
		for (unsigned c = 0; c < MOVEMENT_CLASS_COUNT; ++c)
			if (((hire ? inputs.world.entities->availableMovementClasses[b.team] : 0) |
				 (initialize ? movementClasses[b.team] : 0)) &
				(1u << c))
				for (auto route :
					 {BuildingRoute::Footprint, BuildingRoute::Clearing, BuildingRoute::Combat})
				{
					if ((route == BuildingRoute::Footprint && !footprint) ||
						(route == BuildingRoute::Clearing && !clearing) ||
						(route == BuildingRoute::Combat && !combat))
						continue;
					const int slot = live->routeSlot(c, route);
					auto field = game.map.leaseBuildingRoute(live, c, route);
					const bool estimate = !field;
					// Stale fields keep the optimistic ranking fallback. A stale
					// unreachable result must not hide candidates whose live
					// admission would request a refresh.
					if (!field)
						field = live->leaseGradient(slot);
					std::shared_ptr<HiringPlanner::PreparedRoute> prepared;
					if (!field)
					{
						prepared = std::make_shared<HiringPlanner::PreparedRoute>();
						prepared->building = b.identity;
						prepared->proactive = initialize;
						auto &job = prepared->job;
						job.slot = slot;
						job.swim = c;
						job.route = route;
						if (initialize)
							live->supersedeGradient(slot); // Cancel an older queued cold request.
						job.epoch = live->refreshEpoch[slot];
						job.generation = game.map.topologyGeneration;
						job.captureTick = game.stepCounter;
						job.seed = gradient_preparation::captureBuildingSeed(*live, c, route);
						prepared->snapshot = world.buildingGradientView();
						field = prepared->field;
					}
					routes->push_back({b.identity, slot, std::move(field), estimate,
									   std::move(prepared), initialize});
				}
	}
	inputs.routes = std::move(routes);
	return inputs;
}
