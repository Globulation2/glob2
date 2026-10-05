// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ChecksumSidecar.h"
#include "Engine.h"
#include "ReplayWriter.h"
#include "AIJavaScript.h"
#include "Player.h"
#include "IntBuildingType.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <map>
#include <zlib.h>

namespace
{
using Records = std::map<Uint32, std::string>;
Uint32 little32(const std::string &bytes, size_t position)
{
	REQUIRE(position + 4 <= bytes.size());
	Uint32 result = 0;
	for (unsigned i = 0; i < 4; ++i)
		result |= Uint32(static_cast<unsigned char>(bytes[position + i])) << (8 * i);
	return result;
}
Records records(const std::string &bytes)
{
	REQUIRE(bytes.size() >= 20);
	const auto teams = little32(bytes, 4), count = little32(bytes, 12);
	size_t position = 20;
	Records result;
	for (Uint32 i = 0; i < count; ++i)
	{
		const size_t start = position;
		const auto tick = little32(bytes, position);
		position += 8; // Includes the aggregate world/script checksum.
		for (Uint32 team = 0; team < teams; ++team)
		{
			position += 4;
			for (unsigned kind = 0; kind < 2; ++kind)
			{
				const auto entities = little32(bytes, position);
				position += 4;
				for (Uint32 entity = 0; entity < entities; ++entity)
				{
					const auto fields = little32(bytes, position + 6);
					position += 10 + 4 * size_t(fields);
					REQUIRE(position <= bytes.size());
				}
			}
		}
		REQUIRE(result.emplace(tick, bytes.substr(start, position - start)).second);
	}
	REQUIRE(position == bytes.size());
	return result;
}
void save(Engine &engine, const std::filesystem::path &path)
{
	engine.gui.game.map.finishGradientPipeline();
	GAGCore::BinaryOutputStream output(
		GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(path.string()));
	REQUIRE(output.isValid());
	engine.gui.save(&output, engine.gui.game.mapHeader.getMapName());
}
struct Run
{
	std::string trace, finalSave, replay;
};
void prepareConversion(Engine &engine, const std::filesystem::path &directory)
{
	auto &game = engine.gui.game;
	// Prime the actual controllers' global references/RNG before changing owner.
	// Record replays only after this fixture setup, so they remain self-contained.
	for (unsigned player = 0; player < 2; ++player)
	{
		auto order = game.players[player]->ai->getOrder(false);
		order->sender = player;
		game.executeOrder(order, 0);
	}
	auto *unit = game.teams[0]->myUnits[0];
	REQUIRE(unit);
	Building *inn = nullptr;
	for (unsigned slot = 0; slot < Building::MAX_COUNT; ++slot)
	{
		auto *building = game.teams[1]->myBuildings[slot];
		if (building && building->shortTypeNum == IntBuildingType::FOOD_BUILDING)
			inn = building;
	}
	REQUIRE(inn);
	inn->resources[WHEAT] = 10;
	inn->resources[CHERRY] = 10;
	inn->updateCallLists();
	inn->canNotConvertUnitTimer = 0;
	game.teams[1]->sharedVisionFood |= game.teams[0]->me;
	game.teams[1]->allies &= ~game.teams[0]->me;
	unit->hungry = unit->trigHungry;
	unit->medical = Unit::MED_HUNGRY;
	unit->needToRecheckMedical = true;
	REQUIRE(game.teams[0]->findNearestFood(unit) == inn);
	unit->handleActivity();
	REQUIRE(unit->owner == game.teams[1]);
	CHECK(unit->scriptIdentity == 2);
	// Commit a callback observing the stale reference immediately before save.
	game.mapscript.syncStep(&engine.gui);
	CHECK(game.mapscript.javascript.runtime->inspectGlobals().get("stale").number == 1);
	save(engine, directory / "conversion-0.game");
}
Run execute(const std::filesystem::path &input, const std::filesystem::path &directory,
			unsigned workers, bool playback = false, bool checkpoints = false, bool conversion = false)
{
	std::filesystem::create_directories(directory);
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.seed = 19;
	glob2test::HeadlessGlobals globals(options);
	globals->structuredHeadless = true;
	globals->automaticEndingGame = true;
	globals->automaticEndingSteps = 256;
	{
		Engine engine;
		REQUIRE((playback ? engine.loadReplay(input.string())
						  : engine.initCustom(input.string())) == Engine::EE_NO_ERROR);
        // Released checksum fixtures include the map header's save-format number.
        // Normalize that metadata after the real loader has validated the input:
        // a new save version must not masquerade as simulation divergence.
        engine.gui.game.mapHeader.versionMinor = 125;
		engine.gui.game.map.configureCompute(workers, Map::ComputeAI);
		if (conversion)
			prepareConversion(engine, directory);
		if (!playback)
		{
			globals->replayWriter = std::make_unique<ReplayWriter>();
			globals->replayWriter->init((directory / "game.replay").string(), engine.gui);
			REQUIRE(globals->replayWriter->isValid());
		}
		engine.checksumSidecar = std::make_unique<ChecksumSidecarWriter>();
		REQUIRE(
			engine.checksumSidecar->open((directory / "game.replay").string(), engine.gui.game));
		engine.beginSession(0);
		Uint64 now = 0;
		unsigned iterations = 0;
		while (engine.gui.isRunning)
		{
			REQUIRE(++iterations <= 1024);
			const auto previous = engine.gui.game.stepCounter;
			engine.stepSession(now += 40, {});
			const auto tick = engine.gui.game.stepCounter;
			if (checkpoints && tick != previous && (tick == 32 || tick == 128))
				save(engine, directory / ("checkpoint-" + std::to_string(tick) + ".game"));
		}
		REQUIRE(engine.gui.game.stepCounter == 256);
		if (!playback)
			for (int p = 0; p < engine.gui.game.gameHeader.getNumberOfPlayers(); ++p)
			{
				auto *ai = engine.gui.game.players[p]->ai;
				if (ai && ai->implementationID == AI::JAVASCRIPT)
					CHECK_FALSE(static_cast<AIJavaScript *>(ai->aiImplementation)->disabled);
			}
		if (conversion || input.filename() == "conversion-0.game")
		{
			for (unsigned player = 0; player < 2; ++player)
			{
				auto *controller = static_cast<AIJavaScript *>(
					engine.gui.game.players[player]->ai->aiImplementation);
				CHECK_FALSE(controller->disabled);
				CHECK(controller->runtime->inspectGlobals().get("calls").number > 8);
			}
			CHECK(engine.gui.game.mapscript.javascript.runtime->inspectGlobals().get("stale").number == 1);
		}
		save(engine, directory / "final.game");
		engine.finishSessionForHost();
	} // Flush the replay's terminating order through the production destructor.
	return {glob2test::readFile(directory / "game.replay.checksums"),
			glob2test::readFile(directory / "final.game"),
			playback ? "" : glob2test::readFile(directory / "game.replay")};
}
void samePayload(std::string actual, std::string expected)
{
	// Only the MapHeader SHA1 depends on save history; locate it using the
	// production header's big-endian text length, not a fixture-specific offset.
	auto sha1 = [](const std::string &bytes)
	{
		REQUIRE(bytes.size() >= 4);
		size_t length = 0;
		for (unsigned i = 0; i < 4; ++i)
			length = (length << 8) | static_cast<unsigned char>(bytes[i]);
		return 4 + length + 17;
	};
	const auto offset = sha1(actual);
	REQUIRE(offset == sha1(expected));
	REQUIRE(offset + 20 <= actual.size());
	REQUIRE(offset + 20 <= expected.size());
	actual.erase(offset, 20);
	expected.erase(offset, 20);
	CHECK(actual == expected);
}
void fixture(const std::string &name)
{
	const auto initial =
		glob2test::inflated("test/fixtures/javascript/" + name + "-initial.game.gz");
	const auto released = glob2test::readFile(
		glob2test::inflated("test/fixtures/javascript/" + name + "-256.checksums.gz"));
	const auto expanded = glob2test::readFile(
		glob2test::inflated("test/fixtures/javascript/" + name + "-256-teams16.checksums.gz"));
	// Expanding the generation table changes its aggregate hash, even when the
	// extra slots are unused. Keep every released team/entity field pinned too.
	const auto releasedRecords = records(released);
	const auto expandedRecords = records(expanded);
	REQUIRE(releasedRecords.size() == expandedRecords.size());
	for (const auto& [tick, record] : releasedRecords)
	{
		CAPTURE(tick);
		REQUIRE(expandedRecords.contains(tick));
		CHECK(record.substr(8) == expandedRecords.at(tick).substr(8));
	}
	const auto directory = glob2test::artifactDir();
	const auto serial = execute(initial, directory / "workers1", 1, false, true);
	// The terrain simulation has its own trace; retain released traces above as
	// historical migration evidence instead of rewriting their old behavior.
	const auto terrainFixture = "test/fixtures/javascript/" + name + "-256-terrain.checksums.gz";
	if (glob2test::updatingFixtures())
	{
		gzFile output = gzopen((glob2test::sourceRoot() / terrainFixture).string().c_str(), "wb9");
		REQUIRE(output != nullptr);
		const auto written = gzwrite(output, serial.trace.data(), unsigned(serial.trace.size()));
		const auto closed = gzclose(output);
		REQUIRE(written == int(serial.trace.size()));
		REQUIRE(closed == Z_OK);
	}
	const auto expected = glob2test::readFile(glob2test::inflated(terrainFixture));
	CHECK(serial.trace == expected);
	const auto parallel = execute(initial, directory / "workers4", 4, false, true);
	CHECK(parallel.trace == expected);
	CHECK(parallel.finalSave == serial.finalSave);
	CHECK(parallel.replay == serial.replay);
	const auto expectedRecords = records(expected);
	for (int boundary : {32, 128})
	{
		CAPTURE(boundary);
		const auto resumed =
			execute(directory / "workers1" / ("checkpoint-" + std::to_string(boundary) + ".game"),
					directory / ("resumed-" + std::to_string(boundary)), 4);
		const auto tail = records(resumed.trace);
		CHECK(tail.size() == size_t(256 - boundary));
		for (const auto &[tick, record] : tail)
		{
			CAPTURE(tick);
			REQUIRE(expectedRecords.contains(tick));
			CHECK(record == expectedRecords.at(tick));
		}
		samePayload(resumed.finalSave, serial.finalSave);
	}
	const auto playback =
		execute(directory / "workers1/game.replay", directory / "playback", 1, true);
	CHECK(playback.trace == expected);
}
} // namespace
TEST_CASE("JavaScript original fixture executes, resumes and replays 256 ticks" *
		  doctest::test_suite("JavaScriptSimulation"))
{
	fixture("profile1");
}
TEST_CASE("JavaScript economic planners and map survey execute, resume and replay 256 ticks" *
		  doctest::test_suite("JavaScriptSimulation"))
{
	fixture("realistic-profile1");
}

