// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AtlasObservation.h"

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Ressource.h"
#include "Team.h"
#include "Unit.h"
#include "UnitConsts.h"

#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace Atlas
{
	namespace
	{
		const char OBS_MAGIC[4] = {'A', 'O', 'B', '1'};

		template <typename T> void put(std::FILE *f, T value)
		{
			std::fwrite(&value, sizeof(T), 1, f);
		}

		//! Engine resource types carried in the observation. MAX_RESOURCES is
		//! 8; the remaining MAX_NB_RESOURCES slots are not real resources.
		constexpr int OBSERVED_RESOURCES = 8;

		//! Compress a plane block, returning false if zlib refuses. Planes are
		//! mostly long runs of zero, so this is where the format's size comes
		//! from: a raw record is ~900 KB on a 128x128 map.
		bool deflateBlock(const std::vector<Uint8> &in, std::vector<Uint8> &out)
		{
			uLongf bound = compressBound(uLong(in.size()));
			out.resize(bound);
			if (compress2(out.data(), &bound, in.data(), uLong(in.size()), 6) != Z_OK)
				return false;
			out.resize(bound);
			return true;
		}

		/*
		  Multi-source BFS distance field, computed here rather than read from
		  the engine.

		  Map::getResourceGradient and its siblings look like pure reads but are
		  not: the gradients are built lazily per team and swim class, so asking
		  for one from a diagnostic builds a cache at a tick the simulation
		  would not have built it, and the AI subsequently receives a gradient
		  computed from a different tick's map. Measured, calling them from here
		  moved the game checksum (9d3acac2 against a reference 916e8281), which
		  makes them unusable for observation. Recomputing costs a sweep per
		  field per record, which at one record per 250 ticks is nothing.

		  Distances are over land for a walking unit and wrap with the torus.
		  Swimmers would see a different field; a land field is the informative
		  one for placement, which is what these planes feed.
		*/
		void distanceField(const Map *map, const std::vector<Uint8> &sources,
		                   std::vector<Uint16> &out)
		{
			const Sint32 w = map->getW(), h = map->getH();
			const size_t cells = size_t(w) * size_t(h);
			out.assign(cells, 0xFFFF);
			std::vector<Uint32> queue;
			queue.reserve(cells);
			for (size_t i = 0; i < cells; i++)
				if (sources[i])
				{
					out[i] = 0;
					queue.push_back(Uint32(i));
				}
			for (size_t head = 0; head < queue.size(); head++)
			{
				const Uint32 index = queue[head];
				const Sint32 x = Sint32(index % size_t(w));
				const Sint32 y = Sint32(index / size_t(w));
				const Uint16 next = Uint16(out[index] + 1);
				if (next > 255)
					continue;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++)
					{
						if (!dx && !dy)
							continue;
						const Sint32 nx = map->normalizeX(x + dx);
						const Sint32 ny = map->normalizeY(y + dy);
						const size_t n = size_t(ny) * size_t(w) + size_t(nx);
						if (out[n] <= next)
							continue;
						if (map->getTerrainType(nx, ny) == WATER)
							continue;
						out[n] = next;
						queue.push_back(Uint32(n));
					}
			}
		}

		//! Distance fields read better as nearness: 255 adjacent, 0 unreachable.
		inline Uint8 nearness(Uint16 distance)
		{
			if (distance >= 0xFFFF)
				return 0; // unreachable
			return Uint8(std::max<int>(1, 255 - std::min<int>(254, int(distance) * 8)));
		}
	} // namespace

	bool encodeStaticPlanes(const Map *map, std::vector<Uint8> &out)
	{
		if (!map)
			return false;
		const Sint32 w = map->getW(), h = map->getH();
		const size_t cells = size_t(w) * size_t(h);
		out.assign(size_t(SP_COUNT) * cells, 0);

		for (Sint32 y = 0; y < h; y++)
			for (Sint32 x = 0; x < w; x++)
			{
				const size_t i = size_t(y) * size_t(w) + size_t(x);
				const int terrain = map->getTerrainType(x, y);
				if (terrain == GRASS)
					out[size_t(SP_TERRAIN_GRASS) * cells + i] = 255;
				else if (terrain == SAND)
					out[size_t(SP_TERRAIN_SAND) * cells + i] = 255;
				else if (terrain == WATER)
					out[size_t(SP_TERRAIN_WATER) * cells + i] = 255;
				// Fertility is a Uint16 chance; scale to a byte.
				out[size_t(SP_FERTILITY) * cells + i] =
					Uint8(std::min<int>(255, map->getTile(x, y).fertility));
			}
		return true;
	}

	bool encodeDynamicPlanes(Team *team, std::vector<Uint8> &out)
	{
		if (!team || !team->game)
			return false;
		Map *map = &team->game->map;
		const Sint32 w = map->getW(), h = map->getH();
		const size_t cells = size_t(w) * size_t(h);
		out.assign(size_t(DP_COUNT) * cells, 0);

		const Uint32 vision = team->me;
		auto plane = [&](DynamicPlane p) -> Uint8 * { return out.data() + size_t(p) * cells; };
		auto at = [&](Sint32 x, Sint32 y) -> size_t {
			return size_t(map->normalizeY(y)) * size_t(w) + size_t(map->normalizeX(x));
		};

		// --- per-cell sweep: resources, areas, fog -------------------------
		for (Sint32 y = 0; y < h; y++)
			for (Sint32 x = 0; x < w; x++)
			{
				const size_t i = size_t(y) * size_t(w) + size_t(x);
				const bool known = map->isMapDiscovered(x, y, vision);
				const bool visible = map->isFOWDiscovered(x, y, vision);
				plane(DP_DISCOVERY)[i] = visible ? 255 : (known ? 128 : 0);

				// Resources are remembered once seen: the team knows a forest
				// is there even when not currently looking at it.
				if (known)
				{
					const Resource &res = map->getResource(x, y);
					if (res.type != NO_RES_TYPE && res.type < OBSERVED_RESOURCES)
						plane(DynamicPlane(DP_RESOURCE_0 + res.type))[i] =
							Uint8(std::min<int>(255, res.amount * 32));
				}

				const Uint32 mask = Team::teamNumberToMask(team->teamNumber);
				if (map->isGuardArea(x, y, mask))
					plane(DP_AREA_GUARD)[i] = 255;
				if (map->isClearArea(x, y, mask))
					plane(DP_AREA_CLEAR)[i] = 255;
				if (map->isForbidden(x, y, mask))
					plane(DP_AREA_FORBIDDEN)[i] = 255;
			}

		// --- buildings ------------------------------------------------------
		// Own buildings are always visible to their owner. Everyone else's are
		// drawn only where the team can see them.
		Game *game = team->game;
		for (int t = 0; t < game->mapHeader.getNumberOfTeams(); t++)
		{
			Team *other = game->teams[t];
			if (!other)
				continue;
			const bool mine = (other == team);
			const bool ally = !mine && ((team->allies & other->me) != 0);
			for (int b = 0; b < Building::MAX_COUNT; b++)
			{
				Building *building = other->myBuildings[b];
				if (!building || building->buildingState == Building::DEAD || !building->type)
					continue;
				const int shortType = building->type->shortTypeNum;
				if (shortType < 0 || shortType >= IntBuildingType::NB_BUILDING)
					continue;
				for (int dy = 0; dy < building->type->height; dy++)
					for (int dx = 0; dx < building->type->width; dx++)
					{
						const Sint32 cx = building->posX + dx, cy = building->posY + dy;
						if (!mine && !map->isMapDiscovered(cx, cy, vision))
							continue;
						const size_t i = at(cx, cy);
						if (mine)
						{
							plane(DynamicPlane(DP_MY_BUILDING_0 + shortType))[i] = 255;
							plane(DP_MY_BUILDING_LEVEL)[i] =
								Uint8(std::min<int>(255, building->type->level * 60));
							plane(DP_MY_BUILDING_SITE)[i] =
								building->type->isBuildingSite ? 255 : 0;
							plane(DP_MY_BUILDING_WORKERS)[i] =
								Uint8(std::min<int>(255, building->maxUnitWorking * 20));
						}
						else if (ally)
							plane(DP_ALLY_BUILDING)[i] = 255;
						else
							plane(DynamicPlane(DP_ENEMY_BUILDING_0 + shortType))[i] = 255;
					}
			}
		}

		// --- units ----------------------------------------------------------
		// Own units from the team's own list; everyone else's through the map,
		// so fog applies exactly as the engine sees it.
		for (int u = 0; u < Unit::MAX_COUNT; u++)
		{
			Unit *unit = team->myUnits[u];
			if (!unit)
				continue;
			const size_t i = at(unit->posX, unit->posY);
			DynamicPlane p = DP_MY_UNIT_WORKER;
			if (unit->typeNum == EXPLORER)
				p = DP_MY_UNIT_EXPLORER;
			else if (unit->typeNum == WARRIOR)
				p = DP_MY_UNIT_WARRIOR;
			plane(p)[i] = Uint8(std::min<int>(255, int(plane(p)[i]) + 64));
		}

		for (Sint32 y = 0; y < h; y++)
			for (Sint32 x = 0; x < w; x++)
			{
				if (!map->isFOWDiscovered(x, y, vision))
					continue;
				const Uint16 guid = map->getGroundUnit(x, y);
				if (guid == NOGUID)
					continue;
				const int owner = Unit::GIDtoTeam(guid);
				if (owner == team->teamNumber)
					continue;
				Team *other = game->teams[owner];
				if (!other)
					continue;
				if ((team->allies & other->me) != 0)
					continue;
				Unit *unit = other->myUnits[Unit::GIDtoID(guid)];
				if (!unit)
					continue;
				const size_t i = size_t(y) * size_t(w) + size_t(x);
				DynamicPlane p = DP_ENEMY_UNIT_WORKER;
				if (unit->typeNum == EXPLORER)
					p = DP_ENEMY_UNIT_EXPLORER;
				else if (unit->typeNum == WARRIOR)
					p = DP_ENEMY_UNIT_WARRIOR;
				plane(p)[i] = Uint8(std::min<int>(255, int(plane(p)[i]) + 64));
			}

		// --- distance fields ------------------------------------------------
		// The highest-value planes here: a conv net would otherwise need depth
		// proportional to the map diameter to learn reachability, and these
		// answer it outright. Computed locally — see distanceField above for
		// why the engine's own gradients cannot be used.
		{
			std::vector<Uint8> sources(cells, 0);
			std::vector<Uint16> field;
			const std::pair<DynamicPlane, int> resourceFields[] = {
				{DP_GRAD_WOOD, WOOD}, {DP_GRAD_WHEAT, WHEAT}, {DP_GRAD_STONE, STONE}};
			for (const auto &entry : resourceFields)
			{
				std::fill(sources.begin(), sources.end(), Uint8(0));
				for (Sint32 y = 0; y < h; y++)
					for (Sint32 x = 0; x < w; x++)
					{
						if (!map->isMapDiscovered(x, y, vision))
							continue; // never reveal unexplored resources
						const Resource &res = map->getResource(x, y);
						if (res.type == entry.second)
							sources[size_t(y) * size_t(w) + size_t(x)] = 1;
					}
				distanceField(map, sources, field);
				for (size_t i = 0; i < cells; i++)
					plane(entry.first)[i] = nearness(field[i]);
			}

			const std::pair<DynamicPlane, DynamicPlane> areaFields[] = {
				{DP_GRAD_FORBIDDEN, DP_AREA_FORBIDDEN},
				{DP_GRAD_GUARD, DP_AREA_GUARD},
				{DP_GRAD_CLEAR, DP_AREA_CLEAR}};
			for (const auto &entry : areaFields)
			{
				const Uint8 *area = plane(entry.second);
				bool any = false;
				for (size_t i = 0; i < cells; i++)
				{
					sources[i] = area[i] ? 1 : 0;
					any = any || sources[i];
				}
				if (!any)
					continue; // leave the plane at zero rather than sweeping
				distanceField(map, sources, field);
				for (size_t i = 0; i < cells; i++)
					plane(entry.first)[i] = nearness(field[i]);
			}
		}

		return true;
	}

	// ---------------------------------------------------------------- writer

	ObservationWriter::~ObservationWriter()
	{
		close();
	}

	bool ObservationWriter::open(const std::string &path, const Map *map, Uint8 numTeams,
	                             Uint32 samplePeriod)
	{
		if (!map)
			return false;
		file_ = std::fopen(path.c_str(), "wb");
		if (!file_)
			return false;
		mapW_ = map->getW();
		mapH_ = map->getH();
		samplePeriod_ = samplePeriod ? samplePeriod : 1;
		records_ = 0;

		std::vector<Uint8> statics, packed;
		if (!encodeStaticPlanes(map, statics) || !deflateBlock(statics, packed))
		{
			std::fclose(file_);
			file_ = nullptr;
			return false;
		}

		std::fwrite(OBS_MAGIC, 1, 4, file_);
		put<Uint32>(file_, 0); // patched by close()
		put<Uint16>(file_, Uint16(mapW_));
		put<Uint16>(file_, Uint16(mapH_));
		put<Uint8>(file_, Uint8(SP_COUNT));
		put<Uint8>(file_, Uint8(DP_COUNT));
		put<Uint8>(file_, numTeams);
		put<Uint8>(file_, 0);
		put<Uint32>(file_, Uint32(packed.size()));
		std::fwrite(packed.data(), 1, packed.size(), file_);
		return true;
	}

	void ObservationWriter::writeRecord(Team *team, Uint32 tick, Uint8 teacherId)
	{
		if (!file_ || !team || !team->game)
			return;
		if (team->game->map.getW() != mapW_ || team->game->map.getH() != mapH_)
			return;
		if (!encodeDynamicPlanes(team, scratch_) || !deflateBlock(scratch_, compressed_))
			return;

		put<Uint32>(file_, tick);
		put<Uint8>(file_, Uint8(team->teamNumber));
		put<Uint8>(file_, teacherId);
		put<Uint16>(file_, 0);
		put<Uint32>(file_, Uint32(compressed_.size()));
		std::fwrite(compressed_.data(), 1, compressed_.size(), file_);
		records_++;
	}

	void ObservationWriter::close()
	{
		if (!file_)
			return;
		std::fseek(file_, 4, SEEK_SET);
		put<Uint32>(file_, records_);
		std::fclose(file_);
		file_ = nullptr;
	}
} // namespace Atlas
