// SPDX-License-Identifier: GPL-3.0-or-later
// AI orders are valid: every order a registered AI hands to the engine passes the
// same OrderValidation::validate a turn game applies to human orders, checked
// against the exact state the order executes against. Accepted and Stale are fine
// (Stale is what network latency can also produce); Rejected means the AI issued
// an order no unmodified client sends, including one acting for another team
// (WrongTeam) or on another team's building (ForeignBuilding).
//
// The games run through glob2test::stepAIValidated, the engine's production AI
// path (observation, admission, the header's AI order delay, the pipeline's own
// stale-target withdrawal), with both seats played by the AI under test on a
// small seeded two-colony torus. Replay one seed with GLOB2_SEED=<n>; soak with
// GLOB2_PROPERTY_ITERS=<k>.
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "Map.h"
#include "Team.h"
#include "GameFixtures.h"
#include "Property.h"
#include "ProfileNeeds.h"
#include "AI.h"
#include "AINames.h"
#include "AIRegistry.h"
#include "Building.h"
#include "BuildingType.h"
#include "Order.h"
#include "OrderValidation.h"
#include "Player.h"
#include "scripting/javascript/ScriptValue.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace
{
// Rejections already found on master, which this check tolerates until the AI is
// fixed (an AI fix is a simulation change that needs a SIM_REVISION bump and its
// own decision). Each entry names exactly one AI, order type and reason, so any
// other rejection, by this AI or another, still fails. Remove an entry with the
// fix that makes it unnecessary: the long [slow] run (and the quick run, for
// entries marked inQuickRun) fails when an entry is no longer hit, so stale
// entries do not linger.
struct KnownRejection
{
	AI::ImplementationID ai;
	Uint8 orderType;
	OrderValidation::Reason reason;
	bool inQuickRun; // the default seeds and ticks reach it, not only the [slow] run
};
const std::vector<KnownRejection> knownRejections = {
	// KNOWN: the shared runtime's ChangeAlliances (src/ai/shared_runtime/ManagementMisc.cpp,
	// queued by Nicowar's fruit alliances in src/ai/nicowar/Farming.cpp) starts from the
	// team's observed enemy mask and resends it. Team::setCorrectMasks initializes that
	// mask to ~allies, with bits for teams the map does not have, which validTeamMask
	// rejects. Reproduce: GLOB2_SEED=1, "Nicowar issues only valid orders", tick 94,
	// player 0, set_alliance teamNumber 0 allied 0x1 enemy 0xfffffffe on a 2-team map.
	{AI::NICOWAR, ORDER_SET_ALLIANCE, OrderValidation::Reason::OutOfRange, true},
	// KNOWN: the same mask through Maxima's ChangeAlliances (src/ai/maxima/AIMaximaRuntime.cpp,
	// queued for fruit vision in src/ai/maxima/AIMaxima.cpp). Reproduce: GLOB2_SEED=1,
	// "Maxima issues only valid orders", tick 98, player 0, enemy mask 0xfffffffe.
	{AI::MAXIMA, ORDER_SET_ALLIANCE, OrderValidation::Reason::OutOfRange, true},
	// KNOWN: Cabino's moderateSpawns (src/ai/simple/AICabino.cpp) scales ratios with the
	// integer divisor max/16, which is 1 for a largest ratio of 17..31, so it sends
	// production ratios above MAX_RATIO_RANGE (16), e.g. worker 21. Reproduce:
	// GLOB2_SEED=1, "Cabino issues only valid orders in long games [slow]", tick 734,
	// player 0, modify_swarm gid 0 ratios {21, 6, 1}.
	{AI::CABINO, ORDER_MODIFY_SWARM, OrderValidation::Reason::OutOfRange, false},
};

bool known(AI::ImplementationID ai, const glob2test::ValidatedAIOrder &record)
{
	for (const auto &entry : knownRejections)
		if (entry.ai == ai && entry.orderType == record.order->getOrderType() && entry.reason == record.result.reason)
			return true;
	return false;
}

const char *orderName(Uint8 type)
{
	switch (type)
	{
	case ORDER_CREATE: return "create";
	case ORDER_MODIFY_BUILDING: return "modify_building";
	case ORDER_MODIFY_EXCHANGE: return "modify_exchange";
	case ORDER_MODIFY_SWARM: return "modify_swarm";
	case ORDER_MODIFY_FLAG: return "modify_flag";
	case ORDER_MODIFY_CLEARING_FLAG: return "modify_clearing_flag";
	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG: return "modify_min_level_to_flag";
	case ORDER_MOVE_FLAG: return "move_flag";
	case ORDER_ALTER_FORBIDDEN: return "alter_forbidden";
	case ORDER_ALTER_GUARD_AREA: return "alter_guard_area";
	case ORDER_ALTER_CLEAR_AREA: return "alter_clear_area";
	case ORDER_ALTER_FARM_AREA: return "alter_farm_area";
	case ORDER_DELETE: return "delete";
	case ORDER_CANCEL_DELETE: return "cancel_delete";
	case ORDER_CONSTRUCTION: return "construction";
	case ORDER_CANCEL_CONSTRUCTION: return "cancel_construction";
	case ORDER_CHANGE_PRIORITY: return "change_priority";
	case ORDER_NULL: return "null";
	case ORDER_PAUSE_GAME: return "pause_game";
	case ORDER_PLAYER_QUIT_GAME: return "player_quit_game";
	case ORDER_TEXT_MESSAGE: return "text_message";
	case ORDER_VOICE_DATA: return "voice_data";
	case ORDER_SET_ALLIANCE: return "set_alliance";
	case ORDER_MAP_MARK: return "map_mark";
	case ORDER_ADJUST_LATENCY: return "adjust_latency";
	}
	return "unknown";
}

// The building id an order names, for the orders that name one.
std::optional<Uint16> targetGid(Order &order)
{
	switch (order.getOrderType())
	{
	case ORDER_DELETE: return static_cast<OrderDelete &>(order).gid;
	case ORDER_CANCEL_DELETE: return static_cast<OrderCancelDelete &>(order).gid;
	case ORDER_CONSTRUCTION: return static_cast<OrderConstruction &>(order).gid;
	case ORDER_CANCEL_CONSTRUCTION: return static_cast<OrderCancelConstruction &>(order).gid;
	case ORDER_CHANGE_PRIORITY: return static_cast<OrderChangePriority &>(order).gid;
	case ORDER_MODIFY_BUILDING: return static_cast<OrderModifyBuilding &>(order).gid;
	case ORDER_MODIFY_EXCHANGE: return static_cast<OrderModifyExchange &>(order).gid;
	case ORDER_MODIFY_SWARM: return static_cast<OrderModifySwarm &>(order).gid;
	case ORDER_MODIFY_FLAG: return static_cast<OrderModifyFlag &>(order).gid;
	case ORDER_MODIFY_CLEARING_FLAG: return static_cast<OrderModifyClearingFlag &>(order).gid;
	case ORDER_MODIFY_MIN_LEVEL_TO_FLAG: return static_cast<OrderModifyMinLevelToFlag &>(order).gid;
	case ORDER_MOVE_FLAG: return static_cast<OrderMoveFlag &>(order).gid;
	}
	return std::nullopt;
}

// A reproduction line for one rejected order: which AI, seed, tick and seat, the
// order, why it was rejected, its wire bytes and, after execution, what its target
// building was.
std::string describe(AI::ImplementationID ai, std::uint32_t seed, const glob2test::ValidatedAIOrder &record, Game &game)
{
	auto &order = *record.order;
	std::ostringstream text;
	text << "AI " << AINames::getCLIName(ai) << " seed " << seed << " tick " << record.tick << " player "
		 << record.player << ": " << orderName(order.getOrderType()) << " order "
		 << OrderValidation::name(record.result.verdict) << " (" << OrderValidation::name(record.result.reason) << ")";
	if (const auto gid = targetGid(order))
	{
		text << " gid " << *gid << " (team " << Building::GIDtoTeam(*gid) << ")";
		const int team = Building::GIDtoTeam(*gid);
		if (const Building *b = team < game.teamsCount() && game.teams[team] ? game.teams[team]->myBuildings[Building::GIDtoID(*gid)] : nullptr)
			text << " " << b->type->type << " level " << b->type->level << (b->type->isBuildingSite ? " site" : "")
				 << " state " << int(b->buildingState) << " hp " << b->hp << "/" << b->getEffectiveMaxHp();
	}
	text << " bytes";
	const auto *data = order.getData();
	for (int i = 0; i < order.getDataLength(); ++i)
	{
		char hex[4];
		std::snprintf(hex, sizeof hex, " %02x", unsigned(data[i]));
		text << hex;
	}
	return text.str();
}

// Two colonies on a 64x64 grass torus, each with a swarm, an inn, workers,
// warriors, wheat and wood; the seed moves the fields and the second colony, and
// seeds the game. Both seats play the AI under test, so each sees an opponent
// that builds, fights and loses buildings.
struct World
{
	glob2test::HeadlessGame world{glob2test::GameOptions{
		.wDec = 6, .hDec = 6, .teams = 2, .discovered = true, .clearImmobile = true, .loadDefaultRace = true}};

	World(AI::ImplementationID id, std::uint32_t seed)
	{
		std::mt19937 layout(seed);
		const auto pick = [&](int low, int high) { return low + int(layout() % unsigned(high - low + 1)); };
		GameHeader header;
		header.setNumberOfPlayers(2);
		for (int p = 0; p < 2; ++p)
		{
			header.getBasePlayer(p) = BasePlayer(p, p ? "second" : "first", p, BasePlayer::playerTypeFromImplementationID(id));
			// A small cooperative script: the JavaScript host is itself the thing under
			// test, since it turns arbitrary script output into orders.
			if (id == AI::JAVASCRIPT)
				header.setAIConfig(p, Script::config(
					"let n=0;function step(c){n++;const b=c.game.buildings({team:c.myTeam,limit:50})"
					".filter(x=>!x.virtual);if(!b.length)return null;const t=b[n%b.length];"
					"return {type:'workers',building:t,workers:1+(n%4)};}"));
		}
		header.setRandomSeed(seed);
		world.game.setGameHeader(header, true);
		world.game.setWaitingOnMask(0);
		const int second[2] = {pick(26, 34), pick(26, 34)};
		for (int team = 0; team < 2; ++team)
		{
			const int ox = team ? second[0] : 0, oy = team ? second[1] : 0;
			auto &t = *world.game.teams[team];
			t.startPosX = 4 + ox;
			t.startPosY = 4 + oy;
			t.startPosSet = Team::START_POS_FROM_UNIT;
			for (auto *building : {world.addBuilding("swarm", 4 + ox, 4 + oy, 0, team),
								   world.addBuilding("inn", 10 + ox, 4 + oy, 0, team)})
			{
				building->setMaterial(WHEAT, building->type->maxMaterial[WHEAT]);
				building->update();
				t.addToStaticAbilitiesLists(building);
			}
			for (int unit = 0; unit < 12; ++unit)
				world.addUnit(unit < 8 ? WORKER : WARRIOR, 4 + ox + unit, 12 + oy, team);
			const int wheatX = pick(2, 8), wheatY = pick(16, 20);
			for (int y = wheatY; y < wheatY + 6; ++y)
				for (int x = wheatX; x < wheatX + 14; ++x)
					world.game.map.setResourceByIndex(x + ox, y + oy, WHEAT, 1);
			const int woodX = pick(18, 22), woodY = pick(12, 18);
			for (int y = woodY; y < woodY + 7; ++y)
				for (int x = woodX; x < woodX + 6; ++x)
					world.game.map.setResourceByIndex(x + ox, y + oy, WOOD, 5);
		}
		world.game.map.setMapDiscovered();
		for (int team = 0; team < 2; ++team)
			world.game.teams[team]->stats.step(world.game.teams[team]);
	}
};

struct Tally
{
	unsigned orders = 0, stale = 0, withdrawn = 0, rejected = 0;
	std::map<std::tuple<AI::ImplementationID, Uint8, OrderValidation::Reason>, unsigned> knownHits;
};

// Plays `ticks` ticks of one seed and fails on every rejected order that is not a
// known one, naming AI, seed, tick, order and reason. Reports at most a few per
// game; the first is the reproduction.
void play(AI::ImplementationID id, std::uint32_t seed, unsigned ticks, Tally &tally)
{
	World setup(id, seed);
	auto &game = setup.world.game;
	unsigned reported = 0;
	for (unsigned tick = 0; tick < ticks; ++tick)
		for (const auto &record : glob2test::stepAIValidated(game))
		{
			if (record.deliveredType != ORDER_NULL)
				++tally.orders;
			if (record.deliveredType != record.order->getOrderType())
				++tally.withdrawn;
			switch (record.result.verdict)
			{
			case OrderValidation::Verdict::Accepted: break;
			case OrderValidation::Verdict::Stale: ++tally.stale; break;
			case OrderValidation::Verdict::Rejected:
				++tally.rejected;
				if (known(id, record))
					++tally.knownHits[{id, record.order->getOrderType(), record.result.reason}];
				else if (reported++ < 5)
					FAIL_CHECK(describe(id, seed, record, game));
				break;
			}
		}
	if (reported > 5)
		FAIL_CHECK("AI " << AINames::getCLIName(id) << " seed " << seed << ": " << reported - 5 << " more unexpected rejections");
}

void checkAI(AI::ImplementationID id, std::uint32_t seeds, unsigned ticks, bool longRun)
{
	if (!AIRegistry::available(id))
		return; // compile_profile=core leaves this AI out
	const std::string ai = AINames::getCLIName(id);
	CAPTURE(ai);
	Tally tally;
	glob2test::forEachSeed(seeds, [&](std::uint32_t seed) { play(id, seed, ticks, tally); });
	MESSAGE(ai << ": " << tally.orders << " orders, " << tally.stale << " stale, " << tally.withdrawn
			   << " withdrawn by the pipeline, " << tally.rejected << " rejected");
	// A controller that never acts proves nothing.
	CHECK_MESSAGE(tally.orders > 0, ai << " issued no orders in " << ticks << " ticks");
	if (!glob2test::property::replaySeed())
		for (const auto &entry : knownRejections)
			if (entry.ai == id && (longRun || entry.inQuickRun))
				CHECK_MESSAGE(tally.knownHits.count({entry.ai, entry.orderType, entry.reason}),
							  "known rejection " << orderName(entry.orderType) << " "
												  << OrderValidation::name(entry.reason) << " of " << ai
												  << " no longer occurs; remove its entry");
}

// Headless tier: a few seconds per AI in a dev build. The [slow] cases play longer
// games, into the mid game where colonies fight and lose buildings.
constexpr std::uint32_t QuickSeeds = 2, LongSeeds = 4;
constexpr unsigned QuickTicks = 600, LongTicks = 4000;
// Every AI id below has a case; a new AI must add its own.
static_assert(AI::SIZE == 10, "add AIOrderValidity cases for the new AI");
} // namespace

