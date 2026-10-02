// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "script/ScriptObservations.h"
#include "script/ScriptOrders.h"
#include "script/ScriptRuntime.h"
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Game.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Version.h"
#include "Order.h"
#include "Player.h"
#include "AIJavaScript.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <iostream>
using namespace Script;
using namespace GAGCore;
static std::string save(Game &game)
{
	auto *memory = new MemoryStreamBackend;
	BinaryOutputStream stream(memory);
	game.save(&stream, false, "script integration");
	stream.flush();
	return memory->takeContents();
}
TEST_CASE("JavaScript observations visibility memory pagination and stale references" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.seed = 19;
	options.profileName = "glob2-script-test";
	glob2test::HeadlessGlobals bootstrap(options);
	auto &globals = bootstrap.globals;
	GameGUI gui;
	auto &game = gui.game;
	game.gameHeader.setRandomSeed(19);
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam();
	game.addTeam();
	Race::loadDefault();
	REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
	auto *own = game.addUnit(2, 2, 0, WORKER, 0, 0, 0, 0);
	auto *enemy = game.addUnit(22, 22, 1, WORKER, 0, 0, 0, 0);
	GLOB2_REQUIRE(own && enemy, "JavaScript contract");
	const int swarm = globals.buildingsTypes.getTypeNum("swarm", 0, false);
	auto *ownedBuilding = game.addBuilding(5, 5, swarm, 0, 2, 2);
	GLOB2_REQUIRE(ownedBuilding, "JavaScript contract");
	game.map.unsetMapDiscovered();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	Observations fair(game, 0), full(game, -1);
	fair.observe();
	auto all = full.query("units", {});
	GLOB2_REQUIRE(all.items.size() == 2, "JavaScript contract");
	auto seen = fair.query("units", {});
	GLOB2_REQUIRE(seen.items.size() == 1, "JavaScript contract");
	auto enemyRef =
		Value::object().set("id", unsigned(enemy->gid)).set("generation", enemy->scriptIdentity);
	GLOB2_REQUIRE(fair.query("unit", {enemyRef}).kind == Value::Null, "JavaScript contract");
	GLOB2_REQUIRE(
		fair.query("unit", {Value::object().set("id", 65535).set("generation", 1)}).kind ==
			Value::Null,
		"JavaScript contract");
	GLOB2_REQUIRE(full.query("unit", {enemyRef}).kind == Value::Object, "JavaScript contract");
	auto hidden = fair.query("tile", {22, 22});
	GLOB2_REQUIRE(hidden.get("explored").number == 0, "JavaScript contract");
	GLOB2_REQUIRE(hidden.get("resource").kind == Value::Null, "JavaScript contract");
	enemy->scriptIdentity = game.allocateScriptIdentity(false, enemy->gid);
	enemy->scriptIdentity = game.allocateScriptIdentity(false, enemy->gid);
	game.map.setMapDiscovered(22, 22, game.teams[0]->me);
	game.stepCounter++;
	fair.observe();
	auto explored = fair.query("tile", {22, 22});
	GLOB2_REQUIRE(explored.get("visible").number != 0, "JavaScript contract");
	auto before = explored.get("resource").encode();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	game.map.getResource(22, 22).amount = 7;
	game.stepCounter++;
	fair.observe();
	auto remembered = fair.query("tile", {22, 22});
	GLOB2_REQUIRE(remembered.get("visible").number == 0, "JavaScript contract");
	GLOB2_REQUIRE(remembered.get("resource").encode() == before, "JavaScript contract");
	game.map.setMapDiscovered(22, 22, game.teams[0]->me);
	game.stepCounter++;
	fair.observe();
	auto firstEnemy = fair.query("units", {Value::object().set("team", 1)}).items.at(0);
	GLOB2_REQUIRE(firstEnemy.get("generation").number == enemy->scriptIdentity &&
					  enemy->scriptIdentity == 3,
				  "JavaScript contract");
	GLOB2_REQUIRE(fair.query("unit", {firstEnemy}).kind == Value::Object, "JavaScript contract");
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	enemy->scriptIdentity = game.allocateScriptIdentity(false, enemy->gid);
	enemy->scriptIdentity = game.allocateScriptIdentity(false, enemy->gid);
	game.map.setMapDiscovered(22, 22, game.teams[0]->me);
	game.stepCounter++;
	fair.observe();
	GLOB2_REQUIRE(fair.query("unit", {firstEnemy}).kind == Value::Null, "JavaScript contract");
	auto replacement = fair.query("units", {Value::object().set("team", 1)}).items.at(0);
	GLOB2_REQUIRE(replacement.get("generation").number == enemy->scriptIdentity &&
					  enemy->scriptIdentity == 5,
				  "JavaScript contract");
	auto *identityMemory = new MemoryStreamBackend;
	BinaryOutputStream identityOut(identityMemory);
	fair.save(&identityOut);
	auto identityBytes = identityMemory->takeContents();
	BinaryInputStream identityIn(
		new MemoryStreamBackend(identityBytes.data(), identityBytes.size()));
	identityIn.seekFromStart(0);
	Observations identityLoaded(game, 0);
	identityLoaded.load(&identityIn);
	GLOB2_REQUIRE(identityLoaded.query("unit", {replacement}).kind == Value::Object &&
					  identityLoaded.query("unit", {firstEnemy}).kind == Value::Null,
				  "JavaScript contract");
	auto paged = full.query("units", {Value::object().set("offset", 1).set("limit", 1)});
	GLOB2_REQUIRE(paged.items.size() == 1 && paged.items[0].get("id").number == enemy->gid,
				  "JavaScript contract");
}
TEST_CASE("JavaScript orders execute and survive save load" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.seed = 19;
	options.profileName = "glob2-script-test";
	glob2test::HeadlessGlobals bootstrap(options);
	auto &globals = bootstrap.globals;
	GameGUI gui;
	auto &game = gui.game;
	game.gameHeader.setRandomSeed(19);
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam();
	game.addTeam();
	Race::loadDefault();
	REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
	auto *own = game.addUnit(2, 2, 0, WORKER, 0, 0, 0, 0);
	auto *enemy = game.addUnit(22, 22, 1, WORKER, 0, 0, 0, 0);
	GLOB2_REQUIRE(own && enemy, "JavaScript contract");
	const int swarm = globals.buildingsTypes.getTypeNum("swarm", 0, false);
	auto *ownedBuilding = game.addBuilding(5, 5, swarm, 0, 2, 2);
	GLOB2_REQUIRE(ownedBuilding, "JavaScript contract");
	game.map.unsetMapDiscovered();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	auto buildingRef = Value::object()
						   .set("id", unsigned(ownedBuilding->gid))
						   .set("generation", ownedBuilding->scriptIdentity);
	auto workers = order(
		game, 0,
		Value::object().set("type", "workers").set("building", buildingRef).set("workers", 4));
	GLOB2_REQUIRE(workers->getOrderType() == ORDER_MODIFY_BUILDING, "JavaScript contract");
	bool denied = false;
	try
	{
		order(
			game, 0,
			Value::object().set("type", "workers").set("building", buildingRef).set("workers", 21));
	}
	catch (...)
	{
		denied = true;
	}
	GLOB2_REQUIRE(denied, "JavaScript contract");
	Value mask = Value::array();
	mask.items.emplace_back(true);
	auto area = std::static_pointer_cast<OrderAlterForbidden>(order(game, 0,
																	Value::object()
																		.set("type", "forbidden")
																		.set("x", 3)
																		.set("y", 4)
																		.set("width", 1)
																		.set("height", 1)
																		.set("mode", 1)
																		.set("mask", mask)));
	GLOB2_REQUIRE(area->centerX == 3 && area->centerY == 4 && area->minX == 0 && area->minY == 0 &&
					  area->maxX == 1 && area->maxY == 1,
				  "JavaScript contract");
	denied = false;
	try
	{
		order(game, 1, Value::object().set("type", "delete").set("building", buildingRef));
	}
	catch (...)
	{
		denied = true;
	}
	GLOB2_REQUIRE(denied, "JavaScript contract");
	auto *clearing =
		game.addBuilding(12, 12, globals.buildingsTypes.getTypeNum("clearingflag", 0, false), 0);
	GLOB2_REQUIRE(clearing, "JavaScript contract");
	auto clearingRef = Value::object()
						   .set("id", unsigned(clearing->gid))
						   .set("generation", clearing->scriptIdentity);
	Value switches = Value::array();
	for (int i = 0; i < BASIC_COUNT; ++i)
		switches.items.emplace_back(true);
	auto clearingOrder = Value::object()
							 .set("type", "clearingResources")
							 .set("building", clearingRef)
							 .set("resources", switches);
	denied = false;
	try
	{
		order(game, 0, clearingOrder);
	}
	catch (...)
	{
		denied = true;
	}
	GLOB2_REQUIRE(denied, "JavaScript contract");
	switches.items[STONE] = Value(false);
	clearingOrder.set("resources", switches);
	game.players[0] = new Player(0, "local", game.teams[0], BasePlayer::P_LOCAL);
	game.gameHeader.setNumberOfPlayers(1);
	game.gameHeader.getBasePlayer(0) = *game.players[0];
	auto acceptedClearing = order(game, 0, clearingOrder);
	acceptedClearing->sender = 0;
	game.executeOrder(acceptedClearing, -1);

	auto execute = [&](const Value &description)
	{
		auto accepted = order(game, 0, description);
		accepted->sender = 0;
		game.executeOrder(accepted, 0);
	};
	auto describe = [&](const char *type, Building *building)
	{
		return Value::object()
			.set("type", type)
			.set("building", Value::object()
								 .set("id", unsigned(building->gid))
								 .set("generation", building->scriptIdentity));
	};
	execute(describe("workers", ownedBuilding).set("workers", 4));
	CHECK(ownedBuilding->maxUnitWorking == 4);
	execute(describe("priority", ownedBuilding).set("priority", 1));
	CHECK(ownedBuilding->priority == 1);
	Value ratios = Value::array();
	for (int ratio : {3, 2, 1})
		ratios.items.emplace_back(ratio);
	execute(describe("production", ownedBuilding).set("ratios", ratios));
	for (int i = 0; i < NB_UNIT_TYPE; ++i)
		CHECK(ownedBuilding->ratio[i] == 3 - i);
	auto *market =
		game.addBuilding(18, 4, globals.buildingsTypes.getTypeNum("market", 0, false), 0);
	REQUIRE(market);
	execute(describe("exchange", market).set("receiveMask", 3).set("sendMask", 4));
	CHECK(market->receiveResourceMask == 3);
	CHECK(market->sendResourceMask == 4);
	execute(describe("range", clearing).set("range", 9));
	CHECK(clearing->unitStayRange == 9);
	auto *war = game.addBuilding(25, 20, globals.buildingsTypes.getTypeNum("warflag", 0, false), 0);
	REQUIRE(war);
	execute(describe("minimumLevel", war).set("level", 2));
	CHECK(war->minLevelToFlag == 2);
	execute(describe("moveFlag", clearing).set("x", 14).set("y", 15));
	CHECK(clearing->posX == 14);
	CHECK(clearing->posY == 15);
	for (const char *type : {"forbidden", "guardArea", "clearArea"})
	{
		CAPTURE(type);
		execute(Value::object()
					.set("type", type)
					.set("x", 3)
					.set("y", 4)
					.set("width", 1)
					.set("height", 1)
					.set("mode", 1)
					.set("mask", mask));
	}
	CHECK((game.map.getTile(3, 4).forbidden & 1u) != 0);
	CHECK((game.map.getTile(3, 4).guardArea & 1u) != 0);
	CHECK((game.map.getTile(3, 4).clearArea & 1u) != 0);
	auto *inn = game.addBuilding(0, 16, globals.buildingsTypes.getTypeNum("inn", 0, false), 0);
	REQUIRE(inn);
	auto identity = inn->scriptIdentity;
	execute(describe("construction", inn).set("workers", 2).set("futureWorkers", 3));
	CHECK(inn->buildingState != Building::ALIVE);
	execute(describe("cancelConstruction", inn).set("workers", 4));
	CHECK(inn->buildingState == Building::ALIVE);
	CHECK(inn->scriptIdentity == identity);
	execute(describe("delete", market));
	CHECK(market->buildingState == Building::WAITING_FOR_DESTRUCTION);
	execute(describe("cancelDelete", market));
	CHECK(market->buildingState == Building::ALIVE);
	const int flagType = globals.buildingsTypes.getTypeNum("warflag", 0, false);
	execute(Value::object()
				.set("type", "create")
				.set("buildingType", flagType)
				.set("x", 25)
				.set("y", 25)
				.set("workers", 2)
				.set("futureWorkers", 2)
				.set("range", 7));
	bool created = false;
	for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
		if (auto *building = game.teams[0]->myBuildings[slot];
			building && building->typeNum == flagType && building->posX == 25 &&
			building->posY == 25)
		{
			created = true;
			CHECK(building->scriptIdentity > 0);
		}
	CHECK(created);
	auto bytes = save(game);
	glob2test::writeFile(glob2test::artifactDir() / "supported-orders.game", bytes);
	GameGUI clearingLoaded;
	BinaryInputStream clearingIn(new MemoryStreamBackend(bytes.data(), bytes.size()));
	clearingIn.seekFromStart(0);
	GLOB2_REQUIRE(clearingLoaded.game.load(&clearingIn), "JavaScript contract");
	auto *restoredFlag =
		clearingLoaded.game.teams[0]->myBuildings[Building::GIDtoID(clearing->gid)];
	GLOB2_REQUIRE(restoredFlag && !restoredFlag->clearingResources[STONE] &&
					  restoredFlag->clearingResources[WOOD],
				  "JavaScript contract");
}
TEST_CASE("JavaScript scenario effects commit atomically and resume" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.seed = 19;
	options.profileName = "glob2-script-test";
	glob2test::HeadlessGlobals bootstrap(options);
	auto &globals = bootstrap.globals;
	GameGUI gui;
	auto &game = gui.game;
	game.gameHeader.setRandomSeed(19);
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam();
	game.addTeam();
	Race::loadDefault();
	REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
	auto *own = game.addUnit(2, 2, 0, WORKER, 0, 0, 0, 0);
	auto *enemy = game.addUnit(22, 22, 1, WORKER, 0, 0, 0, 0);
	GLOB2_REQUIRE(own && enemy, "JavaScript contract");
	const int swarm = globals.buildingsTypes.getTypeNum("swarm", 0, false);
	auto *ownedBuilding = game.addBuilding(5, 5, swarm, 0, 2, 2);
	GLOB2_REQUIRE(ownedBuilding, "JavaScript contract");
	game.map.unsetMapDiscovered();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	bool denied = false;
	game.objectives.addNewObjective("test", false, false, false, GameObjectives::Primary, 1);
	game.gameHints.addNewHint("hint", true, 1);
	auto &script = game.mapscript;
	script.setMapScriptMode(MapScript::JavaScript);
	script.setMapScript(
		"export function init(ctx,s){s.count=0;} export function "
		"step(ctx,s){s.count++;s.random=ctx.random();return "
		"[{type:'objective',id:0,action:'complete'},{type:'hint',id:0,visible:true},{type:'message'"
		",text:'test'},{type:'buildingChoice',name:'swarm',enabled:false}];}");
	GLOB2_REQUIRE(script.compileCode(), "JavaScript contract");
	script.syncStep(&gui);
	GLOB2_REQUIRE(game.objectives.isObjectiveComplete(0), "JavaScript contract");
	GLOB2_REQUIRE(game.gameHints.isHintVisible(0), "JavaScript contract");
	GLOB2_REQUIRE(!script.buildingAllowed("swarm", false), "JavaScript contract");
	auto *mapMemory = new MemoryStreamBackend;
	BinaryOutputStream mapOut(mapMemory);
	script.encodeData(&mapOut);
	auto encoded = mapMemory->takeContents();
	auto *inMemory = new MemoryStreamBackend;
	inMemory->write(encoded.data(), encoded.size());
	inMemory->seekFromStart(0);
	BinaryInputStream mapIn(inMemory);
	auto checksum = script.checkSum();
	MapScript loaded(&gui.game, &gui);
	GLOB2_REQUIRE(loaded.decodeData(&mapIn, VERSION_MINOR), "JavaScript contract");
	GLOB2_REQUIRE(loaded.checkSum() == checksum, "JavaScript contract");
	script.syncStep(&gui);
	loaded.syncStep(&gui);
	GLOB2_REQUIRE(loaded.checkSum() == script.checkSum(), "JavaScript contract");
	auto invalidSource = "export function step(ctx,s){s.changed=true;return "
						 "[{type:'objective',id:0,action:'incomplete'},{type:'unknown'}];}";
	script.setMapScript(invalidSource);
	GLOB2_REQUIRE(script.compileCode(), "JavaScript contract");
	checksum = script.checkSum();
	denied = false;
	try
	{
		script.syncStep(&gui);
	}
	catch (...)
	{
		denied = true;
	}
	GLOB2_REQUIRE(denied, "JavaScript contract");
	GLOB2_REQUIRE(game.objectives.isObjectiveComplete(0), "JavaScript contract");
	GLOB2_REQUIRE(script.checkSum() == checksum, "JavaScript contract");
}
TEST_CASE("JavaScript AI state RNG and disabled state survive continuation" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.seed = 19;
	options.profileName = "glob2-script-test";
	glob2test::HeadlessGlobals bootstrap(options);
	auto &globals = bootstrap.globals;
	GameGUI gui;
	auto &game = gui.game;
	game.gameHeader.setRandomSeed(19);
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam();
	game.addTeam();
	Race::loadDefault();
	REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
	auto *own = game.addUnit(2, 2, 0, WORKER, 0, 0, 0, 0);
	auto *enemy = game.addUnit(22, 22, 1, WORKER, 0, 0, 0, 0);
	GLOB2_REQUIRE(own && enemy, "JavaScript contract");
	const int swarm = globals.buildingsTypes.getTypeNum("swarm", 0, false);
	auto *ownedBuilding = game.addBuilding(5, 5, swarm, 0, 2, 2);
	GLOB2_REQUIRE(ownedBuilding, "JavaScript contract");
	game.map.unsetMapDiscovered();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	Player player(0, "script", game.teams[0], BasePlayer::P_LOCAL);
	game.gameHeader.setAIConfig(0, config("export function init(c,s){s.calls=0;} export function "
										  "step(c,s){s.calls++;s.r=c.random();return "
										  "{type:'workers',building:c.game.buildings({team:c."
										  "myTeam})[0],workers:1+Math.floor(s.r*4)};}"));
	AI controller(AI::JAVASCRIPT, &player);
	controller.getOrder(false);
	auto *aiMemory = new MemoryStreamBackend;
	BinaryOutputStream aiOut(aiMemory);
	controller.save(&aiOut);
	auto aiBytes = aiMemory->takeContents();
	auto *aiInput = new MemoryStreamBackend;
	aiInput->write(aiBytes.data(), aiBytes.size());
	aiInput->seekFromStart(0);
	BinaryInputStream aiIn(aiInput);
	AI resumed(&aiIn, &player, VERSION_MINOR);
	for (int i = 0; i < 8; ++i)
	{
		auto a = controller.getOrder(false), b = resumed.getOrder(false);
		GLOB2_REQUIRE(a->getOrderType() == b->getOrderType(), "JavaScript contract");
		auto *left = new MemoryStreamBackend;
		BinaryOutputStream leftOut(left);
		controller.save(&leftOut);
		auto *right = new MemoryStreamBackend;
		BinaryOutputStream rightOut(right);
		resumed.save(&rightOut);
		GLOB2_REQUIRE(left->takeContents() == right->takeContents(), "JavaScript contract");
	}
	game.gameHeader.setAIConfig(0, config("export function step(c,s){s.bad=true;c.random();return "
										  "{type:'delete',building:{id:65535,generation:1}};}"));
	AI bad(AI::JAVASCRIPT, &player);
	GLOB2_REQUIRE(bad.getOrder(false)->getOrderType() == ORDER_NULL, "JavaScript contract");
	GLOB2_REQUIRE(bad.getOrder(false)->getOrderType() == ORDER_NULL, "JavaScript contract");
	auto *disabledController = static_cast<AIJavaScript *>(bad.aiImplementation);
	auto *disabledBefore = new MemoryStreamBackend;
	BinaryOutputStream disabledOut(disabledBefore);
	disabledController->save(&disabledOut);
	auto disabledBytes = disabledBefore->takeContents();
	game.stepCounter++;
	game.map.setMapDiscovered(22, 22, game.teams[0]->me);
	disabledController->observe();
	auto *disabledAfter = new MemoryStreamBackend;
	BinaryOutputStream disabledAgain(disabledAfter);
	disabledController->save(&disabledAgain);
	GLOB2_REQUIRE(disabledAfter->takeContents() == disabledBytes, "JavaScript contract");
	auto *liveController = static_cast<AIJavaScript *>(controller.aiImplementation);
	game.teams[0]->isAlive = false;
	auto *deadBefore = new MemoryStreamBackend;
	BinaryOutputStream deadOut(deadBefore);
	liveController->save(&deadOut);
	auto deadBytes = deadBefore->takeContents();
	game.stepCounter++;
	liveController->observe();
	auto *deadAfter = new MemoryStreamBackend;
	BinaryOutputStream deadAgain(deadAfter);
	liveController->save(&deadAgain);
	GLOB2_REQUIRE(deadAfter->takeContents() == deadBytes, "JavaScript contract");
	game.teams[0]->isAlive = true;
	BinaryInputStream legacy(
		glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), "maps/Sand_River.map.gz"));
	GLOB2_REQUIRE(game.load(&legacy), "JavaScript contract");
	GLOB2_REQUIRE(game.mapscript.getMapScriptMode() == MapScript::USL &&
					  game.mapscript.getMapScript().empty() && game.mapscript.checkSum() == 0 &&
					  game.mapscript.buildingAllowed("swarm", false),
				  "JavaScript contract");
	game.mapscript.syncStep(&gui);
}
TEST_CASE("JavaScript large world observations respect work limits" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.seed = 19;
	options.profileName = "glob2-script-test";
	glob2test::HeadlessGlobals bootstrap(options);
	auto &globals = bootstrap.globals;
	GameGUI gui;
	auto &game = gui.game;
	game.gameHeader.setRandomSeed(19);
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam();
	game.addTeam();
	Race::loadDefault();
	REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
	auto *own = game.addUnit(2, 2, 0, WORKER, 0, 0, 0, 0);
	auto *enemy = game.addUnit(22, 22, 1, WORKER, 0, 0, 0, 0);
	GLOB2_REQUIRE(own && enemy, "JavaScript contract");
	const int swarm = globals.buildingsTypes.getTypeNum("swarm", 0, false);
	auto *ownedBuilding = game.addBuilding(5, 5, swarm, 0, 2, 2);
	GLOB2_REQUIRE(ownedBuilding, "JavaScript contract");
	game.map.unsetMapDiscovered();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	{
		GameGUI populated;
		auto &g = populated.game;
		g.map.setSize(6, 6, GRASS);
		g.map.setGame(&g);
		g.addTeam();
		g.addTeam();
		for (int team = 0; team < 2; ++team)
			for (int i = 0; i < Unit::MAX_COUNT; ++i)
				GLOB2_REQUIRE(g.addUnit(i % 64, i / 64 + team * 16, team, WORKER, 0, 0, 0, 0),
							  "JavaScript contract");
		Observations world(g, -1);
		Host host;
		host.width = 64;
		host.height = 64;
		host.team = -1;
		host.random = [] { return 0u; };
		host.query = [&](const auto &name, const auto &args, const QueryBudget &budget)
		{ return world.query(name, args, budget); };
		bool bounded = false;
		try
		{
			makeRuntime()->invoke("export function step(c,s){s.total=c.game.units().length;}",
								  Value::object(), false, host);
		}
		catch (const HostFailure &)
		{
			GLOB2_REQUIRE(false, "JavaScript contract");
		}
		catch (const std::runtime_error &)
		{
			bounded = true;
		}
		GLOB2_REQUIRE(bounded, "JavaScript contract");
		auto small = makeRuntime()->invoke(
			"export function step(c,s){s.total=c.game.units({offset:100,limit:100}).length;}",
			Value::object(), false, host);
		GLOB2_REQUIRE(small.state.get("total").number == 100, "JavaScript contract");
	}
}
