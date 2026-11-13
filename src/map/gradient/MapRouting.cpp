// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MapRouting.h"
#include "GradientRuntime.h"
#include "field/astar/AStar.h"

#include <cassert>

MapRouting::MapRouting(Map &owner) : map(owner), gradientRuntime(std::make_unique<GradientRuntime>())
{
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MaterialSlotCount; r++)
			for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
			{
				materialGradients[t][r][s] = NULL;
				marketMaterialGradients[t][r][s] = NULL;
			}
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
		{
			forbiddenGradient[t][s] = NULL;
			guardAreasGradient[t][s] = NULL;
			clearAreasGradient[t][s] = NULL;
		}
}

// Map::clear frees every buffer above before the owner releases this object.
MapRouting::~MapRouting() = default;

std::optional<Uint16> MapRouting::planeKeyForSlot(Uint16* const* slot) const
{
	constexpr auto perArray = std::size_t(Team::MAX_COUNT) * MaterialSlotCount * MOVEMENT_CLASS_COUNT;
	const auto decode = [&](Uint16* const* first, bool market) {
		const auto offset = std::size_t(slot - first);
		return MapState::planeKey(int(offset / (MaterialSlotCount * MOVEMENT_CLASS_COUNT)),
			int((offset / MOVEMENT_CLASS_COUNT) % MaterialSlotCount), int(offset % MOVEMENT_CLASS_COUNT), market);
	};
	if (const auto* first = &materialGradients[0][0][0]; slot >= first && slot < first + perArray) return decode(first, false);
	if (const auto* first = &marketMaterialGradients[0][0][0]; slot >= first && slot < first + perArray) return decode(first, true);
	return std::nullopt;
}

bool MapRouting::releasePlaneLease(Uint16* const* slot, const Uint16* cells)
{
	const auto key = planeKeyForSlot(slot);
	if (!key) return false;
	auto& lease = gradientRuntime->planeLeases[*key];
	if (!lease || lease->cells != cells) return false;
	// Only the owner creates references, so a count of one proves no capture holds it.
	const bool adopted = lease.use_count() > 1;
	lease->adopted = adopted;
	lease.reset();
	return adopted;
}

void MapRouting::freePlane(Uint16*& slot)
{
	if (!releasePlaneLease(&slot, slot)) delete[] slot;
	slot = nullptr;
}


const MapState::BuildingSeedPlanes& MapRouting::buildingSeedTemplates() const
{
	assert(!map.compute.insideJob());
	return gradientRuntime->buildingSeeds.ensure(map.stateView());
}

bool MapRouting::buildingSeedsCurrent(const MapState::BuildingSeedSnapshot& copy) const
{
	return gradientRuntime->buildingSeeds.current(copy, map.identity());
}

bool MapRouting::buildingSeedDemand() const
{
	const auto& rt = *gradientRuntime;
	// Requests queued during team stepping are staged after the capture;
	// fixtures that stage first leave them reserved instead.
	return rt.buildingSeeds.captureEveryBoundary() || !rt.buildingRequests.empty() || !rt.stagedBuildings.empty();
}

void MapRouting::captureBuildingSeeds(MapState::BuildingSeedSnapshot& copy, Uint64& bytesCopied) const
{
	buildingSeedTemplates();
	std::uint64_t copied = 0;
	gradientRuntime->buildingSeeds.capture(copy, map.identity(), copied);
	bytesCopied += copied;
}
