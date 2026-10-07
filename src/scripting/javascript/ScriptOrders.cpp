// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptOrders.h"
#include "Game.h"
#include "Order.h"
#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "Brush.h"
#include "AIRuleOrders.h"
#include "ai/observation/AIWorldView.h"
#include <stdexcept>
namespace Script
{
std::shared_ptr<Order> order(const AIEngine::AIWorldView &world, int team, const Value &d)
{
	if (d.kind == Value::Null)
		return std::make_shared<NullOrder>();
	if (d.kind != Value::Object)
		throw std::runtime_error("Order requires a record");
	const std::string type = d.string("type");
	auto number = [&](const char *name, int lo = 0, int hi = 65535)
	{ return d.integer(name, lo, hi); };
	if (type == "create")
	{
		int t = number("buildingType", 0, int(world.catalog->size()) - 1);
		const auto *bt = &world.catalog->at(t);
		if (!bt->semantics.placeable || !world.catalog->at(t).available)
			throw std::runtime_error("Creation requires an available placeable building variant");
		std::optional<Sint32> range;
		if (bt->zonable[WORKER] || bt->zonable[EXPLORER] || bt->zonable[WARRIOR])
			range = number("range", 0, bt->maximumRange);
		return std::make_shared<OrderCreate>(
			team, number("x", 0, world.width - 1), number("y", 0, world.height - 1), t,
			std::min(number("workers", 0, MAX_BUILDING_WORKER_REQUEST),int(bt->semantics.assignmentLimit)),
			std::min(number("futureWorkers", 0, MAX_BUILDING_WORKER_REQUEST),int((bt->site?&world.catalog->at(bt->next):bt)->semantics.assignmentLimit)), range);
	}
	if (type == "forbidden" || type == "guardArea" || type == "clearArea" || type == "farmArea")
	{
		std::shared_ptr<OrderAlterArea> a;
		if (type == "forbidden")
			a = std::make_shared<OrderAlterForbidden>();
		else if (type == "guardArea")
			a = std::make_shared<OrderAlterGuardArea>();
		else if (type == "clearArea")
			a = std::make_shared<OrderAlterClearArea>();
		else // farmArea: refused by OrderValidation in a game without the farm-areas experiment
			a = std::make_shared<OrderAlterFarmArea>();
		a->teamNumber = team;
		a->type = number("mode", BrushTool::MODE_ADD, BrushTool::MODE_DEL);
		a->centerX = number("x", 0, world.width - 1);
		a->centerY = number("y", 0, world.height - 1);
		a->minX = 0;
		a->minY = 0;
		a->maxX = a->minX + number("width", 1, 256);
		a->maxY = a->minY + number("height", 1, 256);
		size_t count = size_t(a->maxX - a->minX) * (a->maxY - a->minY);
		const auto &mask = d.get("mask");
		if (mask.kind != Value::Array || mask.items.size() != count)
			throw std::runtime_error("Area mask has incorrect size");
		a->mask.resize(count);
		for (size_t i = 0; i < count; ++i)
		{
			if (mask.items[i].kind != Value::Boolean)
				throw std::runtime_error("Area mask requires booleans");
			a->mask.set(i, mask.items[i].number != 0);
		}
		return a;
	}
	const auto &reference = d.get("building");
	int gid = reference.integer("id", 0, Building::MAX_COUNT * Team::MAX_COUNT - 1);
	if (Building::GIDtoTeam(gid) != team)
		throw std::runtime_error("Order requires an owned building");
	const auto *b = world.buildingAtSlot(gid);
	if (!b || reference.get("generation").kind != Value::Number ||
		reference.get("generation").number != b->identity.generation)
		throw std::runtime_error("Invalid owned building reference");
	if (type == "workers")
		return std::make_shared<OrderModifyBuilding>(
			gid, number("workers", 0, MAX_BUILDING_WORKER_REQUEST));
	if (type == "delete")
		return std::make_shared<OrderDelete>(gid);
	if (type == "cancelDelete")
		return std::make_shared<OrderCancelDelete>(gid);
	// Fail at the script API boundary instead of silently consuming a callback
	// on an unavailable upgrade. The same command still repairs damaged buildings.
	if (type == "construction" && world.rules.upgradesDisabled
		&& !world.catalog->at(b->typeNum).site && b->hp >= b->maxHp)
		throw std::runtime_error("Building upgrades are disabled by game rules");
	if (type == "construction")
		{
        const auto& kind = world.catalog->at(b->typeNum);
        const bool repair = kind.site ? b->constructionResultState == Building::REPAIR : b->hp < b->maxHp;
        const int target = kind.site ? b->typeNum : repair ? kind.previous : kind.next;
        const auto& placement = target >= 0 ? world.catalog->at(target) : kind;
        const int origin = kind.site ? b->constructionOriginTypeNum : b->typeNum;
        const auto& completed = repair && origin >= 0 ? world.catalog->at(origin)
            : placement.site ? world.catalog->at(placement.next) : placement;
        return std::make_shared<OrderConstruction>(gid,
            std::clamp(number("workers", 0, MAX_BUILDING_WORKER_REQUEST), 0, placement.semantics.assignmentLimit),
            std::clamp(number("futureWorkers", 0, MAX_BUILDING_WORKER_REQUEST), 0, completed.semantics.assignmentLimit));
    }
	if (type == "cancelConstruction" && world.catalog->at(b->typeNum).site &&
		(b->buildingState != Building::ALIVE || (b->constructionResultState != Building::UPGRADE &&
												 b->constructionResultState != Building::REPAIR)))
		throw std::runtime_error("Cannot cancel initial building construction; use delete");
	if (type == "cancelConstruction")
		return std::make_shared<OrderCancelConstruction>(
			gid, number("workers", 0, MAX_BUILDING_WORKER_REQUEST));
	if (type == "priority")
		return std::make_shared<OrderChangePriority>(gid, number("priority", -1, 1));
	if (type == "production")
	{
		if (!world.catalog->at(b->typeNum).semantics.production.enabledUnitMask)
			throw std::runtime_error("Building does not produce units");
		const auto &a = d.get("ratios");
		if (a.kind != Value::Array || a.items.size() != NB_UNIT_TYPE)
			throw std::runtime_error("Production requires three ratios");
		Sint32 ratios[NB_UNIT_TYPE];
		for (int i = 0; i < NB_UNIT_TYPE; ++i)
			ratios[i] = Value::object().set("value", a.items[i]).integer("value", 0, 16);
		for (int i = 0; i < NB_UNIT_TYPE; ++i)
			if (ratios[i] && !world.catalog->at(b->typeNum).semantics.production.recipes[i].enabled)
				throw std::runtime_error("Requested unit recipe is unavailable");
		return std::make_shared<OrderModifySwarm>(gid, ratios);
	}
	if (type == "exchange")
	{
		if (!world.catalog->at(b->typeNum).semantics.market.interTeamFruitExchange)
			throw std::runtime_error("Building does not support resource exchange");
		return std::make_shared<OrderModifyExchange>(
			gid, number("receiveMask", 0, (1 << MAX_NB_RESOURCES) - 1),
			number("sendMask", 0, (1 << MAX_NB_RESOURCES) - 1));
	}
	if (!(world.catalog->at(b->typeNum).zonable[WORKER] || world.catalog->at(b->typeNum).zonable[EXPLORER] || world.catalog->at(b->typeNum).zonable[WARRIOR]))
		throw std::runtime_error("Flag order requires a flag");
	if (type == "range")
		return std::make_shared<OrderModifyFlag>(gid, number("range", 0, world.catalog->at(b->typeNum).maximumRange));
	if (type == "minimumLevel")
	{
		if (!world.catalog->at(b->typeNum).zonable[WARRIOR])
			throw std::runtime_error("Minimum combat level requires warrior attraction");
		return std::make_shared<OrderModifyMinLevelToFlag>(gid, number("level", 0, 3));
	}
	if (type == "workerMinimumLevel")
	{
		if (!world.catalog->at(b->typeNum).zonable[WORKER])throw std::runtime_error("Minimum construction level requires worker attraction");
		return std::make_shared<OrderModifyMinLevelToFlag>(gid, number("workerMinimumLevel",0,3), 2);
	}
	if (type == "requireBombing")
	{
		if (!world.catalog->at(b->typeNum).zonable[EXPLORER] || d.get("requireBombing").kind != Value::Boolean)
			throw std::runtime_error("Bombing requirement needs explorer attraction and a boolean");
		return std::make_shared<OrderModifyMinLevelToFlag>(gid, d.get("requireBombing").number != 0, 1);
	}
	if (type == "moveFlag" && !world.catalog->at(b->typeNum).semantics.relocatable)
		throw std::runtime_error("Building cannot relocate");
	if (type == "moveFlag")
		return std::make_shared<OrderMoveFlag>(gid, number("x", 0, world.width - 1),
											   number("y", 0, world.height - 1), false);
	if (type == "clearingResources")
	{
		if (!world.catalog->at(b->typeNum).zonable[WORKER])
			throw std::runtime_error("Building does not attract resource-clearing workers");
		const auto &a = d.get("resources");
		if (a.kind != Value::Array || a.items.size() != BASIC_COUNT)
			throw std::runtime_error("Clearing requires five booleans");
		bool resources[BASIC_COUNT];
		for (int i = 0; i < BASIC_COUNT; ++i)
		{
			if (a.items[i].kind != Value::Boolean)
				throw std::runtime_error("Clearing requires booleans");
			resources[i] = a.items[i].number != 0;
		}
		if (resources[STONE])
			throw std::runtime_error("Clearing flags cannot clear stone");
		return std::make_shared<OrderModifyClearingFlag>(gid, resources);
	}
	throw std::runtime_error("Unsupported gameplay order");
}
std::shared_ptr<Order> order(Game &game, int team, const Value &descriptor)
{
    // Scenario scripts use the same validation/encoding boundary as AI workers.
    const auto world = AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
    return order(*world, team, descriptor);
}
} // namespace Script
