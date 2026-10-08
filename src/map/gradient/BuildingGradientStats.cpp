// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingGradientStats.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <ostream>

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "Map.h"
#include "ResourceSeedCache.h"
#include "Team.h"
#include "field/GradientConstants.h"

namespace
{
constexpr const char *ROUTE_NAMES[] = {"footprint", "clearing", "combat"};
}

bool BuildingGradientStats::enabledByEnvironment()
{
	const char *value = std::getenv("GLOB2_GRADIENT_STATS");
	return value && *value && std::strcmp(value, "0") != 0;
}

const char *BuildingGradientStats::name(Reason reason)
{
	switch (reason)
	{
	case Reason::Null: return "null";
	case Reason::Dirty: return "dirty";
	case Reason::Generation: return "generation";
	case Reason::Clearing: return "clearing";
	case Reason::Stuck: return "stuck";
	default: return "other";
	}
}

const char *BuildingGradientStats::name(Verdict verdict)
{
	switch (verdict)
	{
	case Verdict::KeepTeamFilter: return "keep_team_filter";
	case Verdict::KeepNoop: return "keep_noop";
	case Verdict::KeepUntouched: return "keep_untouched";
	case Verdict::Rebuild: return "rebuild";
	case Verdict::Overflow: return "overflow";
	default: return "none";
	}
}

const char *BuildingGradientStats::name(Event event)
{
	switch (event)
	{
	case Event::Rebuild: return "rebuild";
	case Event::Drop: return "drop";
	case Event::Evict: return "evict";
	default: return "end";
	}
}

void BuildingGradientStats::push(const JournalEntry &entry)
{
	if (journal.size() < JOURNAL_CAPACITY)
		journal.resize(JOURNAL_CAPACITY);
	if (journalSize == JOURNAL_CAPACITY)
		lostGeneration = std::max(lostGeneration, journal[journalHead].generation);
	else
		++journalSize;
	journal[journalHead] = entry;
	journalHead = (journalHead + 1) % JOURNAL_CAPACITY;
}

void BuildingGradientStats::recordBuilding(std::uint32_t generation, std::size_t cell, std::uint16_t before,
										   std::uint16_t after)
{
	++buildingCells;
	pendingDetail = true;
	push({generation, std::uint32_t(cell), std::uint32_t(before) << 16 | after, Change::Building, false});
}

void BuildingGradientStats::recordBuildingNoop(std::uint32_t generation, std::size_t cell)
{
	++buildingNoops;
	pendingDetail = true;
	push({generation, std::uint32_t(cell), 0, Change::BuildingNoop, false});
}

void BuildingGradientStats::recordForbidden(std::uint32_t generation, std::size_t cell, std::uint32_t teamMask)
{
	++forbiddenCells;
	pendingDetail = true;
	push({generation, std::uint32_t(cell), teamMask, Change::Forbidden, false});
}

void BuildingGradientStats::recordBump(std::uint32_t generation)
{
	++bumps;
	if (!pendingDetail)
		++undetailedBumps;
	push({generation, 0, 0, Change::Bump, pendingDetail});
	pendingDetail = false;
}

void BuildingGradientStats::resetJournal(std::uint32_t generation)
{
	++journalResets;
	journalHead = journalSize = 0;
	lostGeneration = generation;
	pendingDetail = false;
}

void BuildingGradientStats::resourceSeedChanged(unsigned flags)
{
	if (flags & ResourceSeedCache::Immobile)
		++immobileChanges;
	if (flags & ResourceSeedCache::Resource)
		++resourceChanges;
}

