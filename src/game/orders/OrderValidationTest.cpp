// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// OrderValidation: the deterministic check a turn session applies to every human
// order before the engine executes it, and the executor guards that keep a hostile
// order from stopping a game that does not check (legacy network games). Each case
// builds a two-team game, so "own" and "foreign" are both available; player i plays
// team i.

#include "EngineFixtures.h"

#include <memory>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>
#include <random>
#include <vector>

#include "Brush.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "OrderValidation.h"
#include "Player.h"
#include "Team.h"
#include "Version.h"

using OrderValidation::Reason;
using OrderValidation::Verdict;

namespace
{
glob2test::GlobalsOptions withStrings()
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true; // chat and quit orders print translated lines
	return options;
}

struct Fixture
{
	glob2test::HeadlessGlobals globals{withStrings()};
	glob2test::HeadlessGame game{[] {
		glob2test::GameOptions options;
		options.teams = 2;
		options.header = true;
		options.wDec = 6;
		options.hDec = 6;
		return options;
	}()};
	Building* ownInn = nullptr;
	Building* foreignInn = nullptr;
	Building* ownFlag = nullptr;
	Building* foreignFlag = nullptr;

	Fixture(std::function<void(nlohmann::json&)> configure = {})
	{
        if(configure)
        {
            auto catalog=nlohmann::json::parse(game.game.buildingsTypes.snapshotJson());
            configure(catalog);
            game.game.buildingsTypes.loadSnapshotJson(catalog.dump());
            game.game.configureBuildingCatalog();
        }
		game.gui.localPlayer = 0;
		game.gui.localTeamNo = 0;
		ownInn = game.addBuilding("inn", 4, 4, 0, 0);
		foreignInn = game.addBuilding("inn", 40, 40, 0, 1);
		ownFlag = game.game.addBuilding(10, 10, type("warflag", false), 0);
		foreignFlag = game.game.addBuilding(50, 50, type("warflag", false), 1);
		REQUIRE(ownFlag);
		REQUIRE(foreignFlag);
	}

	static int type(const char* name, bool site, int level = 0)
	{
		const int t = globalContainer->buildingsTypes.getTypeNum(name, level, site);
		REQUIRE(t >= 0);
		return t;
	}

	OrderValidation::Result check(Order&& order, int player = 0)
	{
		return OrderValidation::validate(game.game, player, order);
	}
};

void expect(OrderValidation::Result r, Verdict verdict, Reason reason = Reason::None)
{
	INFO(std::string(OrderValidation::name(r.verdict)) << " / " << std::string(OrderValidation::name(r.reason)));
	CHECK(r.verdict == verdict);
	if (verdict != Verdict::Accepted)
		CHECK(r.reason == reason);
}

/// The constructors assert on ids no client has; the wire can carry any.
OrderDelete deleteOf(Uint16 gid)
{
	OrderDelete order(0);
	order.gid = gid;
	return order;
}

Utilities::BitArray fullMask(int w, int h)
{
	Utilities::BitArray mask(w * h);
	for (int i = 0; i < w * h; ++i)
		mask.set(i, true);
	return mask;
}
}