#define AI_ORDER_VALIDITY_CASES(ID, NAME, ...) \
	TEST_CASE(NAME " issues only valid orders" __VA_ARGS__) \
	{ \
		glob2test::HeadlessGlobals globals; \
		checkAI(ID, QuickSeeds, QuickTicks, false); \
	} \
	TEST_CASE(NAME " issues only valid orders in long games [slow]" __VA_ARGS__) \
	{ \
		glob2test::HeadlessGlobals globals; \
		checkAI(ID, LongSeeds, LongTicks, true); \
	}

TEST_SUITE("AIOrderValidity")
{
	AI_ORDER_VALIDITY_CASES(AI::NUMBI, "Numbi")
	AI_ORDER_VALIDITY_CASES(AI::CASTOR, "Castor", * glob2test::needs({"ai:castor"}))
	AI_ORDER_VALIDITY_CASES(AI::WARRUSH, "Warrush")
	AI_ORDER_VALIDITY_CASES(AI::ECONO, "Econo")
	AI_ORDER_VALIDITY_CASES(AI::NICOWAR, "Nicowar")
	AI_ORDER_VALIDITY_CASES(AI::CORTEX, "Cortex", * glob2test::needs({"ai:cortex"}))
	AI_ORDER_VALIDITY_CASES(AI::MAXIMA, "Maxima")
	AI_ORDER_VALIDITY_CASES(AI::CABINO, "Cabino", * glob2test::needs({"ai:cabino"}))
	AI_ORDER_VALIDITY_CASES(AI::JAVASCRIPT, "JavaScript")
}
