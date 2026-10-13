#include <Environment.h>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "FileFormatVersions.h"
#include "ScopedEnvironment.h"
#include "OrderMessages.h"
#include "Order.h"
#include "ReplayReader.h"
#include "Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <cstdio>
#include <locale>
#include <sstream>
#include <TextStream.h>

TEST_CASE("JavaScript test environment scopes restore SDL and CRT readers" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	const auto original = glob2test::profileDir();
	const auto originalCRT = []() -> std::optional<std::string> {
		if (const char *value = std::getenv("GLOB2_USER_DATA_DIR"))
			return std::string(value);
		return std::nullopt;
	}();
	const auto currentCRT = [](const char *name) -> std::optional<std::string> {
		if (const char *value = std::getenv(name))
			return std::string(value);
		return std::nullopt;
	};
	const auto outer = original / "environment-outer";
	const auto inner = original / "environment-inner";
	{
		glob2test::ScopedEnvironment outerProfile("GLOB2_USER_DATA_DIR", outer.string().c_str());
		CHECK(glob2test::profileDir() == outer);
		CHECK(currentCRT("GLOB2_USER_DATA_DIR") == outer.string());
		{
			glob2test::ScopedEnvironment innerProfile("GLOB2_USER_DATA_DIR", inner.string().c_str());
			CHECK(glob2test::profileDir() == inner);
			CHECK(currentCRT("GLOB2_USER_DATA_DIR") == inner.string());
		}
		CHECK(glob2test::profileDir() == outer);
		CHECK(currentCRT("GLOB2_USER_DATA_DIR") == outer.string());
	}
	CHECK(glob2test::profileDir() == original);
	CHECK(currentCRT("GLOB2_USER_DATA_DIR") == originalCRT);

	// Restore an absent variable through SDL's own environment, including
	// Windows and SDL2-compat. An empty cached value is also inactive.
	const auto absent = "GLOB2_TEST_SCOPED_ENV_" + original.filename().string();
	REQUIRE(SDL_getenv_unsafe(absent.c_str()) == nullptr);
	REQUIRE(!currentCRT(absent.c_str()));
	const auto currentValue = [&] {
		const char *value = SDL_getenv_unsafe(absent.c_str());
		REQUIRE(value != nullptr);
		return std::string(value);
	};
	{
		glob2test::ScopedEnvironment outerValue(absent.c_str(), "outer");
		CHECK(currentValue() == "outer");
		CHECK(currentCRT(absent.c_str()) == "outer");
		{
			glob2test::ScopedEnvironment innerValue(absent.c_str(), "inner");
			CHECK(currentValue() == "inner");
			CHECK(currentCRT(absent.c_str()) == "inner");
		}
		CHECK(currentValue() == "outer");
		CHECK(currentCRT(absent.c_str()) == "outer");
	}
	const char *restored = SDL_getenv_unsafe(absent.c_str());
	CHECK((!restored || !*restored));
	CHECK(!currentCRT(absent.c_str()));
#ifdef _WIN32
	// A previous SDL-only write can leave the process and CRT views different.
	// Preserve both, rather than replacing one original with the other.
	REQUIRE(_putenv_s(absent.c_str(), "crt-original") == 0);
	REQUIRE(SDL_setenv_unsafe(absent.c_str(), "sdl-original", 1) == 0);
	REQUIRE(currentValue() == "sdl-original");
	REQUIRE(currentCRT(absent.c_str()) == "crt-original");
	{
		glob2test::ScopedEnvironment value(absent.c_str(), "temporary");
		CHECK(currentValue() == "temporary");
		CHECK(currentCRT(absent.c_str()) == "temporary");
	}
	CHECK(currentValue() == "sdl-original");
	CHECK(currentCRT(absent.c_str()) == "crt-original");
	REQUIRE(_putenv_s(absent.c_str(), "") == 0);
	REQUIRE(GAGCore::setProcessEnvironment(absent.c_str(), "", 1) == 0);
#endif
}

TEST_CASE("JavaScript current text saves validate unused generation counters" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::HeadlessGlobals globals;
	GameGUI original;
	GAGCore::BinaryInputStream initial(glob2OpenMapOrSaveInputStreamBackend(
		*GAGCore::Toolkit::getFileManager(),
		glob2test::inflated("javascript/profile1-initial159.game.gz").string()));
	REQUIRE(original.game.load(&initial));
	auto *storage = new GAGCore::MemoryStreamBackend;
	GAGCore::TextOutputStream output(storage);
	original.game.save(&output, false, "Counter validation");
	const auto text = storage->takeContents();
	const auto position = text.rfind("value = ");
	REQUIRE(position != std::string::npos);
	const auto end = text.find(';', position);
	REQUIRE(end != std::string::npos);
	const auto load = [](const std::string &bytes) {
		GAGCore::MemoryStreamBackend backend(bytes.data(), bytes.size());
		backend.seekFromStart(0);
		GAGCore::TextInputStream input(&backend);
		GameGUI restored;
		return restored.game.load(&input);
	};
	REQUIRE(load(text));
	for (const char *value : {"", "invalid", "-1", "4294967296", "0 trailing"})
	{
		CAPTURE(value);
		auto corrupted = text;
		corrupted.replace(position + 8, end - position - 8, value);
		CHECK_THROWS_AS(load(corrupted), std::runtime_error);
	}
	auto missing = text;
	missing.erase(position, end - position + 1);
	CHECK_THROWS_AS(load(missing), std::runtime_error);
}

