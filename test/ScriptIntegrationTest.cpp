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

#include "ai/ScriptLibrary.h"
#include "script/ScriptServices.h"
#include "script/ScriptSpatial.h"
TEST_CASE("JavaScript custom library atomic updates and frozen source" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	Online::MemoryStorage storage;
	Library library(storage);
	const std::string source = "export function metadata(){return {apiVersion:2,name:'Duplicate'}} "
							   "export function step(){}";
	auto first = library.put(source, "first.js"), second = library.put(source, "second.js");
	CHECK(first != second);
	CHECK(library.entries().size() == 2);
	auto frozen = library.configuration(first);
	CHECK_THROWS(library.put("syntax !", "bad.js", first));
	CHECK(library.configuration(first) == frozen);
	storage.failWrites = true;
	CHECK_THROWS(library.put(source + "\n// update", "new.js", first));
	storage.failWrites = false;
	CHECK(library.configuration(first) == frozen);
	auto before = library.checkpoint();
	library.put(source + "\n// update", "new.js", first);
	CHECK(library.configuration(first) != frozen);
	CHECK(sourceFromConfig(frozen) == source);
	library.rollback(before);
	CHECK(library.configuration(first) == frozen);
	Library restored(storage);
	CHECK(restored.entries().size() == 2);
	restored.remove(first);
	CHECK(restored.entries().size() == 1);
	CHECK_THROWS(restored.configuration(first));
}

TEST_CASE("JavaScript profile two spatial cache and queued save continuity" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::GlobalsOptions options;
	options.seed = 19;
	options.profileName = "glob2-services-test";
	glob2test::HeadlessGlobals bootstrap(options);
	GameGUI gui;
	auto &game = gui.game;
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam();
	Race::loadDefault();
	REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
	auto *building = game.addBuilding(
		5, 5, bootstrap.globals.buildingsTypes.getTypeNum("swarm", 0, false), 0, 2, 2);
	REQUIRE(building);
	Observations observations(game, 0);
	observations.setProfile(2);
	observations.observe();
	Services services(game, 0, observations);
	services.begin();
	auto ref = Value::object()
				   .set("id", unsigned(building->gid))
				   .set("generation", building->scriptIdentity);
	auto command = Value::object()
					   .set("type", "workers")
					   .set("building", ref)
					   .set("workers", 4)
					   .set("actionId", 1);
	Value commands = Value::array();
	commands.items.push_back(command);
	services.commit(commands, Value::object());
	command.set("workers", 5).set("actionId", 2);
	commands.items[0] = command;
	services.commit(commands, Value::object());
	CHECK(services.actions().items.size() == 1);
	CHECK(services.actions().items[0].get("id").number == 1);
	CHECK(services.query("desired", {ref}, {}).get("workers").number == 5);
	auto saved = Value::decode(services.save().encode());
	Services restored(game, 0, observations);
	restored.load(saved);
	CHECK(restored.save().encode() == services.save().encode());
	CHECK(restored.dispatch()->getOrderType() == services.dispatch()->getOrderType());
	CHECK(restored.save().encode() == services.save().encode());
	Spatial spatial(game, 0, observations);
	spatial.begin(Value::array());
	Value points = Value::array();
	points.items.push_back(Value::object().set("x", 0).set("y", 0));
	auto spec = Value::object()
					.set("sources", Value::object().set("points", points))
					.set("metric", "manhattan");
	size_t cold = 0, warm = 0;
	auto field = spatial.query("distanceField", {spec}, [&](size_t n, size_t) { cold += n; });
	spatial.query("distanceField", {spec}, [&](size_t n, size_t) { warm += n; });
	CHECK(cold == warm);
	CHECK(spatial
			  .query("distance",
					 {Value::object().set("x", 31).set("y", 0),
					  Value::object().set("x", 0).set("y", 0)},
					 {})
			  .number == 1);
	CHECK(spatial
			  .query("displacement",
					 {Value::object().set("x", 31).set("y", 0),
					  Value::object().set("x", 0).set("y", 0)},
					 {})
			  .get("x")
			  .number == 1);
}