TEST_SUITE("OrderValidation")
{
	GLOB2_TEST_CASE("unsupported capability orders are rejected while deleted entities remain harmless", "[orders]")
	{
		Fixture f;
		const auto inn=f.ownInn->gid;
		const auto missing=Building::GIDfrom(900,0);
		Sint32 ratios[NB_UNIT_TYPE]={1,0,0};
		bool clearing[MaterialCount]={true,false,false,false,false};
		Building* hospital=f.game.addBuilding("hospital",20,20);
		expect(f.check(OrderModifyBuilding(hospital->gid,0)),Verdict::Accepted);
		expect(f.check(OrderModifyBuilding(hospital->gid,1)),Verdict::Rejected,Reason::OutOfRange);
		hospital->maxUnitWorking=2; // retained staff from an older state may be released
		auto release=std::make_shared<OrderModifyBuilding>(hospital->gid,0); release->sender=0;
		f.game.game.executeOrder(release,0);
		CHECK(hospital->maxUnitWorking==0);
		expect(f.check(OrderModifySwarm(inn,ratios)),Verdict::Rejected,Reason::BadState);
		expect(f.check(OrderModifyFlag(inn,0)),Verdict::Rejected,Reason::BadState);
		expect(f.check(OrderModifyClearingFlag(inn,clearing)),Verdict::Rejected,Reason::BadState);
		expect(f.check(OrderModifyMinLevelToFlag(inn,0,1)),Verdict::Rejected,Reason::BadState);
		expect(f.check(OrderModifyMinLevelToFlag(f.ownFlag->gid,1,1)),Verdict::Rejected,Reason::BadState);
		expect(f.check(OrderModifySwarm(missing,ratios)),Verdict::Accepted);
		expect(f.check(OrderModifyFlag(missing,0)),Verdict::Accepted);
		expect(f.check(OrderModifyClearingFlag(missing,clearing)),Verdict::Accepted);
		expect(f.check(OrderModifyMinLevelToFlag(missing,1,1)),Verdict::Accepted);
	}

	GLOB2_TEST_CASE("catalog stage limits independently bound construction and completed staffing", "[orders]")
	{
        Fixture f([](nlohmann::json& catalog) {
            const std::map<std::string,int> caps={{"inn.0.site",30},{"inn.0.finished",40},
                {"inn.1.site",3},{"inn.1.finished",7}};
            for(auto& variant : catalog["variants"])
                if(auto cap=caps.find(variant["key"].get<std::string>());cap!=caps.end())
                {
                    variant["semantics"]["assignmentLimit"]=cap->second;
                    variant["presentation"]["defaultAssigned"]=std::min(2,cap->second);
                }
        });
        const int root=f.game.game.buildingsTypes.getTypeNum("inn",0,true);
        CHECK(f.ownInn->runtime->assignmentLimit==40);
		expect(f.check(OrderCreate(0,20,20,root,30,40)),Verdict::Accepted);
		expect(f.check(OrderCreate(0,20,20,root,31,40)),Verdict::Rejected,Reason::OutOfRange);
		expect(f.check(OrderCreate(0,20,20,root,30,41)),Verdict::Rejected,Reason::OutOfRange);
		expect(f.check(OrderConstruction(f.ownInn->gid,3,7)),Verdict::Accepted);
		expect(f.check(OrderConstruction(f.ownInn->gid,4,7)),Verdict::Rejected,Reason::OutOfRange);
		expect(f.check(OrderConstruction(f.ownInn->gid,3,8)),Verdict::Rejected,Reason::OutOfRange);
		expect(f.check(OrderModifyBuilding(f.ownInn->gid,40)),Verdict::Accepted);
	}

	GLOB2_TEST_CASE("orders a client builds for its own team and buildings pass", "[orders]")
	{
		Fixture f;
		const Uint16 inn = f.ownInn->gid, flag = f.ownFlag->gid;
		expect(f.check(OrderCreate(0, 20, 20, Fixture::type("inn", true), 3, 5)), Verdict::Accepted);
		expect(f.check(OrderCreate(0, 20, 20, Fixture::type("warflag", false), 10, 10, 8)), Verdict::Accepted);
		expect(f.check(OrderCreate(0, 20, 20, Fixture::type("explorationflag", false), 2, 2, 20)), Verdict::Accepted);
		// Non-flags travel with a radius of 0 from the GUI (or none); it is ignored.
		expect(f.check(OrderCreate(0, 20, 20, Fixture::type("swarm", true), 1, 1, 0)), Verdict::Accepted);
		expect(f.check(OrderDelete(inn)), Verdict::Accepted);
		expect(f.check(OrderModifyBuilding(inn, f.ownInn->type->semantics.assignmentLimit)), Verdict::Accepted);
		expect(f.check(OrderChangePriority(inn, -1)), Verdict::Accepted);
		expect(f.check(OrderConstruction(inn, 4, 6)), Verdict::Accepted);
		expect(f.check(OrderModifyFlag(flag, f.ownFlag->type->maxUnitStayRange)), Verdict::Accepted);
		expect(f.check(OrderModifyMinLevelToFlag(flag, 3)), Verdict::Accepted);
		expect(f.check(OrderMoveFlag(flag, 63, 0, true)), Verdict::Accepted);
		Sint32 ratio[NB_UNIT_TYPE] = {16, 0, 3};
		expect(f.check(OrderModifySwarm(inn, ratio)), Verdict::Rejected, Reason::BadState);
		expect(f.check(OrderAlterForbidden(0, BrushTool::MODE_ADD, 60, 60, 8, 8, fullMask(8, 8))), Verdict::Accepted);
		expect(f.check(OrderAlterClearArea(0, BrushTool::MODE_DEL, 0, 0, 2, 2, fullMask(2, 2))), Verdict::Accepted);
		expect(f.check(SetAllianceOrder(0, 1, 2, 1, 1, 1)), Verdict::Accepted);
		expect(f.check(MessageOrder(~0u, MessageOrder::PRIVATE_MESSAGE_TYPE, "hi")), Verdict::Accepted);
		expect(f.check(MapMarkOrder(0, 5, 5)), Verdict::Accepted);
		expect(f.check(PauseGameOrder(true)), Verdict::Accepted);
		expect(f.check(PlayerQuitsGameOrder(0)), Verdict::Accepted);
		expect(f.check(PlayerQuitsGameOrder(1), 1), Verdict::Accepted);
		expect(f.check(NullOrder()), Verdict::Accepted);
		// A building that has gone since the click is the executor's business.
		expect(f.check(OrderDelete(Building::GIDfrom(900, 0))), Verdict::Accepted);
	}

	GLOB2_TEST_CASE("orders acting for another team, player or building are rejected", "[orders]")
	{
		Fixture f;
		const Uint16 theirs = f.foreignInn->gid, theirFlag = f.foreignFlag->gid;
		expect(f.check(OrderCreate(1, 20, 20, Fixture::type("inn", true), 3, 5)), Verdict::Rejected, Reason::WrongTeam);
		expect(f.check(OrderCreate(-1, 20, 20, Fixture::type("inn", true), 3, 5)), Verdict::Rejected, Reason::WrongTeam);
		expect(f.check(OrderDelete(theirs)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderCancelDelete(theirs)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderConstruction(theirs, 1, 1)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderCancelConstruction(theirs, 1)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderChangePriority(theirs, 1)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderModifyBuilding(theirs, 1)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderModifyExchange(theirs, 1, 2)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderModifyFlag(theirFlag, 1)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderMoveFlag(theirFlag, 1, 1, true)), Verdict::Rejected, Reason::ForeignBuilding);
		bool clear[MaterialCount] = {true};
		expect(f.check(OrderModifyClearingFlag(theirFlag, clear)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderModifyMinLevelToFlag(theirFlag, 1)), Verdict::Rejected, Reason::ForeignBuilding);
		// Ids past every team the engine has, and of a team this map lacks.
		expect(f.check(deleteOf(65535)), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderDelete(Building::GIDfrom(3, 7))), Verdict::Rejected, Reason::ForeignBuilding);
		expect(f.check(OrderAlterForbidden(1, BrushTool::MODE_ADD, 0, 0, 2, 2, fullMask(2, 2))), Verdict::Rejected,
		       Reason::WrongTeam);
		expect(f.check(OrderAlterGuardArea(200, BrushTool::MODE_ADD, 0, 0, 2, 2, fullMask(2, 2))), Verdict::Rejected,
		       Reason::WrongTeam);
		expect(f.check(SetAllianceOrder(1, 3, 0, 3, 3, 3)), Verdict::Rejected, Reason::WrongTeam);
		expect(f.check(SetAllianceOrder(31, 0, 0, 0, 0, 0)), Verdict::Rejected, Reason::WrongTeam);
		expect(f.check(MapMarkOrder(1, 5, 5)), Verdict::Rejected, Reason::WrongTeam);
		expect(f.check(MapMarkOrder(0xffffffffu, 5, 5)), Verdict::Rejected, Reason::WrongTeam);
		expect(f.check(PlayerQuitsGameOrder(1)), Verdict::Rejected, Reason::WrongPlayer);
		expect(f.check(PlayerQuitsGameOrder(-7)), Verdict::Rejected, Reason::WrongPlayer);
		// The sender comes from the relay's bundle; one outside the game is refused too.
		expect(f.check(NullOrder(), 5), Verdict::Rejected, Reason::WrongPlayer);
	}

	GLOB2_TEST_CASE("fields outside what the user interface produces are rejected", "[orders]")
	{
		Fixture f;
		const Uint16 inn = f.ownInn->gid, flag = f.ownFlag->gid;
		const int site = Fixture::type("inn", true);
		expect(f.check(OrderCreate(0, 1, 1, Fixture::type("inn", false), 1, 1)), Verdict::Rejected, Reason::BadBuildingType);
		expect(f.check(OrderCreate(0, 1, 1, Fixture::type("inn", true, 1), 1, 1)), Verdict::Rejected, Reason::BadBuildingType);
		expect(f.check(OrderCreate(0, 1, 1, -1, 1, 1)), Verdict::Rejected, Reason::BadBuildingType);
		expect(f.check(OrderCreate(0, 1, 1, 1 << 20, 1, 1)), Verdict::Rejected, Reason::BadBuildingType);
		expect(f.check(OrderCreate(0, 1, 1, site, 21, 1)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderCreate(0, 1, 1, site, 1, -1)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderCreate(0, 1, 1, Fixture::type("warflag", false), 1, 1, 256)), Verdict::Rejected,
		       Reason::OutOfRange);
		expect(f.check(OrderCreate(0, 1, 1, Fixture::type("warflag", false), 1, 1, -2)), Verdict::Rejected,
		       Reason::OutOfRange);
		expect(f.check(OrderModifyBuilding(inn, f.ownInn->type->semantics.assignmentLimit + 1)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderConstruction(inn, 0xffffffffu, 1)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderChangePriority(inn, 2)), Verdict::Rejected, Reason::OutOfRange);
		Sint32 ratio[NB_UNIT_TYPE] = {1, -1, 1};
		expect(f.check(OrderModifySwarm(inn, ratio)), Verdict::Rejected, Reason::OutOfRange);
		ratio[1] = 17;
		expect(f.check(OrderModifySwarm(inn, ratio)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderModifyFlag(flag, f.ownFlag->type->maxUnitStayRange + 1)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderModifyFlag(flag, -1)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderModifyMinLevelToFlag(flag, 4)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderMoveFlag(flag, 64, 3, false)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderMoveFlag(flag, 3, -1, false)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(OrderAlterClearArea(0, 7, 0, 0, 2, 2, fullMask(2, 2))), Verdict::Rejected, Reason::BadMode);
		expect(f.check(OrderAlterForbidden(0, BrushTool::MODE_NONE, 0, 0, 2, 2, fullMask(2, 2))), Verdict::Rejected,
		       Reason::BadMode);
		expect(f.check(SetAllianceOrder(0, 1u << 2, 0, 0, 0, 0)), Verdict::Rejected, Reason::OutOfRange);
		expect(f.check(MessageOrder(1, MessageOrder::PRIVATE_RECEIPT_TYPE, "x")), Verdict::Rejected, Reason::BadMode);
		expect(f.check(MessageOrder(1, 77, "x")), Verdict::Rejected, Reason::BadMode);
		expect(f.check(OrderVoiceData(1, 10, 200, nullptr)), Verdict::Rejected, Reason::BadVoice);
		expect(f.check(AdjustLatency(3)), Verdict::Rejected, Reason::NotPermitted);
	}

	GLOB2_TEST_CASE("orders whose building is no longer in a state they apply to are stale", "[orders]")
	{
		Fixture f;
		// Cancelling a deletion that is not pending would revive the building.
		expect(f.check(OrderCancelDelete(f.ownInn->gid)), Verdict::Stale, Reason::BadState);
		f.ownInn->launchDelete();
		expect(f.check(OrderCancelDelete(f.ownInn->gid)), Verdict::Accepted);
		// A new building's site has no level to return to (cancelConstruction asserted).
		Building* site = f.game.game.addBuilding(30, 30, Fixture::type("inn", true), 0);
		REQUIRE(site);
		expect(f.check(OrderCancelConstruction(site->gid, 1)), Verdict::Stale, Reason::BadState);
	}

	GLOB2_TEST_CASE("every order type survives random payloads through decode, check and execution", "[orders]")
	{
		Fixture f;
		std::mt19937 random(1234);
		int decoded = 0, accepted = 0;
		for (int round = 0; round < 8000; ++round)
		{
			std::vector<Uint8> bytes(1 + random() % 48);
			for (auto& b : bytes)
				b = static_cast<Uint8>(random());
			// Mostly real order types, with the lengths they expect now and then.
			static const Uint8 types[] = {ORDER_CREATE, ORDER_DELETE, ORDER_CANCEL_DELETE, ORDER_CONSTRUCTION,
			                              ORDER_CANCEL_CONSTRUCTION, ORDER_MODIFY_BUILDING, ORDER_MODIFY_EXCHANGE,
			                              ORDER_MODIFY_SWARM, ORDER_MODIFY_FLAG, ORDER_MODIFY_CLEARING_FLAG,
			                              ORDER_MODIFY_MIN_LEVEL_TO_FLAG, ORDER_MOVE_FLAG, ORDER_CHANGE_PRIORITY,
			                              ORDER_ALTER_FORBIDDEN, ORDER_ALTER_GUARD_AREA, ORDER_ALTER_CLEAR_AREA,
			                              ORDER_ALTER_FARM_AREA,
			                              ORDER_TEXT_MESSAGE, ORDER_VOICE_DATA, ORDER_SET_ALLIANCE, ORDER_MAP_MARK,
			                              ORDER_PAUSE_GAME, ORDER_PLAYER_QUIT_GAME, ORDER_ADJUST_LATENCY};
			if (random() % 64)
				bytes[0] = types[random() % (sizeof types)];
			static const int lengths[] = {28, 2, 2, 10, 6, 4, 10, 2 + 4 * NB_UNIT_TYPE, 6, 2 + MaterialCount, 4, 11, 6};
			if (random() % 2)
				bytes.resize(1 + lengths[random() % (sizeof lengths / sizeof *lengths)]);
			// Small values in the team and id fields, so some land on real teams and buildings.
			if (bytes.size() > 4 && random() % 2)
				bytes[1] = bytes[2] = bytes[3] = 0, bytes[4] = static_cast<Uint8>(random() % 3);
			auto order = Order::getOrder(bytes.data(), static_cast<int>(bytes.size()), VERSION_MINOR);
			if (!order)
				continue;
			++decoded;
			const int player = static_cast<int>(random() % 2);
			const auto result = OrderValidation::validate(f.game.game, player, *order);
			if (result.verdict != Verdict::Accepted)
				continue;
			++accepted;
			order->sender = player;
			f.game.gui.executeOrder(order);
			if (round % 100 == 0)
				f.game.step();
		}
		CHECK(decoded > 500);
		CHECK(accepted > 50);
		MESSAGE(decoded << " decoded, " << accepted << " accepted and executed");
	}

	GLOB2_TEST_CASE("hostile orders do not stop a game that executes them unchecked", "[orders]")
	{
		// Legacy network games and replays execute orders without OrderValidation.
		// These used to fail an assert or index past an array; now they do nothing.
		Fixture f;
		Building* site = f.game.game.addBuilding(30, 30, Fixture::type("inn", true), 0);
		REQUIRE(site);
		const Uint32 before = f.game.checksum();
		std::vector<std::shared_ptr<Order>> orders = {
			std::make_shared<OrderCreate>(1, 2, 2, Fixture::type("inn", true), 1, 1),
			std::make_shared<OrderCreate>(0, 2, 2, 1 << 20, 1, 1),
			std::make_shared<OrderDelete>(deleteOf(65535)),
			std::make_shared<OrderDelete>(Building::GIDfrom(5, 9)),
			std::make_shared<OrderModifyBuilding>(f.ownInn->gid, 500),
			std::make_shared<OrderAlterForbidden>(0, 9, 0, 0, 2, 2, fullMask(2, 2)),
			std::make_shared<OrderAlterGuardArea>(0, 9, 0, 0, 2, 2, fullMask(2, 2)),
			std::make_shared<OrderAlterClearArea>(0, 0, 0, 0, 2, 2, fullMask(2, 2)),
			std::make_shared<OrderAlterGuardArea>(250, BrushTool::MODE_ADD, 0, 0, 2, 2, fullMask(2, 2)),
			// The fixture's game does not carry the farm-areas experiment.
			std::make_shared<OrderAlterFarmArea>(0, BrushTool::MODE_ADD, 0, 0, 2, 2, fullMask(2, 2)),
			std::make_shared<OrderAlterFarmArea>(0, 9, 0, 0, 2, 2, fullMask(2, 2)),
			std::make_shared<SetAllianceOrder>(31, 0, 0, 0, 0, 0),
			std::make_shared<PlayerQuitsGameOrder>(20),
			std::make_shared<MessageOrder>(~0u, 77, "x"),
			std::make_shared<MapMarkOrder>(77, 1, 1),
			std::make_shared<OrderVoiceData>(~0u, 4, 3, nullptr),
		};
		orders.push_back(std::make_shared<OrderCancelConstruction>(site->gid, 1));
		for (auto& order : orders)
		{
			INFO("order type " << int(order->getOrderType()));
			order->sender = 0;
			f.game.gui.executeOrder(order);
		}
		// None of them changed the game: the out-of-range quit left player 0 as it was.
		CHECK(f.game.checksum() == before);
		CHECK(f.game.game.players[0]->type == BasePlayer::P_LOCAL);
		f.game.step(5);
	}
}

TEST_CASE("OrderValidation/clearing material wire boundary")
{
    const Uint8 legacy[] = {0, 0, 1, 0, 1, 0, 1};
    auto old = OrderModifyClearingFlag::deserialize(legacy, sizeof(legacy), 137);
    REQUIRE(old);
    CHECK(old->clearingMaterials[0]);
    CHECK(old->clearingMaterials[4]);
    for (unsigned m = 5; m < MaterialCount; ++m) CHECK_FALSE(old->clearingMaterials[m]);
    CHECK_FALSE(OrderModifyClearingFlag::deserialize(legacy, sizeof(legacy), VERSION_MINOR));
    bool choices[MaterialCount]{};
    choices[materialIndex(MaterialId::Fabric)] = true;
    OrderModifyClearingFlag current(0, choices);
    auto decoded = OrderModifyClearingFlag::deserialize(current.getData(), current.getDataLength(), VERSION_MINOR);
    REQUIRE(decoded);
    CHECK(decoded->clearingMaterials[materialIndex(MaterialId::Fabric)]);
}
