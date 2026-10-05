// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <BinaryStream.h>
#include <TextStream.h>
#include <charconv>
#include <stdexcept>
#include "FileFormatVersions.h"
#include "Game.h"
#include "Order.h"
#include "Building.h"
#include <algorithm>

namespace AIStateSerialization
{
// Older controllers routinely requested workers after completion even for
// unstaffed variants. Import that historical queue against the frozen catalog;
// modern orders still undergo the ordinary strict validation unchanged.
inline void normalizeLegacyOrderStaffing(Game& game, Order& order, Sint32 versionMinor)
{
    if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) return;
    const BuildingType* placement = nullptr;
    if (order.getOrderType() == ORDER_CREATE)
    {
        auto& create = static_cast<OrderCreate&>(order);
        if (create.typeNum < 0 || std::size_t(create.typeNum) >= game.buildingsTypes.size()
            || create.unitWorking < 0 || create.unitWorking > MAX_BUILDING_WORKER_REQUEST
            || create.unitWorkingFuture < 0 || create.unitWorkingFuture > MAX_BUILDING_WORKER_REQUEST) return;
        placement = game.buildingsTypes.get(create.typeNum);
        const auto* completed = placement->isBuildingSite && placement->nextLevel >= 0
            ? game.buildingsTypes.get(placement->nextLevel) : placement;
        create.unitWorking = std::min(create.unitWorking, placement->semantics.assignmentLimit);
        create.unitWorkingFuture = std::min(create.unitWorkingFuture, completed->semantics.assignmentLimit);
    }
    else if (order.getOrderType() == ORDER_CONSTRUCTION)
    {
        auto& construction = static_cast<OrderConstruction&>(order);
        if (construction.gid >= Building::MAX_COUNT * Team::MAX_COUNT
            || construction.unitWorking > MAX_BUILDING_WORKER_REQUEST || construction.unitWorkingFuture > MAX_BUILDING_WORKER_REQUEST) return;
        const auto* team = game.teams[Building::GIDtoTeam(construction.gid)];
        const auto* building = team ? team->myBuildings[Building::GIDtoID(construction.gid)] : nullptr;
        if (!building) return;
        int target = building->typeNum;
        if (!building->type->isBuildingSite)
            target = building->hp < building->getEffectiveMaxHp() ? building->type->prevLevel : building->type->nextLevel;
        if (target < 0 || std::size_t(target) >= game.buildingsTypes.size()) return;
        placement = game.buildingsTypes.get(target);
        const auto* completed = placement->isBuildingSite && placement->nextLevel >= 0
            ? game.buildingsTypes.get(placement->nextLevel) : placement;
        construction.unitWorking = std::min(construction.unitWorking, Uint32(placement->semantics.assignmentLimit));
        construction.unitWorkingFuture = std::min(construction.unitWorkingFuture, Uint32(completed->semantics.assignmentLimit));
    }
}

// Legacy text numeric reads accept partial values and leave missing values
// uninitialized. Continuation fields must reject either case explicitly.
inline Sint32 readSint32(GAGCore::InputStream *stream, const char *name)
{
	if (dynamic_cast<GAGCore::TextInputStream *>(stream))
	{
		const auto encoded = stream->readText(name);
		Sint32 value;
		const auto result = std::from_chars(encoded.data(), encoded.data() + encoded.size(), value);
		if (result.ec != std::errc{} || result.ptr != encoded.data() + encoded.size())
			throw std::runtime_error(std::string("Invalid AI continuation field: ") + name);
		return value;
	}
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	return stream->readSint32(name);
}
}