BuildingGradientStats::Verdict BuildingGradientStats::classify(const Map &map, const Building &building, int slot,
															   int access, const std::uint16_t *field,
															   std::uint32_t fieldGeneration,
															   std::uint32_t currentGeneration) const
{
	if (fieldGeneration == currentGeneration)
		return Verdict::None;
	if (fieldGeneration < lostGeneration)
		return Verdict::Overflow;
	const std::uint32_t teamMask = building.owner->me;
	const std::uint16_t gid = building.gid;
	// A locked footprint field holds only its initialization: any cell may
	// reopen its perimeter, so cell-local reasoning does not apply.
	const bool lockedField = building.locked[access] || !field;
	const int w = map.getW(), mask = map.getMaskW();
	const int shift = std::countr_zero(unsigned(w));
	bool noop = false, filtered = false, untouched = false, sawBump = false;
	auto cellUntouched = [&](std::uint32_t cell)
	{
		if (lockedField)
			return false;
		const int x = int(cell) & mask, y = int(cell >> shift);
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				// At start value: obstacle or never reached by the search.
				if (field[map.coordToIndex(x + dx, y + dy)] > GRADIENT_UNREACHABLE)
					return false;
		return true;
	};
	(void)slot;
	for (std::size_t n = 0; n < journalSize; ++n)
	{
		const auto &entry = journal[(journalHead + JOURNAL_CAPACITY - 1 - n) % JOURNAL_CAPACITY];
		if (entry.generation <= fieldGeneration)
			break;
		if (entry.generation > currentGeneration)
			continue; // details of a bump still in progress
		switch (entry.kind)
		{
		case Change::Bump:
			sawBump = true;
			// Bump sites without cell detail (terrain commits, resource
			// registry replacement) are global changes.
			if (!entry.detailed)
				return Verdict::Rebuild;
			break;
		case Change::BuildingNoop:
			noop = true;
			break;
		case Change::Forbidden:
			if (!(entry.aux & teamMask))
				filtered = true;
			else if (cellUntouched(entry.cell))
				untouched = true;
			else
				return Verdict::Rebuild;
			break;
		case Change::Building:
			if ((entry.aux >> 16) == gid || (entry.aux & 0xFFFF) == gid)
				return Verdict::Rebuild;
			if (cellUntouched(entry.cell))
				untouched = true;
			else
				return Verdict::Rebuild;
			break;
		}
	}
	if (!sawBump)
		return Verdict::Overflow; // generation moved without journal history
	if (untouched)
		return Verdict::KeepUntouched;
	if (filtered)
		return Verdict::KeepTeamFilter;
	if (noop)
		return Verdict::KeepNoop;
	return Verdict::Rebuild;
}

BuildingGradientStats::Context BuildingGradientStats::context(const Building &building)
{
	Context value;
	value.known = true;
	if (const BuildingType *type = building.type)
	{
		value.level = std::int16_t(type->level);
		value.site = type->isBuildingSite != 0;
		if (value.site)
		{
			int delivered = 0, needed = 0;
			for (int r = 0; r < MaterialSlotCount; ++r)
			{
				delivered += std::max(0, building.localMaterials[r]);
				needed += std::max(0, type->maxMaterial[r]);
			}
			value.progress = std::int8_t(needed > 0 ? std::min(3, delivered * 4 / needed) : 3);
		}
	}
	value.construction = std::uint8_t(building.constructionResultState);
	value.units = std::uint16_t(std::min<std::size_t>(building.owner->liveUnits.size(), UINT16_MAX));
	value.buildings = std::uint16_t(std::min<std::size_t>(building.owner->liveBuildings.size(), UINT16_MAX));
	return value;
}

std::uint16_t BuildingGradientStats::typeIndex(const Building &building)
{
	const std::string &name = building.type ? building.type->type : std::string("unknown");
	auto found = typeIndices.find(name);
	if (found != typeIndices.end())
		return found->second;
	const auto index = std::uint16_t(typeNames.size());
	typeNames.push_back(name);
	typeIndices.emplace(name, index);
	return index;
}

std::uint64_t BuildingGradientStats::key(const Building &building, int slot)
{
	return (std::uint64_t(building.owner->teamNumber) << 32 | std::uint64_t(building.gid) << 8) | std::uint64_t(slot);
}

BuildingGradientStats::Row BuildingGradientStats::previousRow(const Building &building, int slot, Event event,
															   std::uint32_t tick, bool hasPrevious, int access)
{
	Row row;
	row.tick = tick;
	row.gid = building.gid;
	row.team = std::uint8_t(building.owner->teamNumber);
	row.route = std::uint8_t(slot / SWIM_CLASS_COUNT);
	row.swim = std::uint8_t(slot % SWIM_CLASS_COUNT);
	row.event = event;
	row.type = typeIndex(building);
	row.hasPrevious = hasPrevious;
	if (!hasPrevious)
		return row;
	row.age = std::int64_t(tick) - std::int64_t(building.lastGlobalGradientUpdateStepCounter[slot]);
	row.prevLocked = building.locked[access];
	if (const auto &search = building.globalGradientSearch[slot])
	{
		row.prevSearch = true;
		row.prevComplete = search->complete();
		row.prevSettledCost = search->settledCost();
		row.prevPopped = search->poppedEntries();
		row.prevQueries = search->queries;
		row.prevExtensions = search->extensions;
		for (int i = 0; i < DEPTH_BINS; ++i)
			row.poppedAtDepth[i] = std::uint32_t(std::min<std::uint64_t>(search->poppedAtDepth[i], UINT32_MAX));
	}
	else
		row.prevComplete = !row.prevLocked; // loaded complete, or locked without a search
	closeLifetime(row, building, slot);
	return row;
}

