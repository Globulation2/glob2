// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ChecksumSidecar.h"
#include "Engine.h"
#include "ReplayWriter.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <map>

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
Run execute(const std::filesystem::path &input, const std::filesystem::path &directory,
			unsigned workers, bool playback = false, bool checkpoints = false)
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
		engine.gui.game.map.configureCompute(workers, Map::ComputeAI);
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
	const auto expected = glob2test::readFile(
		glob2test::inflated("test/fixtures/javascript/" + name + "-256.checksums.gz"));
	const auto directory = glob2test::artifactDir();
	const auto serial = execute(initial, directory / "workers1", 1, false, true);
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
