// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "EntityRandom.h"

#include "Material.h"
#include "UnitConsts.h"
#include <type_traits>

// Authoritative pointer-free building state. Live buildings and snapshots use
// this exact record; owner/type bindings, lists and query scratch live outside it.
struct BuildingStateRecord
{
	enum BuildingState
	{
		DEAD=0,
		ALIVE=1,
		WAITING_FOR_DESTRUCTION=2,
		WAITING_FOR_CONSTRUCTION=3,
		WAITING_FOR_CONSTRUCTION_ROOM=4
	};
	enum ConstructionResultState
	{
		NO_CONSTRUCTION=0,
		NEW_BUILDING=1,
		UPGRADE=2,
		REPAIR=3
	};

	// Presentation clocks remain excluded from legacy simulation checksums.
	Uint32 lastShootStep;
	Sint32 lastShootSpeedX, lastShootSpeedY;

	Sint32 typeNum;
	int shortTypeNum;
	BuildingState buildingState;
	ConstructionResultState constructionResultState;
	Sint32 constructionOriginTypeNum = -1;
	Sint32 maxUnitWorking, maxUnitWorkingPreferred, maxUnitWorkingFuture;
	// Maintained desired staffing; zero when the building needs no work.
	Sint32 desiredMaxUnitWorking, maxUnitInside;
	// Authoritative order-applied priority (-1/0/+1). Pending GUI values live
	// in BuildingGuiState, outside simulation state.
	Sint32 priority;
	EntityRandom entityRandom;
	Uint32 scriptIdentity = 0; // Excluded from legacy simulation checksums.
	Uint16 gid;
	Sint32 posX, posY;
	Uint8 underAttackTimer;
	Sint32 unitStayRange;
	bool clearingMaterials[MaterialCount]; // Clears resources yielding this material.
	Sint32 minLevelToFlag;
	Sint32 minWorkerLevelToFlag = 0;
	bool explorersRequireBombing = false;
	// Canonical private stock. Shared stock is owned by Team; a runtime binding
	// selects that pool instead, and snapshot queries select the frozen Team pool.
	Sint32 localMaterials[MaterialSlotCount];
	Sint32 wishedMaterials[MaterialSlotCount];
	Sint32 hp;
	Sint32 productionTimeout;
	bool siteCompletionPending = false;
	Sint32 productionUnit = -1;
	// Authoritative order-applied production ratios; pending slider values
	// live in BuildingGuiState.
	Sint32 ratio[NB_UNIT_TYPE];
	Sint32 percentUsed[NB_UNIT_TYPE];
	Uint32 unitsFailingRequirements[8];
	Uint32 receiveMaterialMask, sendMaterialMask;
	Sint32 bullets;
	Uint32 seenByMask;
	// Footprint, clearing and combat access, each without/with swimming.
	bool locked[6];
	// Keep optional funding bookkeeping after existing hot simulation fields.
	bool areaFunded = false;
	Sint8 areaFundingTeam = -1;
	Sint32 areaFundingType = -1;
	Uint32 areaFundingTick = 0;
	bool operator==(const BuildingStateRecord&) const = default;
};
static_assert(std::is_trivially_copyable_v<BuildingStateRecord>);
static_assert(std::is_standard_layout_v<BuildingStateRecord>);
