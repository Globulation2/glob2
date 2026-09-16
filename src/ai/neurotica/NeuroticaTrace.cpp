// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "NeuroticaTrace.h"

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Team.h"

#include <algorithm>
#include <cstring>

namespace Neurotica
{
	namespace
	{
		// Bumped from ATR1 when the per-building record grew priority, min-level and
		// unit ratios. Readers reject on magic, so an old trace fails loudly
		// rather than being misparsed at a shifted offset.
		const char TRACE_MAGIC[4] = {'A', 'T', 'R', '2'};
		const char TRACE_FOOTER_MAGIC[4] = {'A', 'T', 'R', 'E'};
		constexpr size_t HEADER_BYTES = 16;
		//! Offset of the patched snapshot count within the header.
		constexpr long SNAPSHOT_COUNT_OFFSET = 4;

		template <typename T> void put(std::FILE *f, T value)
		{
			std::fwrite(&value, sizeof(T), 1, f);
		}

		template <typename T> bool take(const std::vector<Uint8> &buf, size_t &at, T &out)
		{
			if (at + sizeof(T) > buf.size())
				return false;
			std::memcpy(&out, buf.data() + at, sizeof(T));
			at += sizeof(T);
			return true;
		}

		//! Collect a team's live buildings in the trace's compact form.
		void collectBuildings(Team *team, const Map *map, std::vector<TraceBuilding> &out)
		{
			out.clear();
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building *b = team->myBuildings[i];
				if (!b || b->buildingState == Building::DEAD || !b->type)
					continue;
				const Sint32 shortType = b->type->shortTypeNum;
				if (shortType < 0 || shortType >= IntBuildingType::NB_BUILDING)
					continue;
				TraceBuilding record;
				record.x = Uint16(map->normalizeX(b->posX));
				record.y = Uint16(map->normalizeY(b->posY));
				record.shortType = Uint8(shortType);
				record.level = Uint8(std::min<Sint32>(b->type->level, 255));
				record.workers = Uint8(std::min<Sint32>(b->maxUnitWorking, 255));
				record.flagRadius =
					b->type->isVirtual ? Uint8(std::min<Sint32>(b->unitStayRange, 255)) : 0;
				record.priority = Uint8(std::clamp<Sint32>(b->priority + 1, 0, 2));
				record.minLevelToFlag = Uint8(std::clamp<Sint32>(b->minLevelToFlag, 0, 255));
				for (size_t u = 0; u < SWARM_RATIO_STRIDE; u++)
					record.ratio[u] = Uint8(std::clamp<Sint32>(b->ratio[u], 0, 255));
				out.push_back(record);
			}
		}