#include "ReplayTelemetry.h"
#include "scene/Scene.h"
#include "scene/SceneExtract.h"
TEST_CASE("JavaScript telemetry scene permissions and replay diagnostic roundtrip" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world;
	auto &game = world.game;
	if (game.mapHeader.getNumberOfTeams() < 2)
		game.addTeam();
	for (int t = 0; t < 2; ++t)
	{
		auto series = std::make_shared<AITelemetry::Series>();
		series->player = t;
		series->playerName = "AI " + std::to_string(t);
		series->current.available = true;
		series->named.push_back({"strategy.phase", t ? "secret" : "expansion", "", "", 0});
		game.teams[t]->stats.aiTelemetry = {series};
	}
	game.teams[0]->allies = 1;
	SceneRequest request;
	request.localTeam = 0;
	Scene scene;
	SceneExtractor extractor;
	extractor.extract(game, request, scene);
	REQUIRE(scene.panels.aiTelemetry.size() == 1);
	CHECK(scene.panels.aiTelemetry[0].player == 0);
	game.teams[0]->allies = 3;
	extractor.extract(game, request, scene);
	CHECK(scene.panels.aiTelemetry.size() == 1);
	game.teams[1]->allies = 3;
	extractor.extract(game, request, scene);
	CHECK(scene.panels.aiTelemetry.size() == 2);
	game.teams[0]->allies = 1;
	extractor.extract(game, request, scene);
	CHECK(scene.panels.aiTelemetry.size() == 1);
	request.spectating = true;
	extractor.extract(game, request, scene);
	CHECK(scene.panels.aiTelemetry.size() == 2);
	ReplayTelemetry::Stream recording;
	recording.capture(game, 0);
	game.teams[1]->stats.aiTelemetry[0]->named[0].value = "attack";
	recording.capture(game, 32);
	auto *memory = new MemoryStreamBackend;
	BinaryOutputStream output(memory);
	recording.write(&output);
	auto bytes = memory->takeContents();
	BinaryInputStream input(new MemoryStreamBackend(bytes.data(), bytes.size()));
	input.seekFromStart(0);
	ReplayTelemetry::Stream playback;
	playback.read(&input);
	playback.apply(game, 0);
	CHECK(game.teams[1]->stats.aiTelemetry[0]->named[0].value == "secret");
	playback.apply(game, 31);
	CHECK(game.teams[1]->stats.aiTelemetry[0]->named[0].value == "secret");
	playback.apply(game, 32);
	CHECK(game.teams[1]->stats.aiTelemetry[0]->named[0].value == "attack");
	// Headless playback can save its final state. Presentation-only schemas must
	// survive that path without being mistaken for live controller counters.
	auto *savedMemory = new MemoryStreamBackend;
	BinaryOutputStream savedOutput(savedMemory);
	AITelemetry::save(&savedOutput, game.teams[1]->stats.aiTelemetry);
	auto savedBytes = savedMemory->takeContents();
	BinaryInputStream savedInput(new MemoryStreamBackend(savedBytes.data(), savedBytes.size()));
	savedInput.seekFromStart(0);
	std::vector<std::shared_ptr<AITelemetry::Series>> restored;
	AITelemetry::load(&savedInput, restored, VERSION_MINOR);
	REQUIRE(restored.size() == 1);
	CHECK(restored[0]->named == game.teams[1]->stats.aiTelemetry[0]->named);
}