TEST_CASE("Resource oscillation rejects earlier replays and enforces acceptance boundaries" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	CHECK(REPLAY_MINIMUM_VERSION_MINOR == 159);
	CHECK(VERSION_MINOR == 159);
	CHECK(NET_PROTOCOL_VERSION == 70);
	CHECK(FILE_FORMAT_VERSION_JAVASCRIPT == 125);
	CHECK(FILE_FORMAT_VERSION_EXPERIMENTS == 124);
	ReplayReader released;
	CHECK_FALSE(
		released.loadReplay(glob2test::inflated("javascript/released-v123.replay.gz").string()));
	const Uint16 versions[] = {122, 123, 124, 125, 126, 127, 128, FILE_FORMAT_VERSION_CUSTOM_AI, FILE_FORMAT_VERSION_FARM_AREA, 133, 134, 135, FILE_FORMAT_VERSION_RUNTIME_TERRAIN, FILE_FORMAT_VERSION_BUILDING_CATALOG, FILE_FORMAT_VERSION_TERRAIN_SEED, 139, FILE_FORMAT_VERSION_RUNTIME_RESOURCES, FILE_FORMAT_VERSION_TERRAIN_CATALOGUE, FILE_FORMAT_VERSION_HAZARD_ROUTING, FILE_FORMAT_VERSION_AI_PIPELINE, FILE_FORMAT_VERSION_MAP_ASSETS, FILE_FORMAT_VERSION_BUILDING_ARTWORK, FILE_FORMAT_VERSION_VERTEX_TERRAIN, FILE_FORMAT_VERSION_GREEDY_FETCHING, FILE_FORMAT_VERSION_PRIVATE_RANDOM - 1, FILE_FORMAT_VERSION_PRIVATE_RANDOM, FILE_FORMAT_VERSION_UNIT_CATALOG, VERSION_MINOR, VERSION_MINOR + 1};
	for (Uint16 version : versions)
	{
		CAPTURE(version);
		auto *memory = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(memory);
		output.writeUint16(VERSION_MAJOR, "versionMajor");
		output.writeUint16(version, "versionMinor");
		output.writeUint32(0, "replayStepsSinceLastOrder");
		NetSendOrder(std::make_shared<NullOrder>()).encodeData(&output);
		if (version >= FILE_FORMAT_VERSION_CUSTOM_AI)
			ReplayTelemetry::Stream().write(&output);
		auto bytes = memory->takeContents();
		auto *input = new GAGCore::BinaryInputStream(
			new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input->seekFromStart(0);
		ReplayReader reader;
		CHECK(reader.loadReplay(input, false) == (version >= REPLAY_MINIMUM_VERSION_MINOR && version <= VERSION_MINOR));
	}
}

TEST_CASE("released saves below the current floor are rejected" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
    glob2test::HeadlessGlobals globals;
    CHECK(MINIMUM_VERSION_MINOR == 58);
    for (const char* fixture : {"team-stats/version88.game.gz", "team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz", "ai-random-streams/numbi-castor-v121.game.gz"}) {
        CAPTURE(fixture);
        GameGUI gui;
        GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), glob2test::fixture(fixture).string()));
        CHECK_FALSE(gui.game.load(&input));
    }
}

TEST_CASE("the obsolete javascript/master-v124-experiments.game.gz save is rejected [save-format]" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
    glob2test::HeadlessGlobals globals;
    const auto bytes = glob2test::readFile(glob2test::inflated("javascript/master-v124-experiments.game.gz"));
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    input.seekFromStart(0);
    GameGUI restored;
    CHECK_FALSE(restored.game.load(&input));
}

TEST_CASE("JavaScript current saves reject truncated generation tables" *
		  doctest::test_suite("JavaScriptCompatibility"))
{
	glob2test::HeadlessGlobals globals;
	const auto bytes = glob2test::readFile(
		glob2test::inflated("javascript/profile1-initial159.game.gz"));
	GAGCore::BinaryInputStream complete(
		new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
	complete.seekFromStart(0);
	GameGUI original;
	REQUIRE(original.game.load(&complete));
	const auto counterEnd = complete.getPosition();
	REQUIRE(counterEnd > 4);
	// The last counter is unused and zero. Partial file reads previously
	// left zero bytes in place and bypassed the live-identity validation.
	for (size_t removed : {1, 2, 3, 4})
	{
		CAPTURE(removed);
		const auto truncated = bytes.substr(0, counterEnd - removed);
		const auto path = glob2test::artifactDir() /
			("truncated-generations-" + std::to_string(removed) + ".bin");
		glob2test::writeFile(path, truncated);
		for (bool file : {false, true})
		{
			CAPTURE(file);
			GAGCore::StreamBackend *backend;
			if (file)
			{
				auto *handle = std::fopen(path.string().c_str(), "rb");
				REQUIRE(handle);
				backend = new GAGCore::FileStreamBackend(handle);
			}
			else
				backend = new GAGCore::MemoryStreamBackend(truncated.data(), truncated.size());
			GAGCore::BinaryInputStream input(backend);
			input.seekFromStart(0);
			GameGUI target;
			CHECK_THROWS_AS(target.game.load(&input), std::ios_base::failure);
		}
	}
}