		//! Dense per-cell area bitmask for a team, in row-major order.
		void collectAreas(Team *team, const Map *map, Sint32 w, Sint32 h, std::vector<Uint8> &out)
		{
			out.assign(size_t(w) * size_t(h), 0);
			const Uint32 teamMask = Team::teamNumberToMask(team->teamNumber);
			for (Sint32 y = 0; y < h; y++)
				for (Sint32 x = 0; x < w; x++)
				{
					Uint8 bits = 0;
					if (map->isGuardArea(x, y, teamMask))
						bits |= AREA_GUARD;
					if (map->isClearArea(x, y, teamMask))
						bits |= AREA_CLEAR;
					if (map->isForbidden(x, y, teamMask))
						bits |= AREA_FORBIDDEN;
					out[size_t(y) * size_t(w) + size_t(x)] = bits;
				}
		}
	} // namespace

	// ---------------------------------------------------------------- writer

	TraceWriter::~TraceWriter()
	{
		if (file_)
			close(std::vector<Uint8>(numTeams_, OUTCOME_UNKNOWN));
	}

	bool TraceWriter::open(const std::string &path, Sint32 mapW, Sint32 mapH, Uint8 numTeams,
	                       Uint8 policyPeriod)
	{
		file_ = std::fopen(path.c_str(), "wb");
		if (!file_)
			return false;
		mapW_ = mapW;
		mapH_ = mapH;
		numTeams_ = numTeams;
		policyPeriod_ = policyPeriod;
		snapshots_ = 0;

		std::fwrite(TRACE_MAGIC, 1, 4, file_);
		put<Uint32>(file_, 0); // patched by close()
		put<Uint16>(file_, Uint16(mapW));
		put<Uint16>(file_, Uint16(mapH));
		put<Uint8>(file_, numTeams);
		put<Uint8>(file_, policyPeriod);
		put<Uint16>(file_, 0); // pad
		return true;
	}

	void TraceWriter::writeSnapshot(Team *team, Uint32 tick, Uint8 teacherId)
	{
		if (!file_ || !team || !team->game)
			return;
		const Map *map = &team->game->map;
		if (map->getW() != mapW_ || map->getH() != mapH_)
			return;

		std::vector<TraceBuilding> buildings;
		collectBuildings(team, map, buildings);
		std::vector<Uint8> areas;
		collectAreas(team, map, mapW_, mapH_, areas);

		put<Uint32>(file_, tick);
		put<Uint8>(file_, Uint8(team->teamNumber));
		put<Uint8>(file_, teacherId);
		put<Uint16>(file_, Uint16(std::min<size_t>(buildings.size(), 0xFFFF)));
		for (const TraceBuilding &b : buildings)
		{
			put<Uint16>(file_, b.x);
			put<Uint16>(file_, b.y);
			put<Uint8>(file_, b.shortType);
			put<Uint8>(file_, b.level);
			put<Uint8>(file_, b.workers);
			put<Uint8>(file_, b.flagRadius);
			put<Uint8>(file_, b.priority);
			put<Uint8>(file_, b.minLevelToFlag);
			for (size_t u = 0; u < SWARM_RATIO_STRIDE; u++)
				put<Uint8>(file_, b.ratio[u]);
		}

		// Run-length encode the area layers. Area maps are overwhelmingly one
		// long zero run with a few painted patches, so this is where the
		// format's compactness actually comes from.
		std::vector<std::pair<Uint8, Uint16>> runs;
		for (size_t i = 0; i < areas.size();)
		{
			const Uint8 value = areas[i];
			size_t run = 1;
			while (i + run < areas.size() && areas[i + run] == value && run < 0xFFFF)
				run++;
			runs.emplace_back(value, Uint16(run));
			i += run;
		}
		put<Uint32>(file_, Uint32(runs.size()));
		for (const auto &run : runs)
		{
			put<Uint8>(file_, run.first);
			put<Uint16>(file_, run.second);
		}

		snapshots_++;
	}

	void TraceWriter::close(const std::vector<Uint8> &outcomes)
	{
		if (!file_)
			return;
		std::fwrite(TRACE_FOOTER_MAGIC, 1, 4, file_);
		put<Uint8>(file_, numTeams_);
		for (Uint8 team = 0; team < numTeams_; team++)
			put<Uint8>(file_, team < outcomes.size() ? outcomes[team] : Uint8(OUTCOME_UNKNOWN));

		std::fseek(file_, SNAPSHOT_COUNT_OFFSET, SEEK_SET);
		put<Uint32>(file_, snapshots_);
		std::fclose(file_);
		file_ = nullptr;
	}

	// ---------------------------------------------------------------- reader

	bool TraceReader::load(const std::string &path)
	{
		snapshots_.clear();
		outcomes_.clear();

		std::FILE *file = std::fopen(path.c_str(), "rb");
		if (!file)
			return false;
		std::fseek(file, 0, SEEK_END);
		const long size = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);
		if (size < long(HEADER_BYTES))
		{
			std::fclose(file);
			return false;
		}
		// static_cast, not size_t(size): the latter is a most-vexing-parse and
		// declares a function rather than a vector.
		std::vector<Uint8> buf(static_cast<size_t>(size));
		const size_t read = std::fread(buf.data(), 1, buf.size(), file);
		std::fclose(file);
		if (read != buf.size())
			return false;

		if (std::memcmp(buf.data(), TRACE_MAGIC, 4) != 0)
			return false;

		size_t at = 4;
		Uint32 count = 0;
		Uint16 w = 0, h = 0, pad = 0;
		Uint8 numTeams = 0, period = 0;
		if (!take(buf, at, count) || !take(buf, at, w) || !take(buf, at, h) ||
		    !take(buf, at, numTeams) || !take(buf, at, period) || !take(buf, at, pad))
			return false;
		mapW_ = w;
		mapH_ = h;
		policyPeriod_ = period;
		if (mapW_ <= 0 || mapH_ <= 0)
			return false;

		const size_t cells = size_t(mapW_) * size_t(mapH_);
		snapshots_.reserve(count);
		for (Uint32 s = 0; s < count; s++)
		{
			TraceSnapshot snapshot;
			Uint16 numBuildings = 0;
			if (!take(buf, at, snapshot.tick) || !take(buf, at, snapshot.teamNumber) ||
			    !take(buf, at, snapshot.teacherId) || !take(buf, at, numBuildings))
				return false;
			snapshot.buildings.resize(numBuildings);
			for (Uint16 b = 0; b < numBuildings; b++)
			{
				TraceBuilding &record = snapshot.buildings[b];
				if (!take(buf, at, record.x) || !take(buf, at, record.y) ||
				    !take(buf, at, record.shortType) || !take(buf, at, record.level) ||
				    !take(buf, at, record.workers) || !take(buf, at, record.flagRadius) ||
				    !take(buf, at, record.priority) || !take(buf, at, record.minLevelToFlag))
					return false;
				for (size_t u = 0; u < SWARM_RATIO_STRIDE; u++)
					if (!take(buf, at, record.ratio[u]))
						return false;
			}

			Uint32 runCount = 0;
			if (!take(buf, at, runCount))
				return false;
			snapshot.areas.clear();
			snapshot.areas.reserve(cells);
			for (Uint32 r = 0; r < runCount; r++)
			{
				Uint8 value = 0;
				Uint16 run = 0;
				if (!take(buf, at, value) || !take(buf, at, run))
					return false;
				// A corrupt or hostile run count must not be allowed to
				// allocate without bound.
				if (snapshot.areas.size() + run > cells)
					return false;
				snapshot.areas.insert(snapshot.areas.end(), run, value);
			}
			if (snapshot.areas.size() != cells)
				return false;
			snapshots_.push_back(std::move(snapshot));
		}

		// Footer is optional: a trace from a crashed or killed run still has
		// usable snapshots, it just has no outcomes. Treat that as unknown
		// rather than discarding the recording.
		if (at + 4 <= buf.size() && std::memcmp(buf.data() + at, TRACE_FOOTER_MAGIC, 4) == 0)
		{
			at += 4;
			Uint8 footerTeams = 0;
			if (take(buf, at, footerTeams))
				for (Uint8 team = 0; team < footerTeams; team++)
				{
					Uint8 outcome = OUTCOME_UNKNOWN;
					if (!take(buf, at, outcome))
						break;
					outcomes_.push_back(outcome);
				}
		}
		return true;
	}

	Uint8 TraceReader::outcome(Uint8 teamNumber) const
	{
		return teamNumber < outcomes_.size() ? outcomes_[teamNumber] : Uint8(OUTCOME_UNKNOWN);
	}

	const TraceSnapshot *TraceReader::at(Uint8 teamNumber, Uint32 tick) const
	{
		// Snapshots are appended in tick order, so a linear scan for the first
		// match is correct; traces are small enough that indexing per team
		// would be optimising the wrong thing.
		for (const TraceSnapshot &snapshot : snapshots_)
			if (snapshot.teamNumber == teamNumber && snapshot.tick >= tick)
				return &snapshot;
		return nullptr;
	}

	bool TraceReader::expand(const TraceSnapshot &snapshot, DesiredState &out) const
	{
		if (mapW_ <= 0 || mapH_ <= 0)
			return false;
		out.reset(mapW_, mapH_);
		for (const TraceBuilding &b : snapshot.buildings)
		{
			if (b.x >= mapW_ || b.y >= mapH_)
				continue;
			if (b.shortType >= IntBuildingType::NB_BUILDING)
				continue;
			const size_t i = out.index(b.x, b.y);
			out.building[i] = Uint8(b.shortType + 1);
			out.level[i] = b.level;
			out.workers[i] = b.workers;
			out.workersFuture[i] = b.workers;
			if (b.flagRadius != 0)
				out.flagRadius[i] = b.flagRadius;
			out.priority[i] = b.priority;
			if (b.shortType == IntBuildingType::SWARM_BUILDING)
				for (size_t u = 0; u < SWARM_RATIO_STRIDE; u++)
					out.swarmRatio[i * SWARM_RATIO_STRIDE + u] = b.ratio[u];
			else
				out.minLevelToFlag[i] = b.minLevelToFlag;
			// Uniform mid urgency: a trace records what a teacher had, not the
			// order it wanted things in, so inventing a priority here would be
			// fabricating a label. M2's policy learns urgency for real.
			out.urgency[i] = 128;
		}
		if (snapshot.areas.size() == out.areas.size())
			out.areas = snapshot.areas;
		return true;
	}
} // namespace Neurotica