#include <chrono>
TEST_CASE(
	"JavaScript native distance fields match torus reference cold warm and reloaded [benchmark]" *
	doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::HeadlessGlobals globals;
	for (int exponent : {6, 9})
	{
		glob2test::HeadlessGame world({.wDec = exponent, .hDec = exponent, .teams = 1});
		Observations observations(world.game, -1);
		observations.setProfile(2);
		Spatial spatial(world.game, 0, observations);
		spatial.begin(Value::array());
		Value points = Value::array();
		points.items.push_back(Value::object().set("x", 0).set("y", 0));
		auto spec = Value::object()
						.set("sources", Value::object().set("points", points))
						.set("metric", "manhattan");
		size_t coldWork = 0, warmWork = 0;
		auto start = std::chrono::steady_clock::now();
		auto cold =
			spatial.query("distanceField", {spec}, [&](size_t n, size_t) { coldWork += n; });
		auto middle = std::chrono::steady_clock::now();
		auto warm =
			spatial.query("distanceField", {spec}, [&](size_t n, size_t) { warmWork += n; });
		auto finish = std::chrono::steady_clock::now();
		CHECK(coldWork == warmWork);
		bool correct = true;
		int size = 1 << exponent;
		for (int y = 0; y < size; y += std::max(1, size / 32))
			for (int x = 0; x < size; x += std::max(1, size / 32))
			{
				auto actual = spatial.query("fieldValue", {cold, Value(x), Value(y)}, {});
				int expected = std::min(x, size - x) + std::min(y, size - y);
				correct &= actual.get("distance").number == expected;
				correct &= actual.encode() ==
						   spatial.query("fieldValue", {warm, Value(x), Value(y)}, {}).encode();
			}
		CHECK(correct);
		Spatial fresh(world.game, 0, observations);
		fresh.begin(Value::array());
		size_t freshWork = 0;
		fresh.query("distanceField", {spec}, [&](size_t n, size_t) { freshWork += n; });
		CHECK(freshWork == coldWork);
		for (int source = 1; source <= 8; ++source)
		{
			spatial.begin(Value::array());
			auto changed = spec;
			Value seeds = Value::array();
			seeds.items.push_back(Value::object().set("x", source).set("y", 0));
			changed.set("sources", Value::object().set("points", seeds));
			spatial.query("distanceField", {changed}, {});
		}
		spatial.begin(Value::array());
		size_t evictedWork = 0;
		auto evicted =
			spatial.query("distanceField", {spec}, [&](size_t n, size_t) { evictedWork += n; });
		CHECK(evictedWork == coldWork);
		CHECK(spatial.query("fieldValue", {evicted, Value(size - 1), Value(size - 1)}, {})
				  .get("distance")
				  .number == 2);

		std::cout << "GLOB2_SPATIAL_BENCH size=" << size << " cold_us="
				  << std::chrono::duration_cast<std::chrono::microseconds>(middle - start).count()
				  << " warm_us="
				  << std::chrono::duration_cast<std::chrono::microseconds>(finish - middle).count()
				  << " logical_work=" << coldWork << '\n';
	}
}

#include "SettingsScreen.h"
#include "GameGUIDialog.h"
#include <GraphicContext.h>
TEST_CASE("JavaScript custom library and telemetry dialogs render [display:1280x800] [artifacts]" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::HeadlessGlobals globals(
		{.display = true, .loadStrings = true, .width = 1280, .height = 800});
	auto storage = Online::makeUserDirectoryStorage();
	Library library(*storage);
	library.put("export function metadata(){return {apiVersion:2,name:'Readable "
				"Colony',version:'1.0.0',description:'A complete, commented starter AI.'}} export "
				"function step(){}",
				"example.js");
	{
		SettingsScreen settings;
		settings.beginExecution(globals->gfx);
		settings.selectCategory(SettingsScreen::Category::CustomAIs);
		settings.paintFrame(0);
		bool hasImport = false, validate = false;
		for (const auto &row : settings.rows())
		{
			hasImport |= row.id == "ai.import";
			validate |= row.id.rfind("ai.validate.", 0) == 0;
		}
		CHECK(hasImport);
		CHECK(validate);
		auto path = (glob2test::artifactDir() / "custom-ai-library.bmp").string();
		globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/custom-ai-library.bmp");
		settings.paintFrame(1);
		globals->gfx->nextFrame();
		CHECK(std::filesystem::exists(path));
	}
	glob2test::HeadlessGame world({.teams = 2, .discovered = true, .loadDefaultRace = true});
	world.gui.init();
	auto series = std::make_shared<AITelemetry::Series>();
	series->player = 0;
	series->playerName = "Readable Colony / Blue";
	series->current.available = true;
	series->named = {{"strategy.phase", "expansion", "", "Current strategic phase", 128},
					 {"colony.population", "32", "units", "Own colony population", 128},
					 {"runtime.pendingActions", "2", "orders", "Waiting for dispatch", 128}};
	world.game.teams[0]->stats.aiTelemetry = {series};
	world.gui.drawAll(0);
	const auto before = world.checksum();
	InGameAITelemetryScreen dialog(&world.gui);
	dialog.attach(*globals->gfx);
	dialog.update(0);
	dialog.draw(0);
	CHECK(dialog.host().bounds("telemetry/close").w > 0);
	CHECK(dialog.host().find("telemetry/player") != nullptr);
	auto path = (glob2test::artifactDir() / "ai-telemetry.bmp").string();
	globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/ai-telemetry.bmp");
	dialog.draw(1);
	globals->gfx->nextFrame();
	CHECK(std::filesystem::exists(path));
	CHECK(before == world.checksum());
}

