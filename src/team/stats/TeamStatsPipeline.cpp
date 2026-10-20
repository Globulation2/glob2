// SPDX-License-Identifier: GPL-3.0-or-later
#include "TeamStatsPipeline.h"
#include "Game.h"
#include "Team.h"
#include <ChecksumStream.h>
#include <BinaryStream.h>
#include <cstdlib>
#include <algorithm>
#include <limits>
namespace
{
// Format 159 isolates each team in text saves; binary section markers have no bytes.
constexpr int TeamSectionsVersion=159;
static_assert(ComputeExecutor::StatisticsHorizon >= 8);
static_assert(ComputeExecutor::StatisticsLaneBase >= Team::MAX_COUNT);
static_assert(ComputeExecutor::StatisticsLaneBase < ComputeExecutor::Lanes);
} // namespace
TeamStatsPipeline::TeamStatsPipeline(ComputeExecutor &e)
	: executor(e)
{
}
TeamStatsPipeline::~TeamStatsPipeline()
{
	try
	{
		drain();
	}
	catch (...)
	{ /* joins have completed; failure is surfaced by explicit owner barriers */
	}
}
void TeamStatsPipeline::compute(void *context, size_t)
{
	auto &batch = *static_cast<Pending *>(context);
	const auto snapshot = batch.snapshot;
	for (unsigned team = 0; team < batch.samples.size(); ++team)
	{
		auto storage = std::move(batch.samples[team].frame);
		auto sample = computeTeamStatsSample(snapshot, team, batch.phases[team] == 31,
											 batch.smoothingWindows[team], true);
		TeamStats::completeMeasurements(sample, *(*batch.previousFrames)[team]);
		if (!storage || storage.use_count() != 1)
			storage = std::make_shared<GameplayMeasurements>();
		*storage = std::move(sample.measurements);
		sample.frame = std::move(storage);
		(*batch.previousFrames)[team] = sample.frame;
		// The frame owns the measurements. Moving the whole sample would also
		// copy its unused inline measurement arrays and non-aggregate stat.
		auto &result = batch.samples[team];
		result.tick = sample.tick;
		result.prestige = sample.prestige;
		result.aggregate = sample.aggregate;
		result.periodic = sample.periodic;
		result.finalized = sample.finalized;
		result.smoothing = std::move(sample.smoothing);
		if (sample.aggregate)
		{
			result.stat = std::move(sample.stat);
			result.eligible = std::move(sample.eligible);
		}
		result.coverage = std::move(sample.coverage);
		result.frame = std::move(sample.frame);
	}
	batch.snapshot = {};
}
bool TeamStatsPipeline::publishDue(Game &game)
{
	if (failure)
		std::rethrow_exception(failure);
	bool changed = false;
	while (!pending.empty() && pending.front()->due <= game.stepCounter)
	{
		auto &batch = *pending.front();
		if (batch.due != game.stepCounter)
			throw std::logic_error("Statistics deadline was skipped");
		// join() also executes the required serial fallback on zero-worker hosts.
		if (!batch.ticket.empty())
		{
			try
			{
				executor.join(batch.ticket, {JoinReason::TickDeadline, game.stepCounter});
			}
			catch (...)
			{
				failure = std::current_exception();
				throw;
			}
			batch.ticket = {};
		}
		batch.snapshot = {};
		for (unsigned t = 0; t < batch.samples.size(); ++t)
		{
			if (batch.samples[t].periodic)
				game.teams[t]->stats.printEconomy(game.teams[t], batch.tick);
			game.teams[t]->stats.publishFrame(batch.samples[t], batch.due,
											  game.teams[t]->race.getCatalog());
			if (batch.samples[t].periodic && getenv("GLOB2_TEAM_TIMELINE"))
				game.teams[t]->stats.printMeasurements(t);
		}
		++metrics.published;
		spare = std::move(pending.front());
		pending.pop_front();
		changed = true;
	}
	return changed;
}
void TeamStatsPipeline::prepareBoundary(Game &game, const SimulationSnapshot::Handle &captured)
{
	if (failure)
		std::rethrow_exception(failure);
	if (!needsSample(game.stepCounter))
		return;
	if (captured.tick != game.stepCounter || !captured.entities || !matchesWorld(captured.worldIdentity))
		throw std::logic_error("Statistics input does not match the completed observation");
	worldIdentity = captured.worldIdentity;
	if (!pending.empty() &&
		(pending.back()->due - pending.back()->tick != game.gameHeader.getTeamStatsDelay() &&
		 pending.back()->due - pending.back()->tick != std::max(1u, game.gameHeader.getTeamStatsDelay())))
		throw std::logic_error("Cannot change active statistics delay");
	if (lastSubmitted && game.stepCounter != *lastSubmitted + 1)
		throw std::logic_error("Statistics observation tick was skipped");
	if (phases.empty())
		for (int t = 0; t < game.teamsCount(); ++t)
		{
			game.teams[t]->stats.configureUnits(game.unitCatalog().size());
			phases.push_back(game.teams[t]->stats.samplingPhase());
		}
	if (previousFrames.empty())
		for (int t = 0; t < game.teamsCount(); ++t)
			previousFrames.push_back(
				std::make_shared<GameplayMeasurements>(game.teams[t]->stats.measurements()));
	const auto delay = std::max(1u, game.gameHeader.getTeamStatsDelay());
	if (game.stepCounter > std::numeric_limits<Uint32>::max() - delay)
		throw std::overflow_error("Statistics deadline overflow");
	auto batch = spare ? std::move(spare) : std::make_unique<Pending>();
	batch->tick = game.stepCounter;
	batch->due = batch->tick + delay;
	batch->snapshot = captured.statisticsView();
	batch->samples.resize(phases.size());
	batch->phases = phases;
	batch->previousFrames = &previousFrames;
	batch->smoothingWindows.resize(phases.size());
	for (unsigned t = 0; t < phases.size(); ++t)
	{
		if (phases[t] == 31)
			game.teams[t]->stats.captureSmoothingWindow(batch->smoothingWindows[t]);
		else
			batch->smoothingWindows[t].clear();
	}
	// Retain the callback context before submitting: deque allocation must not
	// fail after a worker has started using the batch.
	pending.push_back(std::move(batch));
	auto &submitted = *pending.back();
	try
	{
		const ComputeExecutor::Group group{
			1, {compute, &submitted}, ComputeExecutor::StatisticsLaneBase};
		submitted.ticket =
			executor.submit(std::span(&group, 1), ComputeExecutor::boundaryDue(submitted.due),
							{ComputeProducer::TeamStatistics, submitted.tick, submitted.due});
	}
	catch (...)
	{
		// Submission failures are terminal; do not retry a partially admitted chain.
		failure = std::current_exception();
		throw;
	}
	for (auto &phase : phases)
		phase = (phase + 1) % 32;
	lastSubmitted = game.stepCounter;
	++metrics.submitted;
	metrics.peakPending = std::max<Uint64>(metrics.peakPending, pending.size());
}
void TeamStatsPipeline::drain(JoinReason reason)
{
	for (auto &batch : pending)
	{
		if (!batch->ticket.empty())
		{
			try
			{
				executor.join(batch->ticket, {reason, ComputeTelemetry::NoTick});
			}
			catch (...)
			{
				if (!failure)
					failure = std::current_exception();
			}
			batch->ticket = {};
		}
		batch->snapshot = {};
	}
	if (failure)
		std::rethrow_exception(failure);
}
void TeamStatsPipeline::save(GAGCore::OutputStream *s)
{
	drain(JoinReason::Save);
	s->writeEnterSection("TeamStatsPipeline");
	s->writeUint8(lastSubmitted.has_value(), "hasLast");
	if (lastSubmitted)
		s->writeUint32(*lastSubmitted, "last");
	s->writeUint32(phases.size(), "teams");
	for (unsigned t=0;t<phases.size();++t) {
        s->writeEnterSection(t); s->writeUint32(phases[t],"phase"); s->writeLeaveSection();
    }
	s->writeUint32(pending.size(), "pending");
	for (unsigned i = 0; i < pending.size(); ++i)
	{
		s->writeEnterSection(i);
		const auto &b = *pending[i];
		s->writeUint32(b.tick, "tick");
		s->writeUint32(b.due, "due");
		for (unsigned t=0;t<b.samples.size();++t) {
            s->writeEnterSection(t);
            TeamStats::saveSample(s,b.samples[t]);
            s->writeLeaveSection();
        }
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
void TeamStatsPipeline::load(Game &game, GAGCore::InputStream *s, int versionMinor)
{
	worldIdentity = game.map.identity();
	GAGCore::BinaryInputStream::CheckedReads checked(s);
	s->readEnterSection("TeamStatsPipeline");
	if (s->readUint8("hasLast"))
		lastSubmitted = s->readUint32("last");
	const auto teams = s->readCount("teams");
	if (teams && teams != unsigned(game.teamsCount()))
		throw std::runtime_error("Invalid statistics team count");
	for (unsigned t = 0; t < teams; ++t)
	{
		if (versionMinor>=TeamSectionsVersion) s->readEnterSection(t);
        auto phase = s->readUint32("phase");
        if (versionMinor>=TeamSectionsVersion) s->readLeaveSection();
		if (phase >= 32)
			throw std::runtime_error("Invalid statistics phase");
		phases.push_back(phase);
	}
	for (unsigned t = 0; t < teams; ++t)
		previousFrames.push_back(
			std::make_shared<GameplayMeasurements>(game.teams[t]->stats.measurements()));
	const auto count = s->readCount("pending");
	if (count > 9 || (count && (!lastSubmitted || !teams)))
		throw std::runtime_error("Invalid statistics queue");
	for (unsigned i = 0; i < count; ++i)
	{
		s->readEnterSection(i);
		auto b = std::make_unique<Pending>();
		b->tick = s->readUint32("tick");
		b->due = s->readUint32("due");
		// Format 155 carried a redundant input digest. Preserve its stream layout.
		if (versionMinor == 155)
			(void)s->readUint32("inputChecksum");
		if (b->tick > b->due || (b->due - b->tick != game.gameHeader.getTeamStatsDelay() &&
			 b->due - b->tick != std::max(1u, game.gameHeader.getTeamStatsDelay())) ||
			b->due < game.stepCounter || b->tick > *lastSubmitted ||
			(!pending.empty() && b->tick != pending.back()->tick + 1))
			throw std::runtime_error("Invalid statistics deadline");
		for (unsigned t = 0; t < teams; ++t)
		{
			if (versionMinor>=TeamSectionsVersion) s->readEnterSection(t);
            auto sample = TeamStats::loadSample(s, versionMinor,game.mapHeader.privateAdaptive174);
            if (versionMinor>=TeamSectionsVersion) s->readLeaveSection();
			if (sample.tick != b->tick ||
				sample.measurementFrame().births.size() != game.unitCatalog().size() ||
				sample.measurementFrame().variants.size() != game.buildingsTypes.size())
				throw std::runtime_error("Invalid statistics sample");
			if (!sample.frame)
			{
				TeamStats::completeMeasurements(sample, *previousFrames[t]);
				sample.frame =
					std::make_shared<GameplayMeasurements>(std::move(sample.measurements));
			}
			previousFrames[t] = sample.frame;
			b->samples.push_back(std::move(sample));
		}
		pending.push_back(std::move(b));
		s->readLeaveSection();
	}
	if (bool(lastSubmitted) != bool(teams))
		throw std::runtime_error("Invalid statistics sampling state");
	if (count > game.gameHeader.getTeamStatsDelay() + 1 ||
		(count && pending.back()->tick != *lastSubmitted))
		throw std::runtime_error("Invalid statistics queue horizon");
	for (unsigned t = 0; t < teams; ++t)
	{
		auto phase = game.teams[t]->stats.samplingPhase();
		for (const auto &batch : pending)
		{
			const auto &sample = batch->samples[t];
			const auto types = game.unitCatalog().size();
			if (sample.aggregate != (phase == 31) || sample.smoothing.isFree.size() != types ||
				(sample.aggregate &&
				 (sample.stat.numberUnitPerType.size() != types ||
				  sample.stat.isFree.size() != types ||
				  sample.stat.upgradeStatePerType.size() != types ||
				  sample.stat.buildingCountByVariant.size() != game.buildingsTypes.size() ||
				  sample.eligible.size() != types)))
				throw std::runtime_error("Invalid pending statistics phase or catalog");
			phase = (phase + 1) % 32;
		}
		if (phase != phases[t])
			throw std::runtime_error("Invalid restored statistics progression");
	}
	if (lastSubmitted && *lastSubmitted > game.stepCounter)
		throw std::runtime_error("Invalid statistics observation boundary");
	s->readLeaveSection();
}
Uint32 TeamStatsPipeline::checkSum(bool heavy)
{
	if (heavy)
		drain(JoinReason::Verification);
	Uint32 cs = lastSubmitted.value_or(0) * 16777619u + lastSubmitted.has_value();
	for (auto phase : phases)
		cs = (cs ^ phase) * 16777619u;
	for (const auto &b : pending)
	{
		cs = (cs ^ b->tick) * 16777619u;
		cs = (cs ^ b->due) * 16777619u;
		if (heavy)
		{
			GAGCore::ChecksumStream stream;
			for (const auto &sample : b->samples)
				TeamStats::saveSample(&stream, sample);
			cs = (cs ^ stream.value) * 16777619u;
		}
	}
	return cs;
}
