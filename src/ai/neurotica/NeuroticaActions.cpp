// SPDX-License-Identifier: GPL-3.0-or-later
#include "NeuroticaActions.h"
#include "NeuroticaObservation.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Order.h"
#include "Team.h"
#include "Brush.h"
#include "WinProbability.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <zlib.h>
namespace Neurotica
{
	namespace
	{
		void put(std::vector<Uint8> &b, Sint32 v)
		{
			Uint32 u = Uint32(v);
			for (int i = 0; i < 4; ++i)
				b.push_back(Uint8(u >> (8 * i)));
		}
		Sint32 get(const std::vector<Uint8> &b, size_t p)
		{
			if (p + 4 > b.size())
				throw std::runtime_error("Neurotica: truncated action");
			Uint32 u = 0;
			for (int i = 0; i < 4; ++i)
				u |= Uint32(b[p + i]) << (8 * i);
			return Sint32(u);
		}
		BuildingType *placeType(int type, Sint32 &id)
		{
			if (type < 0 || type >= IntBuildingType::NB_BUILDING)
				return nullptr;
			id = globalContainer->buildingsTypes.getPlaceableTypeNum(
				IntBuildingType::reverseConversionMap[type]);
			return id < 0 ? nullptr : globalContainer->buildingsTypes.get(id);
		}
		bool legal(Team *team, int type, int x, int y)
		{
			Map &m = team->game->map;
			Sint32 id = -1;
			auto *bt = placeType(type, id);
			return bt && x >= 0 && y >= 0 && x < m.getW() && y < m.getH() &&
				   (bt->isVirtual || m.isHardSpaceForBuilding(x, y, bt->width, bt->height));
		}
	} // namespace
	std::vector<Uint8> actionContext(Team *team)
	{
		Map &m = team->game->map;
		std::vector<Uint8> b = {'N', 'A', 'C', '1'}, s, d;
		std::vector<Building *> entities;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
		{
			Building *e = team->myBuildings[i];
			if (e && e->type && e->buildingState != Building::DEAD)
				entities.push_back(e);
		}
		int phi = 500;
		// A shaping potential need not be calibrated. Use the scorer from the
		// opening too, but do NOT change its minimum tick for victory adjudication.
		// Centering and zero terminal potential are enforced in the learner.
		{
			std::vector<int> alliances;
			auto p = WinProbability::permille(WinProbability::slotsOf(*team->game, alliances));
			if (size_t(team->teamNumber) < alliances.size())
				phi = p.at(alliances[team->teamNumber]);
		}
		for (Sint32 v :
			 {m.getW(), m.getH(), Sint32(team->teamNumber), Sint32(team->game->stepCounter),
			  Sint32(entities.size()), Sint32(phi), Sint32(DP_COUNT),
			  Sint32(std::getenv("GLOB2_NEUROTICA_GAME_ID")
						 ? std::strtol(std::getenv("GLOB2_NEUROTICA_GAME_ID"), nullptr, 10)
						 : 0)})
			put(b, v);
		if (!encodeStaticPlanes(&m, s) || !encodeDynamicPlanes(team, d))
			throw std::runtime_error("Neurotica: observation encoding failed");
		b.insert(b.end(), s.begin(), s.end());
		b.insert(b.end(), d.begin(), d.end());
		for (Building *e : entities)
		{
			std::array<Sint32, ENTITY_FIELDS> row{
				{e->gid, e->posX, e->posY, e->type->shortTypeNum, e->type->level,
				 e->type->isBuildingSite, e->type->isVirtual, e->buildingState,
				 e->constructionResultState, e->maxUnitWorking, Sint32(e->unitsWorking.size()),
				 e->hp, e->ratio[0], e->ratio[1], e->ratio[2], e->unitStayRange, e->priority,
				 e->minLevelToFlag}};
			for (int i = 0; i < 8; ++i)
				row[18 + i] = e->resources[i];
			row[26] = e->underAttackTimer;
			row[27] = e->productionTimeout;
			const bool repair = e->hp < e->getEffectiveMaxHp();
			row[28] = e->buildingState == Building::ALIVE && !e->type->isBuildingSite &&
					  !e->type->isVirtual &&
					  (repair ? e->type->prevLevel : e->type->nextLevel) != BUILDING_LEVEL_NONE &&
					  e->isHardSpaceForBuildingSite(repair ? Building::REPAIR : Building::UPGRADE);
			for (Sint32 v : row)
				put(b, v);
		}
		// Environment-only type-conditioned masks. No model scores or count caps.
		for (int t = 0; t < 13; ++t)
			for (int y = 0; y < m.getH(); ++y)
				for (int x = 0; x < m.getW(); ++x)
					b.push_back(legal(team, t, x, y));
		for (int y = 0; y < m.getH(); ++y)
			for (int x = 0; x < m.getW(); ++x)
				b.push_back(1); // Flag movement is permitted anywhere by the engine.
		return b;
	}
	Action encodeAction(Team *team, Order &order)
	{
		Action a;
		auto &v = a.v;
		// Typed access keeps the recorder independent of network byte layouts.
		switch (order.getOrderType())
		{
		case ORDER_NULL:
			break;
		case ORDER_SET_ALLIANCE:
		{
			auto &o = static_cast<SetAllianceOrder &>(order);
			if (o.alliedMask != team->allies || o.enemyMask != team->enemies ||
				(o.visionExchangeMask | o.visionFoodMask | o.visionOtherMask) > 3)
				throw std::runtime_error("Neurotica: training requires fixed two-team alliances");
			v[OP] = SHARING;
			v[R0] = o.visionExchangeMask;
			v[R1] = o.visionFoodMask;
			v[R2] = o.visionOtherMask;
			break;
		}
		case ORDER_CREATE:
		{
			auto &o = static_cast<OrderCreate &>(order);
			v[OP] = CREATE;
			auto *t = globalContainer->buildingsTypes.get(o.typeNum);
			Sint32 id = -1;
			placeType(t->shortTypeNum, id);
			if (id != o.typeNum)
				throw std::runtime_error("Neurotica corpus: non-placeable create type");
			v[TYPE] = t->shortTypeNum;
			v[X] = team->game->map.normalizeX(o.posX);
			v[Y] = team->game->map.normalizeY(o.posY);
			if (!legal(team, v[TYPE], v[X], v[Y]))
			{
				std::cerr << "NEUROTICA_TEACHER_NO_EFFECT tick=" << team->game->stepCounter
						  << " op=create" << std::endl;
				return Action();
			}
			v[WORKERS] = o.unitWorking;
			v[FUTURE] = o.unitWorkingFuture;
			v[FLAG_RADIUS] = o.flagRadius ? *o.flagRadius : 256;
			break;
		}
		case ORDER_DELETE:
			v[OP] = DELETE_BUILDING;
			v[SOURCE] = static_cast<OrderDelete &>(order).gid;
			break;
		case ORDER_CANCEL_DELETE:
			v[OP] = CANCEL_DELETE;
			v[SOURCE] = static_cast<OrderCancelDelete &>(order).gid;
			break;
		case ORDER_CONSTRUCTION:
		{
			auto &o = static_cast<OrderConstruction &>(order);
			v[OP] = CONSTRUCT;
			v[SOURCE] = o.gid;
			v[WORKERS] = o.unitWorking;
			v[FUTURE] = o.unitWorkingFuture;
			break;
		}
		case ORDER_CANCEL_CONSTRUCTION:
		{
			auto &o = static_cast<OrderCancelConstruction &>(order);
			v[OP] = CANCEL_CONSTRUCTION;
			v[SOURCE] = o.gid;
			v[WORKERS] = o.unitWorking;
			break;
		}
		case ORDER_MODIFY_BUILDING:
		{
			auto &o = static_cast<OrderModifyBuilding &>(order);
			v[OP] = STAFF;
			v[SOURCE] = o.gid;
			v[WORKERS] = o.numberRequested;
			break;
		}
		case ORDER_MODIFY_SWARM:
		{
			auto &o = static_cast<OrderModifySwarm &>(order);
			v[OP] = MIX;
			v[SOURCE] = o.gid;
			for (int j = 0; j < 3; ++j)
				v[R0 + j] = o.ratio[j];
			break;
		}
		case ORDER_MODIFY_FLAG:
		{
			auto &o = static_cast<OrderModifyFlag &>(order);
			v[OP] = RADIUS;
			v[SOURCE] = o.gid;
			v[FLAG_RADIUS] = o.range;
			break;
		}
		case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
		{
			auto &o = static_cast<OrderModifyMinLevelToFlag &>(order);
			v[OP] = MIN_LEVEL;
			v[SOURCE] = o.gid;
			v[FLAG_LEVEL] = o.minLevelToFlag;
			break;
		}
		case ORDER_CHANGE_PRIORITY:
		{
			auto &o = static_cast<OrderChangePriority &>(order);
			v[OP] = PRIORITY;
			v[SOURCE] = o.gid;
			v[BUILD_PRIORITY] = o.priority + 1;
			break;
		}
		case ORDER_MOVE_FLAG:
		{
			auto &o = static_cast<OrderMoveFlag &>(order);
			v[OP] = MOVE_FLAG;
			v[SOURCE] = o.gid;
			v[X] = o.x;
			v[Y] = o.y;
			v[DROP] = o.drop;
			break;
		}
		case ORDER_MODIFY_CLEARING_FLAG:
		{
			auto &o = static_cast<OrderModifyClearingFlag &>(order);
			v[OP] = CLEAR_RESOURCES;
			v[SOURCE] = o.gid;
			for (int j = 0; j < BASIC_COUNT; ++j)
				if (o.clearingResources[j])
					v[CLEAR_MASK] |= 1 << j;
			break;
		}
		case ORDER_MODIFY_EXCHANGE:
		{
			auto &o = static_cast<OrderModifyExchange &>(order);
			v[OP] = EXCHANGE;
			v[SOURCE] = o.gid;
			v[RECEIVE_MASK] = o.receiveResourceMask;
			v[SEND_MASK] = o.sendResourceMask;
			break;
		}
		case ORDER_ALTER_GUARD_AREA:
		case ORDER_ALTER_CLEAR_AREA:
		case ORDER_ALTER_FORBIDDEN:
		{
			auto &o = static_cast<OrderAlterArea &>(order);
			v[OP] = order.getOrderType() == ORDER_ALTER_GUARD_AREA
						? GUARD_AREA
						: (order.getOrderType() == ORDER_ALTER_CLEAR_AREA ? CLEAR_AREA
																		  : FORBIDDEN_AREA);
			v[AREA_MODE] = o.type == BrushTool::MODE_ADD ? 1 : 0;
			Map &m = team->game->map;
			a.area.assign(size_t(m.getW()) * m.getH(), 0);
			for (int y = o.minY; y < o.maxY; ++y)
				for (int x = o.minX; x < o.maxX; ++x)
					if (o.mask.get(size_t(y - o.minY) * (o.maxX - o.minX) + x - o.minX))
						a.area[size_t(m.normalizeY(o.centerY + y)) * m.getW() +
							   m.normalizeX(o.centerX + x)] = 1;
			break;
		}
		default:
			throw std::runtime_error("Neurotica corpus: unsupported gameplay order " +
									 std::to_string(order.getOrderType()));
		}
		return a;
	}
	std::shared_ptr<Order> decodeAction(Team *team, const Action &a)
	{
		const auto &v = a.v;
		Map &m = team->game->map;
		auto fail = []()
		{ throw std::runtime_error("NEUROTICA_POLICY_FAILURE invalid semantic action"); };
		if (v[OP] < 0 || v[OP] >= OP_COUNT || v[DELAY] < 1 || v[DELAY] > 25)
			fail();
		Building *e = nullptr;
		if (v[OP] >= DELETE_BUILDING && v[OP] <= EXCHANGE)
		{
			for (int i = 0; i < Building::MAX_COUNT; ++i)
				if (team->myBuildings[i] && team->myBuildings[i]->gid == v[SOURCE])
					e = team->myBuildings[i];
			if (!e || e->buildingState == Building::DEAD)
				fail();
			if ((v[OP] == MIX && e->type->shortTypeNum != 0) ||
				((v[OP] == RADIUS || v[OP] == MIN_LEVEL || v[OP] == MOVE_FLAG) &&
				 !e->type->isVirtual) ||
				(v[OP] == CLEAR_RESOURCES && e->type->shortTypeNum != 10) ||
				(v[OP] == EXCHANGE && e->type->shortTypeNum != 12))
				fail();
		}
		for (int f : {WORKERS, FUTURE, R0, R1, R2, FLAG_LEVEL, CLEAR_MASK, RECEIVE_MASK, SEND_MASK})
			if (v[f] < 0 || v[f] > 255)
				fail();
		if (v[FLAG_RADIUS] < 0 || v[FLAG_RADIUS] > 256 || v[BUILD_PRIORITY] < 0 ||
			v[BUILD_PRIORITY] > 2 || v[DROP] < 0 || v[DROP] > 1 || v[AREA_MODE] < 0 ||
			v[AREA_MODE] > 1)
			fail();
		switch (v[OP])
		{
		case SHARING:
			if (v[R0] > 3 || v[R1] > 3 || v[R2] > 3)
				fail();
			return std::make_shared<SetAllianceOrder>(team->teamNumber, team->allies, team->enemies,
													  v[R0], v[R1], v[R2]);
		case HOLD:
			return std::make_shared<NullOrder>();
		case CREATE:
		{
			if (!legal(team, v[TYPE], v[X], v[Y]))
				fail();
			Sint32 id = -1;
			placeType(v[TYPE], id);
			std::optional<Sint32> r;
			if (v[FLAG_RADIUS] != 256)
				r = v[FLAG_RADIUS];
			return std::make_shared<OrderCreate>(team->teamNumber, v[X], v[Y], id, v[WORKERS],
												 v[FUTURE], r);
		}
		case DELETE_BUILDING:
			return std::make_shared<OrderDelete>(e->gid);
		case CANCEL_DELETE:
			return std::make_shared<OrderCancelDelete>(e->gid);
		case CONSTRUCT:
			if (e->buildingState != Building::ALIVE || e->type->isBuildingSite ||
				e->type->isVirtual)
				fail();
			{
				bool repair = e->hp < e->getEffectiveMaxHp();
				if ((repair ? e->type->prevLevel : e->type->nextLevel) == BUILDING_LEVEL_NONE ||
					!e->isHardSpaceForBuildingSite(repair ? Building::REPAIR : Building::UPGRADE))
					fail();
			}
			return std::make_shared<OrderConstruction>(e->gid, v[WORKERS], v[FUTURE]);
		case CANCEL_CONSTRUCTION:
			return std::make_shared<OrderCancelConstruction>(e->gid, v[WORKERS]);
		case STAFF:
			if (v[WORKERS] > MAX_BUILDING_WORKER_REQUEST)
				fail();
			return std::make_shared<OrderModifyBuilding>(e->gid, v[WORKERS]);
		case MIX:
		{
			Sint32 r[3] = {v[R0], v[R1], v[R2]};
			return std::make_shared<OrderModifySwarm>(e->gid, r);
		}
		case RADIUS:
			if (v[FLAG_RADIUS] > 255)
				fail();
			return std::make_shared<OrderModifyFlag>(e->gid, v[FLAG_RADIUS]);
		case MIN_LEVEL:
			return std::make_shared<OrderModifyMinLevelToFlag>(e->gid, v[FLAG_LEVEL]);
		case PRIORITY:
			return std::make_shared<OrderChangePriority>(e->gid, v[BUILD_PRIORITY] - 1);
		case MOVE_FLAG:
			if (v[X] < 0 || v[Y] < 0 || v[X] >= m.getW() || v[Y] >= m.getH())
				fail();
			return std::make_shared<OrderMoveFlag>(e->gid, v[X], v[Y], v[DROP]);
		case CLEAR_RESOURCES:
		{
			bool r[BASIC_COUNT];
			for (int j = 0; j < BASIC_COUNT; ++j)
				r[j] = v[CLEAR_MASK] & (1 << j);
			return std::make_shared<OrderModifyClearingFlag>(e->gid, r);
		}
		case EXCHANGE:
			return std::make_shared<OrderModifyExchange>(e->gid, v[RECEIVE_MASK], v[SEND_MASK]);
		default:
		{
			if (a.area.size() != size_t(m.getW()) * m.getH() ||
				m.getW() > ORDER_AREA_BRUSH_MAX_SIDE || m.getH() > ORDER_AREA_BRUSH_MAX_SIDE)
				fail();
			std::shared_ptr<OrderAlterArea> o;
			if (v[OP] == GUARD_AREA)
				o = std::make_shared<OrderAlterGuardArea>();
			else if (v[OP] == CLEAR_AREA)
				o = std::make_shared<OrderAlterClearArea>();
			else
				o = std::make_shared<OrderAlterForbidden>();
			o->teamNumber = team->teamNumber;
			o->type = v[AREA_MODE] ? BrushTool::MODE_ADD : BrushTool::MODE_DEL;
			o->centerX = o->centerY = o->minX = o->minY = 0;
			o->maxX = m.getW();
			o->maxY = m.getH();
			o->mask = Utilities::BitArray(a.area.size(), false);
			for (size_t i = 0; i < a.area.size(); ++i)
			{
				if (a.area[i] > 1)
					fail();
				o->mask.set(i, a.area[i]);
			}
			return o;
		}
		}
	}
	std::vector<Uint8> packAction(const Action &a)
	{
		std::vector<Uint8> b;
		for (auto v : a.v)
			put(b, v);
		b.insert(b.end(), a.area.begin(), a.area.end());
		return b;
	}
	Action unpackAction(const std::vector<Uint8> &b)
	{
		Action a;
		for (int i = 0; i < FIELD_COUNT; ++i)
			a.v[i] = get(b, i * 4);
		a.area.assign(b.begin() + FIELD_COUNT * 4, b.end());
		return a;
	}
	void recordAction(Team *team, int player, Order &order)
	{
		const char *path = std::getenv("GLOB2_NEUROTICA_ACTION_CORPUS");
		if (!path)
			return;
		// Record every issued order plus periodic holds. Labels describe orders,
		// never effects 500 ticks later. Capture before engine execution.
		if (order.getOrderType() == ORDER_NULL && team->game->stepCounter % 25 != 0)
			return;
		auto context = actionContext(team);
		auto action = packAction(encodeAction(team, order));
		std::vector<Uint8> raw;
		put(raw, Sint32(context.size()));
		raw.insert(raw.end(), context.begin(), context.end());
		raw.insert(raw.end(), action.begin(), action.end());
		uLongf n = compressBound(raw.size());
		std::vector<Uint8> z(n);
		if (compress2(z.data(), &n, raw.data(), raw.size(), 1) != Z_OK)
			throw std::runtime_error("Neurotica corpus compression failed");
		std::ofstream f(std::string(path) + ".p" + std::to_string(player) + ".nac",
						std::ios::binary | std::ios::app);
		std::vector<Uint8> len;
		put(len, Sint32(n));
		f.write(reinterpret_cast<char *>(len.data()), 4);
		f.write(reinterpret_cast<char *>(z.data()), n);
		if (!f)
			throw std::runtime_error("Neurotica corpus write failed");
	}
} // namespace Neurotica
