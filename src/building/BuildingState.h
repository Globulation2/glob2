// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "EntityRandom.h"

#include "Material.h"
#include "BuildingStock.h"
#include "UnitConsts.h"
#include <type_traits>

// Authoritative pointer-free building state. Live buildings and snapshots use
// this exact record; owner/type bindings, lists and query scratch live outside it.
struct BuildingStateRecord
{
	enum BuildingState : Uint8
	{
		DEAD=0,
		ALIVE=1,
		WAITING_FOR_DESTRUCTION=2,
		WAITING_FOR_CONSTRUCTION=3,
		WAITING_FOR_CONSTRUCTION_ROOM=4
	};
	enum ConstructionResultState : Uint8
	{
		NO_CONSTRUCTION=0,
		NEW_BUILDING=1,
		UPGRADE=2,
		REPAIR=3
	};

	// Presentation clocks remain excluded from legacy simulation checksums.
	Uint32 lastShootStep;
	Sint32 lastShootSpeedX, lastShootSpeedY;

	// Catalog variants are dense 0..4095; family/origin IDs retain -1 sentinels.
	Uint16 typeNum;
	Sint16 shortTypeNum;
	Sint16 constructionOriginTypeNum = -1;
	Sint32 maxUnitWorking, maxUnitWorkingPreferred, maxUnitWorkingFuture;
	// Maintained desired staffing; zero when the building needs no work.
	Sint32 desiredMaxUnitWorking;
	// Authoritative order-applied priority (-1/0/+1). Pending GUI values live
	// in BuildingGuiState, outside simulation state.
	Sint32 priority;
	EntityRandom entityRandom;
	Uint32 scriptIdentity = 0; // Excluded from legacy simulation checksums.
	Uint16 gid;
	Uint16 maxUnitInside;
	BuildingState buildingState;
	ConstructionResultState constructionResultState;
	Uint8 minLevelToFlag;
	Uint8 minWorkerLevelToFlag = 0;
	Sint32 posX, posY;
	Uint8 underAttackTimer;
	Uint16 unitStayRange;
	// Named masks preserve neighboring flags without bitfields or proxy references.
	MaterialMask clearingMaterialMask = 0;
	bool clearsMaterial(unsigned material) const { return (clearingMaterialMask & MaterialMask(1u << material)) != 0; }
	void setClearsMaterial(unsigned material, bool enabled)
	{
		const MaterialMask mask = MaterialMask(1u << material);
		clearingMaterialMask = enabled ? clearingMaterialMask | mask : clearingMaterialMask & ~mask;
	}
	bool explorersRequireBombing = false;
	// Canonical private stock. Shared stock is owned by Team; a runtime binding
	// selects that pool instead, and snapshot queries select the frozen Team pool.
	PrivateBuildingStock localMaterials[MaterialSlotCount];
	Sint32 privateMaterial(int material) const { return localMaterials[material]; }
	void setPrivateMaterial(int material, Sint64 value)
	{
		localMaterials[material] = value < 0 ? 0 : value > PrivateBuildingStockLimit ? PrivateBuildingStockLimit : value;
	}
	Sint32 wishedMaterials[MaterialSlotCount];
	Sint32 hp;
	Sint32 productionTimeout;
	bool siteCompletionPending = false;
	Sint32 productionUnit = -1;
	// Authoritative order-applied production ratios; pending slider values
	// live in BuildingGuiState.
	Uint16 ratio[NB_UNIT_TYPE];
	Sint32 percentUsed[NB_UNIT_TYPE];
	Uint32 unitsFailingRequirements[8];
	Uint32 receiveMaterialMask, sendMaterialMask;
	Sint32 bullets;
	Uint32 seenByMask;
	// Footprint, clearing and combat access, each without/with swimming.
	static constexpr unsigned LockCount = 9;
	Uint16 lockedMask = 0;
	bool isLocked(unsigned access) const { return (lockedMask & Uint16(1u << access)) != 0; }
	void setLocked(unsigned access, bool enabled)
	{
		const Uint16 mask = Uint16(1u << access);
		lockedMask = enabled ? lockedMask | mask : lockedMask & ~mask;
	}
	// Keep optional funding bookkeeping after existing hot simulation fields.
	bool areaFunded = false;
	Sint8 areaFundingTeam = -1;
	Sint16 areaFundingType = -1;
	Uint32 areaFundingTick = 0;
	bool operator==(const BuildingStateRecord&) const = default;
};
static_assert(std::is_trivially_copyable_v<BuildingStateRecord>);
static_assert(std::is_standard_layout_v<BuildingStateRecord>);