TEST_CASE("JavaScript conversion boundary resumes globals RNG references and complete tick records" *
		  doctest::test_suite("JavaScriptSimulation"))
{
	const auto directory = glob2test::artifactDir();
	const auto initial = directory / "conversion-initial.game";
	{
		glob2test::GlobalsOptions options;
		options.loadStrings = true;
		options.seed = 19;
		glob2test::HeadlessGlobals globals(options);
		glob2test::GameOptions gameOptions;
		gameOptions.teams = 2;
		gameOptions.discovered = true;
		gameOptions.loadDefaultRace = true;
		glob2test::HeadlessGame world(gameOptions);
		world.gui.init();
		world.gui.localPlayer = 0;
		world.gui.localTeamNo = 0;
		REQUIRE(world.game.sgslScript.compileScript(&world.game, "").type == ErrorReport::ET_OK);
		world.addBuilding("swarm", 2, 2, 0, 0);
		world.addBuilding("swarm", 24, 24, 0, 1);
		world.addBuilding("inn", 8, 8, 0, 1);
		world.addUnit(WORKER, 24, 20, 1); // Survival requires workers on both teams.
		world.addUnit(EXPLORER, 20, 20, 1);
		REQUIRE(world.game.removeUnitAndBuildingAndFlags(20, 20, Game::DEL_UNIT));
		world.addUnit(EXPLORER, 12, 8, 0);
		world.addUnit(WORKER, 20, 8, 0); // Keep the source team alive after conversion.
		GameHeader header;
		header.setNumberOfPlayers(2);
		header.setRandomSeed(19);
		header.setMapDiscovered(true);
		const std::string aiSource = R"(
let calls = 0, first = null, draws = [], sightings = [];
function step(ctx) {
  calls++;
  const units = ctx.game.units({team:ctx.myTeam});
  if (!first && units.length) first = units[0];
  sightings.push(first ? ctx.game.unit(first)?.generation ?? null : null);
  const sample = ctx.random();
  draws.push(Math.hypot(sample, units.length + 0.25));
  const building = ctx.game.buildings({team:ctx.myTeam})[0];
  return {type:'workers',building,workers:1 + Math.floor(sample * 4)};
})";
		for (unsigned player = 0; player < 2; ++player)
		{
			header.getBasePlayer(player) = BasePlayer(player, "Conversion script", player,
				BasePlayer::playerTypeFromImplementationID(AI::JAVASCRIPT));
			header.setAIConfig(player, Script::config(aiSource));
		}
		world.game.setGameHeader(header);
		auto &scenario = world.game.mapscript;
		scenario.setMapScriptMode(MapScript::JavaScript);
		scenario.setMapScript(R"(
let first = null, stale = false, calls = 0, samples = [];
function step(ctx) {
  calls++;
  if (!first) first = ctx.game.units({team:0})[0];
  stale = ctx.game.unit(first) === null;
  samples.push(Math.hypot(ctx.random(), calls));
  return [{type:'message',text:stale ? 'Converted reference is stale' : 'Original reference'}];
})");
		REQUIRE(scenario.compileCode());
		scenario.syncStep(&world.gui);
		GAGCore::BinaryOutputStream output(
			GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(initial.string()));
		REQUIRE(output.isValid());
		world.gui.save(&output, "Conversion boundary");
	}
	const auto serial = execute(initial, directory / "workers1", 1, false, false, true);
	const auto parallel = execute(initial, directory / "workers4", 4, false, false, true);
	CHECK(parallel.trace == serial.trace);
	CHECK(parallel.finalSave == serial.finalSave);
	CHECK(parallel.replay == serial.replay);
	const auto resumed = execute(directory / "workers1/conversion-0.game", directory / "resumed-conversion", 4);
	const auto complete = records(serial.trace);
	const auto tail = records(resumed.trace);
	CHECK(tail.size() == 256);
	for (const auto &[tick, record] : tail)
	{
		CAPTURE(tick);
		REQUIRE(complete.contains(tick));
		CHECK(record == complete.at(tick));
	}
	samePayload(resumed.finalSave, serial.finalSave);
	const auto playback = execute(directory / "workers1/game.replay", directory / "playback", 1, true);
	CHECK(playback.trace == serial.trace);
}