void BuildingGradientStats::closeLifetime(Row &row, const Building &building, int slot)
{
	const auto found = lifetimes.find(key(building, slot));
	poppedTotal += row.prevPopped;
	if (found == lifetimes.end())
	{
		poppedUnknownLifetime += row.prevPopped;
		return;
	}
	row.lifetimeKnown = true;
	row.context = found->second.context;
	row.lifetimeReason = found->second.reason;
	row.lifetimeVerdict = found->second.verdict;
	poppedByReason[unsigned(row.lifetimeReason)] += row.prevPopped;
	poppedByVerdict[unsigned(row.lifetimeVerdict)] += row.prevPopped;
	lifetimes.erase(found);
}

void BuildingGradientStats::fieldRebuilding(const Map &map, const Building &building, int slot, int access,
											std::uint32_t tick, std::uint32_t topologyGeneration)
{
	const Reason reason = pendingReason;
	pendingReason = Reason::Other;
	const bool hasPrevious = reason != Reason::Null;
	const std::uint32_t fieldGeneration = building.gradientGeneration[slot];
	Verdict verdict = Verdict::None;
	if (reason == Reason::Generation)
		verdict = classify(map, building, slot, access, building.globalGradient[slot], fieldGeneration,
						   topologyGeneration);
	if (reason == Reason::Dirty && fieldGeneration != topologyGeneration)
		++dirtyWithGeneration;
	Row row = previousRow(building, slot, Event::Rebuild, tick, hasPrevious, access);
	row.reason = reason;
	row.verdict = verdict;
	++rebuildCounts[unsigned(reason)];
	++verdictCounts[unsigned(verdict)];
	++eventCounts[unsigned(Event::Rebuild)];
	rowList.push_back(row);
	mapWidth = map.getW();
	mapHeight = map.getH();
	lifetimes[key(building, slot)] = {tick, reason, verdict, context(building)};
}

void BuildingGradientStats::fieldReleased(const Building &building, int slot, Event event, std::uint32_t tick)
{
	if (!building.globalGradient[slot])
		return;
	const int access = (slot / SWIM_CLASS_COUNT) * SWIM_VARIANT_COUNT + (slot % SWIM_CLASS_COUNT > 0);
	Row row = previousRow(building, slot, event, tick, true, access);
	row.reason = Reason::Other;
	++eventCounts[unsigned(event)];
	rowList.push_back(row);
}

void BuildingGradientStats::finish(const Game &game)
{
	for (int team = 0; team < game.teamsCount(); ++team)
	{
		const Team *owner = game.teams[team];
		if (!owner || !owner->myBuildings)
			continue;
		for (int id = 0; id < Building::MAX_COUNT; ++id)
			if (const Building *building = owner->myBuildings[id])
				for (int slot = 0; slot < BUILDING_GRADIENT_COUNT; ++slot)
					fieldReleased(*building, slot, Event::End, game.stepCounter);
	}
}