TEST_CASE("JavaScript native spatial answers exclude hidden terrain resources and enemies" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true});
	auto &game = world.game;
	auto *enemy = world.addUnit(WARRIOR, 22, 22, 1);
	game.map.unsetMapDiscovered();
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	game.map.setMapDiscovered(0, 0, 8, 8, game.teams[0]->me);
	game.map.getTile(2, 2).fertility = 321;
	Observations before(game, 0);
	before.setProfile(2);
	before.observe();
	auto answers = [&](Observations &observations)
	{
		Spatial spatial(game, 0, observations);
		spatial.begin(Value::array());
		size_t work = 0;
		QueryBudget budget = [&](size_t n, size_t) { work += n; };
		auto selector = Value::object().set("sources", Value::object().set("resource", "wheat"));
		auto field = spatial.query("distanceField", {selector}, budget);
		Value result = Value::object().set(
			"field", spatial.query("fieldValue", {field, Value(2), Value(2)}, budget));
		result.set(
			"summary",
			spatial.query(
				"summary",
				{Value::object().set("x", 16).set("y", 16).set("width", 16).set("height", 16)},
				budget));
		result.set("hotspots",
				   spatial.query(
					   "hotspots",
					   {Value::object().set(
						   "sources",
						   Value::object().set("units", Value::object().set("relation", "enemy")))},
					   budget));
		return result.set("work", double(work)).encode();
	};
	auto initial = answers(before);
	game.map.getTile(22, 22).fertility = 65535;
	game.map.getTile(22, 22).terrain = 256;
	game.map.setResource(21, 21, WHEAT, 1);
	enemy->hp = 999;
	world.addUnit(WARRIOR, 24, 24, 1);
	Observations after(game, 0);
	after.setProfile(2);
	after.observe();
	CHECK(answers(after) == initial);
	game.map.switchFogOfWar();
	game.map.switchFogOfWar();
	game.map.getTile(2, 2).fertility = 999;
	Spatial remembered(game, 0, before);
	remembered.begin(Value::array());
	auto summary = remembered.query(
		"summary", {Value::object().set("x", 2).set("y", 2).set("width", 1).set("height", 1)}, {});
	CHECK(summary.get("fertility").number == 321);
	CHECK(summary.get("visibleTiles").number == 0);
	game.map.setMapDiscovered(10, 10, game.teams[0]->me);
	game.map.getTile(10, 10).terrain = 256;
	auto location = Value::object().set("x", 10).set("y", 10);
	CHECK(remembered.query("passable", {location}, {}).number == 0);
	location.set("movement", "swim");
	CHECK(remembered.query("passable", {location}, {}).number == 1);
	CHECK(remembered.query("passable", {Value::object().set("x", 22).set("y", 22)}, {}).kind ==
		  Value::Null);
}

#include <fstream>
TEST_CASE("JavaScript linked sources freeze once and removal retains external files" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	Online::MemoryStorage storage;
	Library library(storage);
	const auto path = glob2test::artifactDir() / "linked-ai.js";
	const std::string first =
		"export function metadata(){return {apiVersion:2,name:'Linked'}} export function step(){}";
	{
		std::ofstream file(path);
		file << first;
	}
	auto id = library.put(first, "linked-ai.js", {}, path.string());
	const auto frozen = library.configuration(id);
	{
		std::ofstream file(path);
		file << first << "\n// completed watch rebuild";
	}
	CHECK(library.configuration(id) != frozen);
	CHECK(sourceFromConfig(frozen) == first);
	{
		std::ofstream file(path);
		file << "unfinished syntax !";
	}
	CHECK_THROWS(library.configuration(id));
	CHECK(sourceFromConfig(frozen) == first);
	library.remove(id);
	library.collectUnusedSources();
	CHECK(std::filesystem::exists(path));
	{
		std::ofstream file(path);
		file << first;
	}
	id = library.put(first, "linked-ai.js", {}, path.string());
	std::filesystem::remove(path);
	CHECK_THROWS(library.configuration(id));
}