TEST_CASE("JavaScript profile two actions fields telemetry and construction survive save resume "
		  "and worker counts" *
		  doctest::test_suite("JavaScriptSimulation"))
{
	const auto directory = glob2test::artifactDir();
	const auto initial = directory / "profile2-initial.game";
	{
		glob2test::GlobalsOptions options;
		options.loadStrings = true;
		options.seed = 19;
		glob2test::HeadlessGlobals globals(options);
		glob2test::HeadlessGame world({.teams = 2, .discovered = true, .loadDefaultRace = true});
		world.gui.init();
		world.gui.localPlayer = 0;
		world.gui.localTeamNo = 0;
		REQUIRE(world.game.sgslScript.compileScript(&world.game, "").type == ErrorReport::ET_OK);
		world.addBuilding("swarm", 2, 2, 0, 0);
		world.addBuilding("swarm", 24, 24, 0, 1);
		world.addUnit(WORKER, 8, 8, 0);
		world.addUnit(WORKER, 20, 20, 1);
		GameHeader header;
		header.setNumberOfPlayers(2);
		header.setRandomSeed(19);
		header.setMapDiscovered(true);
		const std::string source = R"(
var calls=0, project=null, reference=null;
function metadata2(){return {apiVersion:2,name:'Profile two continuation'};}
function step2(ctx){
 calls++;
 let b=ctx.game.buildings({team:ctx.myTeam})[0];if(!b)return;
 reference=b.ref;b.workers=1+Math.floor(ctx.random()*4);b.priority=calls%2;
 if(project===null){let t=ctx.game.buildingTypes().find(t=>t.name==='inn' && t.site && t.level===0);
  project=ctx.actions.create({buildingType:t.id,x:(b.x+6)%ctx.game.map.width,y:b.y,workers:2,futureWorkers:2});}
 let field=ctx.spatial.distanceField({sources:{points:[{x:b.x,y:b.y}]},metric:'manhattan'});
 let at=ctx.spatial.fieldValue(field,b.x,b.y);
 ctx.telemetry.set('strategy.calls',calls);ctx.telemetry.set('strategy.homeDistance',at.distance===null?-1:at.distance);
}
export {metadata2 as metadata,step2 as step};
)";
		for (unsigned p = 0; p < 2; ++p)
		{
			header.getBasePlayer(p) = BasePlayer(
				p, "Profile two", p, BasePlayer::playerTypeFromImplementationID(AI::JAVASCRIPT));
			header.setAIConfig(p, Script::config(source, 2));
		}
		world.game.setGameHeader(header);
		GAGCore::BinaryOutputStream output(
			GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(initial.string()));
		world.gui.save(&output, "Profile two continuation");
	}
	const auto serial = execute(initial, directory / "workers1", 1, false, true);
	const auto parallel = execute(initial, directory / "workers4", 4);
	CHECK(serial.trace == parallel.trace);
	samePayload(serial.finalSave, parallel.finalSave);
	CHECK(serial.replay == parallel.replay);
	for (int checkpoint : {32, 128})
	{
		const auto resumed =
			execute(directory / ("workers1/checkpoint-" + std::to_string(checkpoint) + ".game"),
					directory / ("resume-" + std::to_string(checkpoint)), 4);
		const auto complete = records(serial.trace);
		for (const auto &[tick, record] : records(resumed.trace))
		{
			REQUIRE(complete.contains(tick));
			CHECK(record == complete.at(tick));
		}
		samePayload(resumed.finalSave, serial.finalSave);
	}
	const auto replay =
		execute(directory / "workers1/game.replay", directory / "playback", 1, true);
	CHECK(replay.trace == serial.trace);
}
