// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPipeline.h"
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "Unit.h"
#include "SharedBuildingGradient.h"
#include "HiringGradientProbe.h"
#include "SnapshotGradient.h"
#include "BuildingGradientDepthPolicy.h"
#include "ai/observation/AIWorldView.h"
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
struct HiringPipeline::Work
{
	std::shared_ptr<const HiringPlanner::Inputs> input;
	unsigned team = 0;
	std::shared_ptr<const Output> output;
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
    HiringGradientProbe::Timer timer(HiringGradientProbe::HiringCpu);
    HiringGradientProbe::add(HiringGradientProbe::HiringJobs);
	auto &w = *static_cast<Work *>(context);
	w.output = std::make_shared<Output>(HiringPlanner::Planner{}.rank(*w.input, w.team, Capacity));
    const auto& o=*w.output;
    HiringGradientProbe::add(HiringGradientProbe::CandidateVisits,o.candidateVisits);
    HiringGradientProbe::add(HiringGradientProbe::HeuristicQueries,o.metrics.missingBuildingFields);
    HiringGradientProbe::add(HiringGradientProbe::SharedQueries,o.metrics.sharedQueries);
    {
        const AIEngine::AIWorldView world(w.input->world);
        for(const auto& building:o.buildings) for(const auto& list:building.jobs)
            for(unsigned candidateIndex=0; candidateIndex<list.candidates.size(); ++candidateIndex) {
                const auto& candidate=list.candidates[candidateIndex];
                const auto* unit=world.unitAtSlot(candidate.unit.gid);
                if(!unit || unit->identity!=candidate.unit || unit->performance[FLY]) continue;
                const auto route=list.key.job==Hiring::Job::Delivery || list.key.job==Hiring::Job::Fetch ? BuildingRoute::Footprint : list.key.job==Hiring::Job::Clear ? BuildingRoute::Clearing : BuildingRoute::Combat;
                const int slot=int(route)*SWIM_CLASS_COUNT+Map::swimClass(unit->performance[WALK],unit->performance[SWIM]);
                HiringGradientProbe::add(HiringGradientProbe::ShortlistedGround);
                bool found=false;
                for(const auto& bound:*w.input->routes) if(bound.building==building.building && bound.slot==slot) {
                    found=true;
                    if(HiringGradientProbe::mode()!=HiringGradientProbe::Baseline &&
                       (HiringGradientProbe::mode()!=HiringGradientProbe::TopOnly || candidateIndex<list.probePrimaryCount)) {
                        HiringGradientProbe::add(candidateIndex<list.probePrimaryCount ? HiringGradientProbe::WarmedPrimary : HiringGradientProbe::WarmedExtra);
                        if(bound.field->distance(unit->posX,unit->posY)==INT_MAX) HiringGradientProbe::add(HiringGradientProbe::ShortlistedUnreachable);
                    } else {
                        auto lock=SharedBuildingGradient::lock(bound.field);
                        const auto cell=std::size_t(unit->posY&(world.height-1))*world.width+(unit->posX&(world.width-1));
                        if(bound.field->search && !bound.field->search->resolved(cell))HiringGradientProbe::add(HiringGradientProbe::ShortlistedUnsettled);
                    }
                    break;
                }
                if(!found)HiringGradientProbe::add(HiringGradientProbe::ShortlistedMissing);
            }
    }
    if(HiringGradientProbe::mode()==HiringGradientProbe::Baseline)w.input.reset();
}
void HiringPipeline::observe(Game &game, const SimulationSnapshot::Handle &snapshot)
{
    probeGame=&game;
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
	if (!teamMask)
		return;
	if (pending.size() >= Delay)
		throw std::logic_error("Hiring publication horizon exceeded");
	auto input = std::make_shared<HiringPlanner::Inputs>(bindRoutes(game, snapshot, teamMask));
	auto batch = std::make_unique<Batch>();
	batch->tick = tick;
	batch->due = tick + Delay;
	for (unsigned t = tick % Refresh; t < unsigned(game.teamsCount()); t += Refresh)
		batch->work.push_back({input, t, {}});
	auto &b = *pending.emplace_back(std::move(batch));
	std::array<ComputeExecutor::Group, Team::MAX_COUNT> groups;
	for (unsigned i = 0; i < b.work.size(); ++i)
		groups[i] = {1, {compute, &b.work[i]}, ComputeExecutor::HiringLaneBase + b.work[i].team};
	b.ticket = executor.submit(std::span(groups).first(b.work.size()), ComputeExecutor::boundaryDue(b.due),
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
		for (const auto &w : b.work)
		{
			if (!w.output)
				throw std::logic_error("Missing hiring result");
            if(w.input && probeGame && HiringGradientProbe::mode()!=HiringGradientProbe::Baseline)
                for(const auto& route:*w.input->routes) {
                    auto& field=*route.field;
                    if(!field.probePrivate || Building::GIDtoTeam(route.building.gid)!=int(w.team) || !field.values) continue;
                    auto* live=probeGame->resolveBuilding(route.building);
                    const int slot=route.slot,swim=slot%SWIM_CLASS_COUNT;
                    const auto kind=BuildingRoute(slot/SWIM_CLASS_COUNT);
                    if(!live || live->refreshEpoch[slot]!=field.probeEpoch || probeGame->map.topologyGeneration!=field.probeGeneration ||
                        (live->globalGradient[slot] && live->lastGlobalGradientUpdateStepCounter[slot]>=field.probeTick)) {
                        HiringGradientProbe::add(HiringGradientProbe::Discarded);continue;
                    }
                    auto guard=SharedBuildingGradient::lock(live->sharedGradient[slot]);
                    live->retireSharedGradient(slot);
                    probeGame->map.recycleBuildingGradientBuffer(live->globalGradient[slot]);
                    recycleBuildingGradientSearch(std::move(live->globalGradientSearch[slot]));
                    live->supersedeGradient(slot);
                    live->globalGradient[slot]=field.retiredValues.release();
                    live->globalGradientSearch[slot]=std::move(field.retiredSearch);
                    live->sharedGradient[slot]=route.field;
                    live->dirtyGradient[slot]=false;
                    live->gradientGeneration[slot]=field.probeGeneration;
                    live->lastGlobalGradientUpdateStepCounter[slot]=field.probeTick;
                    live->globalGradientUsedStep[slot]=std::max(live->globalGradientUsedStep[slot],field.probeTick);
                    live->setLocked(live->routeAccess(swim,kind),field.probeLocked);
                    if(kind==BuildingRoute::Clearing)live->anyResourceToClear[swimAccessVariant(swim)]=field.probeResource;
                    field.probePrivate=false;
                    HiringGradientProbe::add(HiringGradientProbe::Applied);
                }
            if(probeGame) for(const auto& lists:w.output->buildings) {
                auto* live=probeGame->resolveBuilding(lists.building);
                if(!live || live->typeNum!=lists.type)continue;
                for(const auto& list:lists.jobs)for(unsigned candidateIndex=0;candidateIndex<list.candidates.size();++candidateIndex) {
                    const auto& candidate=list.candidates[candidateIndex];
                    const bool primary=candidateIndex<list.probePrimaryCount;
                    auto* unit=probeGame->resolveUnit(candidate.unit);
                    if(!unit || unit->performance[FLY])continue;
                    HiringGradientProbe::add(HiringGradientProbe::PublishedGround);
                    if(primary)HiringGradientProbe::add(HiringGradientProbe::PublishedPrimaryGround);
                    const auto kind=list.key.job==Hiring::Job::Delivery || list.key.job==Hiring::Job::Fetch ? BuildingRoute::Footprint : list.key.job==Hiring::Job::Clear ? BuildingRoute::Clearing : BuildingRoute::Combat;
                    const int slot=live->routeSlot(unit->swimClass(),kind);
                    auto guard=SharedBuildingGradient::lock(live->sharedGradient[slot]);
                    if(!live->globalGradient[slot]) {HiringGradientProbe::add(HiringGradientProbe::PublishedMissing);if(primary)HiringGradientProbe::add(HiringGradientProbe::PublishedPrimaryMissing);}
                    else {
                        if(live->dirtyGradient[slot] || live->gradientGeneration[slot]!=probeGame->map.topologyGeneration)HiringGradientProbe::add(HiringGradientProbe::PublishedStale);
                        if(live->globalGradientSearch[slot] && !live->globalGradientSearch[slot]->resolved(probeGame->map.coordToIndex(unit->posX,unit->posY))) {HiringGradientProbe::add(HiringGradientProbe::PublishedUnsettled);if(primary)HiringGradientProbe::add(HiringGradientProbe::PublishedPrimaryUnsettled);}
                    }
                }
            }
			published[w.team] = w.output;
			++metrics.published;
		}
		pending.pop_front();
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
		if (teams != expected)
			throw std::runtime_error("Invalid hiring team count");
		unsigned expectedTeam = b->tick % Refresh;
		s->readEnterSection("work");
		for (unsigned t = 0; t < teams; ++t)
		{
			s->readEnterSection(t);
			auto o = loadOutput(s, game);
			if (o->tick != b->tick || o->team != expectedTeam)
				throw std::runtime_error("Invalid hiring admission");
			expectedTeam += Refresh;
			b->work.push_back({{}, o->team, o});
			s->readLeaveSection();
		}
		s->readLeaveSection(2);
		pending.push_back(std::move(b));
	}
	s->readLeaveSection(2);
}
// Diagnostic digest of published contents and the logical pending schedule,
// never worker readiness. Not part of the game checksum: outputs are a pure
// function of the snapshot, and divergence reaches world state within ticks.
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
	return s.value;
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
		if (!(teamMask & (1u << b.team)) || !HiringPlanner::needsCandidates(b))
			continue;
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
				for (auto route : {BuildingRoute::Footprint, BuildingRoute::Clearing, BuildingRoute::Combat})
				{
					if ((route == BuildingRoute::Footprint && !footprint) ||
						(route == BuildingRoute::Clearing && !clearing) ||
						(route == BuildingRoute::Combat && !combat))
						continue;
                    auto field=game.map.leaseBuildingRoute(live,c,route);
                    const bool originalMissing=!field;
                    if(!field && HiringGradientProbe::mode()!=HiringGradientProbe::Baseline && live->globalGradient[live->routeSlot(c,route)])
                        field=live->leaseGradient(live->routeSlot(c,route));
                    if(!field && HiringGradientProbe::mode()!=HiringGradientProbe::Baseline) {
                        field=std::make_shared<SharedBuildingGradient>();
                        const auto slot=live->routeSlot(c,route);
                        field->probePrivate=true;field->probeEpoch=live->refreshEpoch[slot];
                        field->probeGeneration=game.map.topologyGeneration;field->probeTick=game.stepCounter;
                        const auto seed=gradient_preparation::captureBuildingSeed(*live,c,route);
                        field->probeInitialize=[snapshot=world.buildingGradientView(),seed](SharedBuildingGradient& f) {
                            HiringGradientProbe::Timer timer(HiringGradientProbe::PrivateBuildCpu);
                            HiringGradientProbe::add(HiringGradientProbe::PrivateBuilds);
                            f.width=snapshot.width;f.height=snapshot.height;
                            f.retiredValues.reset(new Uint16[std::size_t(f.width)*f.height]);
                            f.retiredSearch=acquireBuildingGradientSearch();
                            const auto result=gradient_preparation::buildBuilding(seed,snapshot,BuildingGradientSearch::Inputs::ofSnapshot(snapshot,seed.swim),f.retiredValues.get(),*f.retiredSearch,BuildingGradientDepth::MIN_DEPTH);
                            f.probeLocked=result.locked;f.probeResource=result.resourceState;
                            if(result.locked)f.retiredSearch.reset();
                            f.values=f.retiredValues.get();f.search=f.retiredSearch.get();
                        };
                    }
                    if(field) {
                        const bool estimate=originalMissing && (HiringGradientProbe::mode()==HiringGradientProbe::Shortlist || HiringGradientProbe::mode()==HiringGradientProbe::TopOnly || !field->probePrivate);
                        routes->push_back({b.identity,live->routeSlot(c,route),std::move(field),estimate});
                    }
				}
	}
	inputs.routes = std::move(routes);
	return inputs;
}
