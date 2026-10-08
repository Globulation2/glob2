// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Ressource.h"
#include "UnitConsts.h"
#include "sim/EntityRef.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <span>

#include <SDLGraphicContext.h>

#include <array>
#include <string>
#include <vector>

struct BuildingType;

using SceneTeam = SimulationSnapshot::TeamView;
inline GAGCore::Color presentationColor(const SimulationSnapshot::ColorRecord& c)
{ return {c.r,c.g,c.b,c.a}; }

//! The standard immutable records, borrowed without renderer-specific copies.
using SnapshotUnit = SimulationSnapshot::UnitView;

using SnapshotBuilding = SimulationSnapshot::BuildingView;

//! What the selected building contributes to the map view (off-screen worker
//! arrows and failing-unit badges).
struct SceneSelectedBuilding
{
	static constexpr int FailReasons = 8; //!< Building::UnitCantWorkReasonSize (checked)
	BuildingRef ref;
	int verbose = 0;
	std::span<const Uint16> debugGradient;
	bool recordFailingUnits = false;
	Sint32 desiredMaxUnitWorking = 0;
	std::span<const Uint32> unitsFailingRequirements;
	std::array<std::span<const Uint16>, FailReasons> unitsFailingByReason;
	std::span<const UnitRef> unitsWorking; //!< Generation-checked snapshot relationships
};

using SceneBullet = SimulationSnapshot::BulletRecord;
using SceneExplosion = SimulationSnapshot::ExplosionRecord;
using SceneDeathAnimation = SimulationSnapshot::DeathRecord;
using SceneSectorEffects = SimulationSnapshot::SectorEffects;

//! All units, buildings and effects of a PresentationFrame, with lookup by gid.
struct SceneEntities
{
	static constexpr int Teams = 32;           //!< >= Team::MAX_COUNT (checked)
	static constexpr int SlotsPerTeam = 1024;  //!< >= Unit/Building::MAX_COUNT (checked)

	int teamCount = 0;
	MaterialMask materialPresence = 0;
	std::span<const SceneTeam> teams;
	std::span<const SnapshotUnit> units;
	std::span<const SnapshotBuilding> buildings;
    SimulationSnapshot::Handle world;
    std::shared_ptr<const std::vector<BuildingType>> typeDefinitions;
    std::vector<Uint8> connectionMasks;
	//! Flag gids per team, in each team's order.
	std::array<std::span<const BuildingRef>, Teams> virtualBuildings;
	std::span<const SceneSectorEffects> sectors;
	SceneSelectedBuilding selectedBuilding;
	UnitRef selectedUnit;
	Uint32 highlightUnitType = 0, highlightBuildingType = 0;

    const BuildingType* type(const SnapshotBuilding& b) const { return &typeDefinitions->at(b.typeNum); }
    const BuildingType* lastUpgradeType(const SnapshotBuilding& b) const {
        int completed=b.typeNum;
        if (b.constructionResultState==BuildingStateRecord::REPAIR)
            completed=b.constructionOriginTypeNum>=0 ? b.constructionOriginTypeNum : type(b)->isBuildingSite ? type(b)->nextLevel : b.typeNum;
        return &typeDefinitions->at(typeDefinitions->at(completed).terminalTypeNum);
    }
    const Sint32* materials(const SnapshotBuilding& b) const { return b.usesTeamResources ? world.teams->values.at(b.team).materials.data() : b.localMaterials; }
    Uint8 connections(const SnapshotBuilding& b) const { return connectionMasks.at(buildingIndex[b.gid]); }
	const SceneTeam &owner(const SnapshotUnit &u) const { return teams[u.team]; }
	const SceneTeam &owner(const SnapshotBuilding &b) const { return teams[b.team]; }
	const SnapshotUnit *unit(Uint16 gid) const
	{
		const Uint32 i = gid < unitIndex.size() ? unitIndex[gid] : Uint32(-1);
		return size_t(i)<units.size() ? &units[i] : nullptr;
	}
	const SnapshotBuilding *building(Uint16 gid) const
	{
		const Uint32 i = gid < buildingIndex.size() ? buildingIndex[gid] : Uint32(-1);
		return size_t(i)<buildings.size() ? &buildings[i] : nullptr;
	}
    const SnapshotUnit* unit(UnitRef ref) const
    { const auto* value=unit(ref.gid); return value && value->identity==ref ? value : nullptr; }
    const SnapshotBuilding* building(BuildingRef ref) const
    { const auto* value=building(ref.gid); return value && value->identity==ref ? value : nullptr; }
	bool isSelected(const SnapshotUnit &u) const { return u.gid == selectedUnit.gid && u.scriptIdentity == selectedUnit.generation; }
	bool isSelected(const SnapshotBuilding &b) const
	{
		return b.gid == selectedBuilding.ref.gid && b.scriptIdentity == selectedBuilding.ref.generation;
	}

	std::span<const Uint32> unitIndex;
    std::span<const Uint32> buildingIndex; //!< gid -> index, -1 when absent
};
