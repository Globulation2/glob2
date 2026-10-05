// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "OrderValidation.h"

#include "Brush.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Team.h"
#include "GameGUI.h"

namespace OrderValidation
{
namespace
{
constexpr Result accepted() { return {}; }
constexpr Result rejected(Reason reason) { return {Verdict::Rejected, reason}; }
constexpr Result stale() { return {Verdict::Stale, Reason::BadState}; }

/// What every check needs: the game and the sender's team, taken from the player
/// the relay sequenced the order for.
struct Context
{
	const Game& game;
	int team;
	const Team& teamRef() const { return *game.teams[team]; }
};

bool inRange(Sint64 value, Sint64 low, Sint64 high) { return value >= low && value <= high; }

/// A building id of the sender's own team (the user interface only issues building
/// orders for buildings of the local team). `building` is null when no building has
/// that id any more, which the executors already treat as a no-op.
Result ownBuilding(const Context& c, Uint16 gid, const Building*& building)
{
	building = nullptr;
	if (gid >= Building::MAX_COUNT * Team::MAX_COUNT || Building::GIDtoTeam(gid) != c.team)
		return rejected(Reason::ForeignBuilding);
	building = c.teamRef().myBuildings[Building::GIDtoID(gid)];
	return accepted();
}

/// The sender's own team, as a team field in the order.
Result ownTeam(const Context& c, Sint64 teamNumber)
{
	return teamNumber == c.team ? accepted() : rejected(Reason::WrongTeam);
}

/// A mask of team bits: no bit for a team the map does not have.
bool validTeamMask(const Context& c, Uint32 mask)
{
	const int teams = c.game.mapHeader.getNumberOfTeams();
	return teams >= 32 || (mask >> teams) == 0;
}

Result checkCreate(const Context& c, const OrderCreate& o)
{
	if (Result r = ownTeam(c, o.teamNumber); r.verdict != Verdict::Accepted)
		return r;
	if (!c.game.isBuildingTypeAvailable(o.typeNum))
		return rejected(Reason::BadBuildingType);
	const BuildingType* type = globalContainer->buildingsTypes.get(o.typeNum);
	// What GameGUIToolManager::placeBuildingAt and ScriptOrders create: a level-0
	// building site, or a level-0 flag (which has no site).
	if (!type || type->level != 0 || !(type->isBuildingSite || type->isVirtual))
		return rejected(Reason::BadBuildingType);
	if (!inRange(o.unitWorking, 0, MAX_BUILDING_WORKER_REQUEST) ||
	    !inRange(o.unitWorkingFuture, 0, MAX_BUILDING_WORKER_REQUEST))
		return rejected(Reason::OutOfRange);
	// posX/posY are masked to the map by executeCreate. The radius only applies to
	// flags; for other buildings the client sends 0 and the executor ignores it.
	if (type->isVirtual && o.flagRadius && !inRange(*o.flagRadius, 0, MAX_CREATE_FLAG_RADIUS))
		return rejected(Reason::OutOfRange);
	return accepted();
}

Result checkAlterArea(const Context& c, const OrderAlterArea& o)
{
	if (Result r = ownTeam(c, o.teamNumber); r.verdict != Verdict::Accepted)
		return r;
	if (o.type != BrushTool::MODE_ADD && o.type != BrushTool::MODE_DEL)
		return rejected(Reason::BadMode);
	// The box size and the mask length were checked when the order was decoded
	// (OrderAlterArea::setData), and the executors wrap every cell to the map.
	return accepted();
}
}

const char* name(Reason reason)
{
	switch (reason)
	{
	case Reason::None: return "none";
	case Reason::Undecodable: return "undecodable";
	case Reason::NotPermitted: return "not_permitted";
	case Reason::WrongTeam: return "wrong_team";
	case Reason::WrongPlayer: return "wrong_player";
	case Reason::ForeignBuilding: return "foreign_building";
	case Reason::BadBuildingType: return "bad_building_type";
	case Reason::OutOfRange: return "out_of_range";
	case Reason::BadMode: return "bad_mode";
	case Reason::BadVoice: return "bad_voice";
	case Reason::BadState: return "bad_state";
	case Reason::PauseLimit: return "pause_limit";
	case Reason::Count: break;
	}
	return "unknown";
}

const char* name(Verdict verdict)
{
	switch (verdict)
	{
	case Verdict::Accepted: return "accepted";
	case Verdict::Stale: return "stale";
	case Verdict::Rejected: return "rejected";
	}
	return "unknown";
}

Result validate(const Game& game, int senderPlayer, Order& order)
{
	if (senderPlayer < 0 || senderPlayer >= game.gameHeader.getNumberOfPlayers() || !game.players[senderPlayer])
		return rejected(Reason::WrongPlayer);
	const int team = game.players[senderPlayer]->teamNumber;
	if (team < 0 || team >= game.mapHeader.getNumberOfTeams() || !game.teams[team])
		return rejected(Reason::WrongPlayer);
	const Context c{game, team};
	const Building* b = nullptr;

	switch (order.getOrderType())
	{
	case ORDER_NULL:
		return accepted();

	case ORDER_CREATE:
		return checkCreate(c, static_cast<OrderCreate&>(order));

	case ORDER_DELETE:
		return ownBuilding(c, static_cast<OrderDelete&>(order).gid, b);

	case ORDER_CANCEL_DELETE:
	{
		if (Result r = ownBuilding(c, static_cast<OrderCancelDelete&>(order).gid, b); r.verdict != Verdict::Accepted)
			return r;
		// Building::cancelDelete makes any building ALIVE again, a dead one included.
		if (b && b->buildingState != Building::WAITING_FOR_DESTRUCTION)
			return stale();
		return accepted();
	}

	case ORDER_CONSTRUCTION:
	{
		const auto& o = static_cast<OrderConstruction&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		// The values travel as Uint32 and are applied as Sint32 worker counts.
		if (o.unitWorking > Uint32(MAX_BUILDING_WORKER_REQUEST) || o.unitWorkingFuture > Uint32(MAX_BUILDING_WORKER_REQUEST))
			return rejected(Reason::OutOfRange);
		// Construction is also the repair command. Use rule-adjusted maximum HP
		// to reject only a healthy building's upgrade, including fortress games.
		if (c.game.gameHeader.isUnitUpgradesDisabled() && b && !b->type->isBuildingSite
			&& b->hp >= b->getEffectiveMaxHp()) return rejected(Reason::BadState);
		if (b && !b->type->isBuildingSite && b->hp >= b->getEffectiveMaxHp()
			&& b->type->shortTypeNum == IntBuildingType::MARKET_BUILDING
			&& !b->isUpgradeAvailable()) return rejected(Reason::BadState);
		return accepted();
	}

	case ORDER_CANCEL_CONSTRUCTION:
	{
		// Executed as an OrderConstruction (Game::executeOrder); only gid is read, and
		// the unit count is ignored by Building::cancelConstruction.
		if (Result r = ownBuilding(c, static_cast<OrderCancelConstruction&>(order).gid, b); r.verdict != Verdict::Accepted)
			return r;
		// Building::cancelConstruction asserts on a site that is not an upgrade or a
		// repair (a new building's site) and on a site without the level to return to.
		if (b && b->type->isBuildingSite)
		{
			const int target = b->constructionResultState == Building::UPGRADE  ? b->type->prevLevel
			                   : b->constructionResultState == Building::REPAIR ? b->type->nextLevel
			                                                                    : BUILDING_LEVEL_NONE;
			if (b->buildingState != Building::ALIVE || target == BUILDING_LEVEL_NONE)
				return stale();
		}
		return accepted();
	}

	case ORDER_CHANGE_PRIORITY:
	{
		const auto& o = static_cast<OrderChangePriority&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		return inRange(o.priority, -1, 1) ? accepted() : rejected(Reason::OutOfRange);
	}

	case ORDER_MODIFY_BUILDING:
	{
		const auto& o = static_cast<OrderModifyBuilding&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		return o.numberRequested <= MAX_BUILDING_WORKER_REQUEST ? accepted() : rejected(Reason::OutOfRange);
	}

	case ORDER_MODIFY_EXCHANGE:
		// The masks are only ever tested bit by bit; any value is harmless.
		return ownBuilding(c, static_cast<OrderModifyExchange&>(order).gid, b);

	case ORDER_MODIFY_SWARM:
	{
		const auto& o = static_cast<OrderModifySwarm&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		for (int i = 0; i < NB_UNIT_TYPE; ++i)
			if (!inRange(o.ratio[i], 0, MAX_RATIO_RANGE))
				return rejected(Reason::OutOfRange);
		return accepted();
	}

	case ORDER_MODIFY_FLAG:
	{
		const auto& o = static_cast<OrderModifyFlag&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		// GameGUI::requestFlagRange clamps to the flag type's maximum.
		const int maximum = b && b->type->defaultUnitStayRange ? b->type->maxUnitStayRange : MAX_CREATE_FLAG_RADIUS;
		return inRange(o.range, 0, maximum) ? accepted() : rejected(Reason::OutOfRange);
	}

	case ORDER_MODIFY_CLEARING_FLAG:
		// Decoded as booleans; nothing else to check.
		return ownBuilding(c, static_cast<OrderModifyClearingFlag&>(order).gid, b);

	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
	{
		const auto& o = static_cast<OrderModifyMinLevelToFlag&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		// A war flag's level row (0..NB_UNIT_LEVELS-1) or an exploration flag's option.
		return o.minLevelToFlag < NB_UNIT_LEVELS ? accepted() : rejected(Reason::OutOfRange);
	}

	case ORDER_MOVE_FLAG:
	{
		const auto& o = static_cast<OrderMoveFlag&>(order);
		if (Result r = ownBuilding(c, o.gid, b); r.verdict != Verdict::Accepted)
			return r;
		// Game::executeMoveFlag stores the position unwrapped; the user interface
		// always sends a wrapped one (Map::cursorToBuildingPos).
		if (!inRange(o.x, 0, game.map.getW() - 1) || !inRange(o.y, 0, game.map.getH() - 1))
			return rejected(Reason::OutOfRange);
		return accepted();
	}

	case ORDER_ALTER_FORBIDDEN:
	case ORDER_ALTER_GUARD_AREA:
	case ORDER_ALTER_CLEAR_AREA:
		return checkAlterArea(c, static_cast<OrderAlterArea&>(order));

	case ORDER_ALTER_FARM_AREA:
		// Only a game carrying the farm-areas experiment shows the farm brush.
		if (!game.gameHeader.hasExperiment(ExperimentId::FarmAreas))
			return rejected(Reason::NotPermitted);
		return checkAlterArea(c, static_cast<OrderAlterArea&>(order));

	case ORDER_TEXT_MESSAGE:
	{
		// GameGUI::executeOrder asserts on any other type; the text itself was checked
		// when the order was decoded (MessageOrder::setData).
		const auto t = static_cast<MessageOrder&>(order).messageOrderType;
		if (t != MessageOrder::NORMAL_MESSAGE_TYPE && t != MessageOrder::PRIVATE_MESSAGE_TYPE)
			return rejected(Reason::BadMode);
		return accepted();
	}

	case ORDER_VOICE_DATA:
	{
		const auto& o = static_cast<OrderVoiceData&>(order);
		if (o.frameCount > MAX_VOICE_FRAMES)
			return rejected(Reason::BadVoice);
		return accepted();
	}

	case ORDER_SET_ALLIANCE:
	{
		const auto& o = static_cast<SetAllianceOrder&>(order);
		if (Result r = ownTeam(c, o.teamNumber); r.verdict != Verdict::Accepted)
			return r;
		for (Uint32 mask : {o.alliedMask, o.enemyMask, o.visionExchangeMask, o.visionFoodMask, o.visionOtherMask})
			if (!validTeamMask(c, mask))
				return rejected(Reason::OutOfRange);
		return accepted();
	}

	case ORDER_MAP_MARK:
		// Only the team is used to index anything (GameGUI::addMark's colour).
		return ownTeam(c, static_cast<MapMarkOrder&>(order).teamNumber);

	case ORDER_PAUSE_GAME:
		// Any player may pause and resume a multiplayer game, as in legacy games. A
		// match's pause limit (MatchSetup::pauseLimit) is applied after this check by
		// TurnLockstepSession, which keeps the pause bookkeeping.
		return accepted();

	case ORDER_PLAYER_QUIT_GAME:
		return static_cast<PlayerQuitsGameOrder&>(order).player == senderPlayer ? accepted()
		                                                                      : rejected(Reason::WrongPlayer);

	case ORDER_ADJUST_LATENCY:
		// NetEngine's latency handshake; the relay drops it, clients never send it.
		return rejected(Reason::NotPermitted);

	default:
		return rejected(Reason::Undecodable);
	}
}

void Audit::record(int seat, std::uint32_t tick, bool voice, Result result)
{
	if (seat < 0 || static_cast<std::size_t>(seat) >= SEATS)
		return;
	SeatAudit& s = seats[seat];
	if (voice)
	{
		// Voice never reaches the match record, so the verifier cannot count it.
		if (result.verdict != Verdict::Accepted)
			++s.voiceRejected;
		return;
	}
	switch (result.verdict)
	{
	case Verdict::Accepted:
		++s.accepted;
		return;
	case Verdict::Stale:
		++s.stale;
		break;
	case Verdict::Rejected:
		++s.rejected;
		if (s.firstRejectedTick == UINT32_MAX)
			s.firstRejectedTick = tick;
		break;
	}
	++s.reasons[static_cast<std::size_t>(result.reason)];
}

std::uint32_t Audit::totalRejected() const
{
	std::uint32_t total = 0;
	for (const auto& s : seats)
		total += s.rejected;
	return total;
}
}
