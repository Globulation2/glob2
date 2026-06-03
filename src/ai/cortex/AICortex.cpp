// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AICortex.h"
#include "CortexObservation.h"

#include "Order.h"
#include "Player.h"
#include "team/Team.h"
#include "GlobalContainer.h"
#include "Settings.h"
#include "IntBuildingType.h"
#include "BuildingType.h"
#include "building/Building.h"
#include "unit/UnitConsts.h"
#include <Stream.h>

using std::shared_ptr;

AICortex::AICortex(Player* player)
{
	init(player);
}

AICortex::AICortex(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	init(player);
	load(stream, player, versionMinor);
}

AICortex::~AICortex()
{
}

void AICortex::init(Player* player)
{
	this->player = player;
	timer = 0;
	buildCooldownUntil = 0;
}

bool AICortex::load(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	this->player = player;
	stream->readEnterSection("AICortex");
	timer = stream->readUint32("timer");
	buildCooldownUntil = stream->readSint32("buildCooldownUntil");
	stream->readLeaveSection();
	// orderQueue is transient working state, not persisted; it refills on the
	// next decision cycle after load.
	return true;
}

void AICortex::save(GAGCore::OutputStream* stream)
{
	stream->writeEnterSection("AICortex");
	stream->writeUint32(timer, "timer");
	stream->writeSint32(buildCooldownUntil, "buildCooldownUntil");
	stream->writeLeaveSection();
}

void AICortex::translateAction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	switch (action.kind)
	{
		case Cortex::ACTION_NOOP:
			// Nothing to enqueue.
			break;

		case Cortex::ACTION_BUILD:
		{
			const int type = action.buildingType;
			const int slot = action.locationSlot;
			if (type < 0 || type >= Cortex::CORTEX_BUILDING_TYPES)
				break;
			if (slot < 0 || slot >= Cortex::CORTEX_BUILD_CANDIDATES)
				break;

			// An issued OrderCreate takes several ticks to execute and register
			// as a building site, which is longer than one decision cycle. Without
			// a cooldown the policy re-issues the same build on the next cycle
			// (the site isn't visible yet), stacking duplicate orders the engine
			// then rejects. Suppress new builds until the in-flight one can land.
			if (obs.tick < buildCooldownUntil)
				break;

			const Cortex::BuildCandidate& cand = obs.buildCandidates[type][slot];
			if (!cand.valid)
				break; // policy chose a stale/empty slot; drop rather than misbuild.

			// Resolve the long building-site type id for a fresh (level 0)
			// building, exactly as the GUI/Echo build path does.
			const std::string& name = IntBuildingType::reverseConversionMap[type];
			Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, true);
			if (typeNum < 0)
				break; // no buildable site type (e.g. a virtual/flag type) — skip.

			// Worker counts from the engine's canonical defaults: column 0 is the
			// construction-site assignment, column 1 the finished-building one.
			const int unitWorking       = globalContainer->settings.defaultUnitsAssigned[type][0];
			const int unitWorkingFuture = globalContainer->settings.defaultUnitsAssigned[type][1];

			orderQueue.push(shared_ptr<Order>(new OrderCreate(
				player->team->teamNumber, cand.x, cand.y, typeNum,
				unitWorking, unitWorkingFuture)));
			buildCooldownUntil = obs.tick + BUILD_COOLDOWN_TICKS;
			break;
		}

		case Cortex::ACTION_SET_PRODUCTION:
		{
			// Clamp the target defensively to [0, CORTEX_MAX_RATIO]; the engine
			// writes ratios verbatim, so it's the action layer's job to bound them.
			Sint32 target[NB_UNIT_TYPE];
			for (int t = 0; t < NB_UNIT_TYPE; t++)
			{
				int r = action.productionRatio[t];
				target[t] = (r < 0) ? 0 : (r > Cortex::CORTEX_MAX_RATIO ? Cortex::CORTEX_MAX_RATIO : r);
			}

			// Iterate buildings by array index (never std::set) for lockstep
			// determinism. Enqueue one OrderModifySwarm per finished swarm whose
			// current ratio differs from the target; getOrder() drains orderQueue
			// one order per tick, so this naturally retargets one swarm per tick.
			Team* team = player->team;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (!b)
					continue;
				if (b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
					continue;
				if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
					continue; // finished swarm only — not a site, a swarm under upgrade, or dead.
				                  // (mirrors executeModifySwarm's unitProductionTime guard.)

				// Dedup: only emit when this swarm's current ratio differs from
				// the target, so a steady-state policy that re-issues the same
				// ACTION_SET_PRODUCTION every cycle doesn't spam redundant orders.
				bool differs = false;
				for (int t = 0; t < NB_UNIT_TYPE; t++)
					if (b->ratio[t] != target[t])
					{
						differs = true;
						break;
					}
				if (!differs)
					continue;

				orderQueue.push(shared_ptr<Order>(new OrderModifySwarm(b->gid, target)));
			}
			break;
		}

		default:
			// Unknown intent: ignore rather than emit a bogus Order.
			break;
	}
}

shared_ptr<Order> AICortex::getOrder(void)
{
	// Drain any Orders queued by a prior decision cycle, one per tick.
	if (!orderQueue.empty())
	{
		shared_ptr<Order> order = orderQueue.front();
		orderQueue.pop();
		return order;
	}

	// Run the observation -> policy -> action pipeline on a slow cadence.
	timer++;
	if ((timer % OBSERVE_INTERVAL) == 0)
	{
		Cortex::CortexObservation obs = Cortex::observe(player);
		Cortex::CortexAction action = policy.decide(obs);
		translateAction(action, obs);

		if (!orderQueue.empty())
		{
			shared_ptr<Order> order = orderQueue.front();
			orderQueue.pop();
			return order;
		}
	}

	return shared_ptr<Order>(new NullOrder());
}