void BuildingGradientStats::writeCsv(std::ostream &out) const
{
	out << "tick,team,gid,type,route,swim,event,reason,shadow_verdict,lifetime_reason,lifetime_verdict,age,"
		   "prev_search,prev_locked,prev_complete,prev_settled_cost,prev_settled_tiles,prev_popped,prev_queries,"
		   "prev_extensions";
	for (int i = 0; i < DEPTH_BINS; ++i)
		out << ",popped_at_depth_" << i;
	out << ",width,height,level,is_site,construction_state,progress,team_units,team_buildings\n";
	for (const auto &row : rowList)
	{
		out << row.tick << ',' << int(row.team) << ',' << row.gid << ',' << typeNames[row.type] << ','
			<< ROUTE_NAMES[row.route] << ',' << int(row.swim) << ',' << name(row.event) << ','
			<< (row.event == Event::Rebuild ? name(row.reason) : "") << ','
			<< (row.verdict == Verdict::None ? "" : name(row.verdict)) << ','
			<< (row.lifetimeKnown ? name(row.lifetimeReason) : "") << ','
			<< (row.lifetimeKnown && row.lifetimeVerdict != Verdict::None ? name(row.lifetimeVerdict) : "") << ',';
		if (!row.hasPrevious)
		{
			out << ",,,,,,,,";
			for (int i = 0; i < DEPTH_BINS; ++i)
				out << ',';
			out << ',' << mapWidth << ',' << mapHeight << ",,,,,,\n";
			continue;
		}
		out << row.age << ',' << int(row.prevSearch) << ',' << int(row.prevLocked) << ',' << int(row.prevComplete)
			<< ',';
		if (row.prevSearch)
			out << row.prevSettledCost << ',' << row.prevSettledCost / GRADIENT_STEP;
		else
			out << ',';
		out << ',' << row.prevPopped << ',' << row.prevQueries << ',' << row.prevExtensions;
		for (int i = 0; i < DEPTH_BINS; ++i)
			out << ',' << row.poppedAtDepth[i];
		out << ',' << mapWidth << ',' << mapHeight;
		if (row.context.known)
		{
			static constexpr const char *CONSTRUCTION[] = {"none", "new", "upgrade", "repair"};
			out << ',' << row.context.level << ',' << int(row.context.site) << ','
				<< CONSTRUCTION[std::min<unsigned>(row.context.construction, 3)] << ',';
			if (row.context.progress >= 0)
				out << int(row.context.progress);
			out << ',' << row.context.units << ',' << row.context.buildings;
		}
		else
			out << ",,,,,,";
		out << '\n';
	}
}

void BuildingGradientStats::writeJson(std::ostream &out) const
{
	auto object = [&](const char *label, const auto &values, auto count, auto nameOf, bool skipNone)
	{
		out << '"' << label << "\":{";
		bool comma = false;
		for (unsigned i = 0; i < unsigned(count); ++i)
		{
			if (skipNone && i == 0)
				continue;
			if (comma)
				out << ',';
			comma = true;
			out << '"' << nameOf(i) << "\":" << values[i];
		}
		out << '}';
	};
	auto reasonName = [](unsigned i) { return name(Reason(i)); };
	auto verdictName = [](unsigned i) { return name(Verdict(i)); };
	auto eventName = [](unsigned i) { return name(Event(i)); };
	std::uint64_t kept = 0, keptPopped = 0;
	for (auto v : {Verdict::KeepTeamFilter, Verdict::KeepNoop, Verdict::KeepUntouched})
	{
		kept += verdictCounts[unsigned(v)];
		keptPopped += poppedByVerdict[unsigned(v)];
	}
	out << "{\"version\":1,\"rows\":" << rowList.size() << ",\"depth_bins\":" << DEPTH_BINS
		<< ",\"depth_bin_cost\":" << BuildingGradientSearch::DEPTH_BIN_COST << ',';
	object("rebuilds", rebuildCounts, Reason::Count, reasonName, false);
	out << ",\"dirty_with_generation\":" << dirtyWithGeneration << ',';
	object("generation_verdicts", verdictCounts, Verdict::Count, verdictName, true);
	out << ",\"generation_kept\":" << kept << ',';
	object("events", eventCounts, Event::Count, eventName, false);
	out << ",\"popped_total\":" << poppedTotal << ",\"popped_unknown_lifetime\":" << poppedUnknownLifetime << ',';
	object("popped_by_lifetime_reason", poppedByReason, Reason::Count, reasonName, false);
	out << ',';
	object("popped_by_lifetime_verdict", poppedByVerdict, Verdict::Count, verdictName, true);
	out << ",\"popped_kept\":" << keptPopped << ",\"clearing_goal_gone\":" << clearingGoalGoneCount
		<< ",\"journal\":{\"capacity\":" << JOURNAL_CAPACITY << ",\"bumps\":" << bumps
		<< ",\"undetailed_bumps\":" << undetailedBumps << ",\"building_cells\":" << buildingCells
		<< ",\"building_noops\":" << buildingNoops << ",\"forbidden_cells\":" << forbiddenCells
		<< ",\"resets\":" << journalResets << ",\"lost_generation\":" << lostGeneration << '}'
		<< ",\"unbumped\":{\"immobile_changes\":" << immobileChanges << ",\"resource_changes\":" << resourceChanges
		<< ",\"resource_blocking_changes\":" << resourceBlockingChanges << "}}";
}