TEST_CASE("JavaScript queued edits preserve fairness cancellation and stale targets" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true, .header = true});
	auto *building = world.addBuilding("swarm", 5, 5);
	REQUIRE(building);
	Observations observations(world.game, 0);
	observations.setProfile(2);
	observations.observe();
	Services services(world.game, 0, observations);
	services.begin();
	auto ref = Value::object()
				   .set("id", unsigned(building->gid))
				   .set("generation", building->scriptIdentity);
	auto submit = [&](Value command)
	{
		Value batch = Value::array();
		command.set("actionId", services.nextAction());
		batch.items.push_back(command);
		services.commit(batch, Value::object());
	};
	submit(Value::object().set("type", "workers").set("building", ref).set("workers", 4));
	submit(Value::object().set("type", "priority").set("building", ref).set("priority", 1));
	for (int n = 0; n < 8; ++n)
		submit(Value::object().set("type", "workers").set("building", ref).set("workers", 5 + n));
	REQUIRE(services.actions().items.size() == 2);
	CHECK(services.actions().items[0].get("id").number == 1);
	auto first = services.dispatch();
	first->sender = 0;
	world.game.executeOrder(first, 0);
	CHECK(building->maxUnitWorking == 12);
	auto second = services.dispatch();
	second->sender = 0;
	world.game.executeOrder(second, 0);
	CHECK(building->priority == 1);
	submit(Value::object().set("type", "workers").set("building", ref).set("workers", 3));
	const auto target = services.actions().items.back().get("id");
	submit(Value::object().set("type", "cancel").set("target", target));
	CHECK(services.query("actionStatus", {target}, {}).get("status").text == "cancelled");
	CHECK(services.dispatch()->getOrderType() == ORDER_NULL);
	submit(Value::object().set("type", "workers").set("building", ref).set("workers", 6));
	const auto stale = services.actions().items.back().get("id");
	++building->scriptIdentity;
	CHECK(services.dispatch()->getOrderType() == ORDER_NULL);
	CHECK(services.query("actionStatus", {stale}, {}).get("status").text == "failed");
}

TEST_CASE("JavaScript placement reserves footprints and explains impossible constraints" *
		  doctest::test_suite("JavaScriptIntegration"))
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.discovered = true, .loadDefaultRace = true, .header = true});
	Observations observations(world.game, -1);
	observations.setProfile(2);
	observations.observe();
	Spatial spatial(world.game, 0, observations);
	spatial.begin(Value::array());
	auto request =
		Value::object()
			.set("building", "inn")
			.set("reachable", false)
			.set("clearance", 0)
			.set("region",
				 Value::object().set("x", 30).set("y", 30).set("width", 4).set("height", 4));
	auto result = spatial.query("placement", {request, Value::array()}, {});
	REQUIRE(result.get("found").number == 1);
	auto exact = result.get("order");
	CHECK(order(world.game, 0, exact)->getOrderType() == ORDER_CREATE);
	auto point = result.get("candidates").items[0];
	request.set("region", Value::object()
							  .set("x", point.get("x"))
							  .set("y", point.get("y"))
							  .set("width", 1)
							  .set("height", 1));
	Value staged = Value::array();
	staged.items.push_back(exact);
	CHECK(spatial.query("placement", {request, staged}, {}).get("found").number == 0);
	Value reservations = Value::array();
	reservations.items.push_back(Value::object().set("command", exact).set("status", "pending"));
	spatial.begin(reservations);
	CHECK(spatial.query("placement", {request, Value::array()}, {}).get("found").number == 0);
	reservations.items[0].set("status", "cancelled");
	spatial.begin(reservations);
	CHECK(spatial.query("placement", {request, Value::array()}, {}).get("found").number == 1);
	Value constraints = Value::array();
	constraints.items.push_back(Value::object().set("metric", "fertility").set("min", 2147483647));
	request.set("constraints", constraints);
	auto impossible = spatial.query("placement", {request, Value::array()}, {});
	CHECK(impossible.get("found").number == 0);
	CHECK(!impossible.get("reason").text.empty());
}
