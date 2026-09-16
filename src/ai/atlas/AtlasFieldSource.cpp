// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AtlasFieldSource.h"

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "Map.h"
#include "Team.h"

namespace Atlas
{
	bool IdentityFieldSource::snapshot(Team *team, DesiredState &out)
	{
		if (!team || !team->game)
			return false;
		Map *map = &team->game->map;
		const Sint32 w = map->getW();
		const Sint32 h = map->getH();
		if (w <= 0 || h <= 0)
			return false;

		out.reset(w, h);

		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building *b = team->myBuildings[i];
			if (!b || b->buildingState == Building::DEAD || !b->type)
				continue;
			const Sint32 x = map->normalizeX(b->posX);
			const Sint32 y = map->normalizeY(b->posY);
			const size_t idx = out.index(x, y);

			const Sint32 shortType = b->type->shortTypeNum;
			if (shortType < 0 || shortType >= IntBuildingType::NB_BUILDING)
				continue;
			out.building[idx] = Uint8(shortType + 1);
			out.level[idx] = Uint8(std::min<Sint32>(b->type->level, 254));
			out.workers[idx] = Uint8(std::min<Sint32>(b->maxUnitWorking, 254));
			// Building::maxUnitWorkingFuture is private and has no accessor.
			// Mirroring the current staffing is faithful for this field's
			// purpose: workersFuture is only ever read when *creating* or
			// upgrading a building, and every building in an identity
			// snapshot already exists, so the reconciler never consults it.
			out.workersFuture[idx] = out.workers[idx];
			if (b->type->isVirtual)
				out.flagRadius[idx] = Uint8(std::min<Sint32>(b->unitStayRange, 254));
			// Mid urgency across the board: the identity field has nothing to
			// prioritise, since by construction it asks for no change.
			out.urgency[idx] = 128;
		}

		const Uint32 teamMask = Team::teamNumberToMask(team->teamNumber);
		for (Sint32 y = 0; y < h; y++)
			for (Sint32 x = 0; x < w; x++)
			{
				const size_t idx = out.index(x, y);
				Uint8 bits = 0;
				if (map->isGuardArea(x, y, teamMask))
					bits |= AREA_GUARD;
				if (map->isClearArea(x, y, teamMask))
					bits |= AREA_CLEAR;
				if (map->isForbidden(x, y, teamMask))
					bits |= AREA_FORBIDDEN;
				out.areas[idx] = bits;
			}

		return true;
	}

	bool IdentityFieldSource::field(Uint32 tick, DesiredState &out)
	{
		(void)tick;
		return snapshot(team_, out);
	}

	OracleFieldSource::OracleFieldSource(Team *team, std::shared_ptr<TraceReader> trace,
	                                     Uint32 delta)
		: team_(team), trace_(std::move(trace)), delta_(delta)
	{
	}

	bool OracleFieldSource::field(Uint32 tick, DesiredState &out)
	{
		if (!team_ || !trace_)
			return false;
		const TraceSnapshot *snapshot = trace_->at(Uint8(team_->teamNumber), tick + delta_);
		if (!snapshot)
			return false; // past the end of the recording — stay inert
		return trace_->expand(*snapshot, out);
	}
} // namespace Atlas
