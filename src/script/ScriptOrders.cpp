// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptOrders.h"
#include "Game.h"
#include "Order.h"
#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Brush.h"
#include <stdexcept>
namespace Script
{
std::shared_ptr<Order> order(Game &game, int team, const Value &d)
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
		int t = number("buildingType", 0, int(globalContainer->buildingsTypes.size()) - 1);
		const auto *bt = globalContainer->buildingsTypes.get(t);
		if (!bt->isVirtual && (!bt->isBuildingSite || bt->level != 0))
			throw std::runtime_error("Creation requires a basic building site or flag");
		std::optional<Sint32> range;
		if (bt->isVirtual)
			range = number("range", 0, 255);
		return std::make_shared<OrderCreate>(
			team, number("x", 0, game.map.getW() - 1), number("y", 0, game.map.getH() - 1), t,
			number("workers", 0, MAX_BUILDING_WORKER_REQUEST),
			number("futureWorkers", 0, MAX_BUILDING_WORKER_REQUEST), range);
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
		a->centerX = number("x", 0, game.map.getW() - 1);
		a->centerY = number("y", 0, game.map.getH() - 1);
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
	Building *b = game.teams[team]->myBuildings[Building::GIDtoID(gid)];
	if (!b || reference.get("generation").kind != Value::Number ||
		reference.get("generation").number != b->scriptIdentity)
		throw std::runtime_error("Invalid owned building reference");
	if (type == "workers")
		return std::make_shared<OrderModifyBuilding>(
			gid, number("workers", 0, MAX_BUILDING_WORKER_REQUEST));
	if (type == "delete")
		return std::make_shared<OrderDelete>(gid);
	if (type == "cancelDelete")
		return std::make_shared<OrderCancelDelete>(gid);
	if (type == "construction" && game.gameHeader.isUnitUpgradesDisabled()
		&& !b->type->isBuildingSite && b->hp >= b->getEffectiveMaxHp())
		throw std::runtime_error("Building upgrades are disabled by game rules");
	if (type == "construction")
		return std::make_shared<OrderConstruction>(
			gid, number("workers", 0, MAX_BUILDING_WORKER_REQUEST),
			number("futureWorkers", 0, MAX_BUILDING_WORKER_REQUEST));
	if (type == "cancelConstruction" && b->type->isBuildingSite &&
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
		if (b->shortTypeNum != IntBuildingType::SWARM_BUILDING)
			throw std::runtime_error("Production requires a swarm");
		const auto &a = d.get("ratios");
		if (a.kind != Value::Array || a.items.size() != NB_UNIT_TYPE)
			throw std::runtime_error("Production requires three ratios");
		Sint32 ratios[NB_UNIT_TYPE];
		for (int i = 0; i < NB_UNIT_TYPE; ++i)
			ratios[i] = Value::object().set("value", a.items[i]).integer("value", 0, 16);
		return std::make_shared<OrderModifySwarm>(gid, ratios);
	}
	if (type == "exchange")
	{
		if (b->shortTypeNum != IntBuildingType::MARKET_BUILDING)
			throw std::runtime_error("Exchange requires a market");
		return std::make_shared<OrderModifyExchange>(
			gid, number("receiveMask", 0, (1 << MAX_NB_RESOURCES) - 1),
			number("sendMask", 0, (1 << MAX_NB_RESOURCES) - 1));
	}
	if (!b->type->isVirtual)
		throw std::runtime_error("Flag order requires a flag");
	if (type == "range")
		return std::make_shared<OrderModifyFlag>(gid, number("range", 0, 255));
	if (type == "minimumLevel")
		return std::make_shared<OrderModifyMinLevelToFlag>(gid, number("level", 0, 3));
	if (type == "moveFlag")
		return std::make_shared<OrderMoveFlag>(gid, number("x", 0, game.map.getW() - 1),
											   number("y", 0, game.map.getH() - 1), false);
	if (type == "clearingResources")
	{
		if (b->shortTypeNum != IntBuildingType::CLEARING_FLAG)
			throw std::runtime_error("Clearing requires a clearing flag");
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
} // namespace Script
