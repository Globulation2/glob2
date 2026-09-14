// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <PerformanceTelemetry.h>
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Version.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "FileFormatVersions.h"
#include "Utilities.h"
#include "Order.h"
#include "Player.h"
#include <BackgroundFileWriter.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <FileManager.h>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#ifdef WIN32
#include <process.h>
#else
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

GlobalContainer *globalContainer = nullptr;
using namespace GAGCore;
namespace fs = std::filesystem;

static std::string contents(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	assert(file);
	return std::string(std::istreambuf_iterator<char>(file), {});
}

static void checkAtomicWrites(FileManager& files, const fs::path& directory)
{
	const std::string path = (directory / "atomic.game").string();
#ifdef WIN32
	const auto process = _getpid();
#else
	const auto process = getpid();
#endif
	const std::string collision = path + ".tmp-" + std::to_string(process) + "-0";
	{ std::ofstream existing(collision); existing << "keep"; }
	const auto write = [](OutputStream& stream) { stream.write("complete", 8, "data"); };
	assert(files.writeAtomically(path, write));
	assert(contents(collision) == "keep");
	fs::remove(collision);
	assert(contents(path) == "complete");
	assert(!files.writeAtomically(path, [](OutputStream& stream) {
		stream.write("partial", 7, "data");
		throw std::runtime_error("injected serialization failure");
	}));
	assert(contents(path) == "complete");
	assert(files.writeAtomically(path, [](OutputStream& stream) {
		stream.write("placeholder", 11, "data");
		stream.seekFromStart(0);
		stream.write("replacement", 11, "data");
	}));
	assert(contents(path) == "replacement");
	const auto blocked = directory / "directory.game";
	fs::create_directory(blocked);
	assert(!files.writeAtomically(blocked.string(), write));
	assert(fs::is_directory(blocked));
	assert(!files.writeAtomically((directory / "missing" / "save.game").string(), write));
#ifndef WIN32
	for (rlim_t limit : {rlim_t(0), rlim_t(1024)})
	{
		const pid_t child = fork();
		assert(child >= 0);
		if (child == 0)
		{
			std::signal(SIGXFSZ, SIG_IGN);
			struct rlimit budget = {limit, limit};
			if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
			const bool saved = files.writeAtomically(path, [limit](OutputStream& stream) {
				const std::string data(limit ? 16384 : 8, 'x');
				stream.write(data.data(), data.size(), "data");
			});
			_exit(saved ? 3 : 0);
		}
		int status = 0;
		assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
		assert(contents(path) == "replacement");
	}
#endif
	for (const auto& entry : fs::directory_iterator(directory))
		assert(entry.path().filename().string().find(".tmp-") == std::string::npos);
	std::cout << "PASS atomic creation/replacement, close drains pending writes, temporary-name collision, seek, serialization/open/rename failure, temporary cleanup" << std::endl;
#ifndef WIN32
	std::cout << "PASS injected short write and buffered flush failure preserve previous bytes" << std::endl;
#endif
}

static void checkBackgroundWriter(FileManager& files, const fs::path& directory)
{
	auto &perf = PerformanceTelemetry::collector();
	perf.reset();
	const std::string path = (directory / "background.game").string();
	{
		BackgroundFileWriter writer(&files);
		writer.waitUntilIdle();
		// Each snapshot's finish step travels with it, so a superseded one never runs on a newer snapshot.
		for (int i = 0; i < 50; ++i)
			writer.write(path, "snapshot " + std::to_string(i), [i](std::string& bytes) { bytes += " finished " + std::to_string(i); });
		writer.waitUntilIdle();
		assert(contents(path) == "snapshot 49 finished 49");
		writer.write(path, "finished by the destructor");
	}
	assert(contents(path) == "finished by the destructor");
	{
		BackgroundFileWriter writer(&files);
		writer.write((directory / "missing" / "save.game").string(), "unwritable");
		writer.waitUntilIdle();
		writer.write(path, "written after a failure");
	}
	assert(contents(path) == "written after a failure");
	assert(perf.saved + perf.superseded == 52 && perf.failed == 1);
	assert(perf.window[unsigned(PerformanceTelemetry::Id::SaveWrite)].time.count ==
		   perf.saved + perf.failed);
	assert(perf.window[unsigned(PerformanceTelemetry::Id::SaveQueue)].time.count ==
		   perf.saved + perf.failed);
	for (const auto& entry : fs::directory_iterator(directory))
		assert(entry.path().filename().string().find(".tmp-") == std::string::npos);
	std::cout << "PASS background writes keep the newest snapshot with its finish step, finish on destruction and continue after a failure" << std::endl;
}

static std::unique_ptr<BinaryInputStream> input(const std::string& bytes, bool file)
{
	if (file)
	{
		FILE *fp = tmpfile();
		assert(fp && fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size());
		rewind(fp);
		return std::make_unique<BinaryInputStream>(new FileStreamBackend(fp));
	}
	auto *backend = new MemoryStreamBackend(bytes.data(), bytes.size());
	backend->seekFromStart(0);
	return std::make_unique<BinaryInputStream>(backend);
}

static void checkPendingConstruction()
{
	const int type = globalContainer->buildingsTypes.getTypeNum("inn", 0, true);
	for (bool text : {false, true})
	{
		GameGUI source, target;
		for (Game* game : {&source.game, &target.game})
		{
			game->map.setSize(5, 5, GRASS);
			game->map.setGame(game);
			game->addTeam(0);
			game->teams[0]->race.loadDefault();
			game->map.setMapDiscovered();
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x) game->map.clearImmobileUnit(x, y);
		}
		const auto roundTrip = [&] {
			auto* backend = new MemoryStreamBackend;
			std::unique_ptr<OutputStream> writer;
			if (text) writer = std::make_unique<TextOutputStream>(backend);
			else writer = std::make_unique<BinaryOutputStream>(backend);
			source.game.saveBuildProjects(writer.get());
			writer->flush();
			MemoryStreamBackend bytes(*backend);
			bytes.seekFromStart(0);
			if (text)
			{
				TextInputStream reader(&bytes);
				target.game.loadBuildProjects(&reader);
			}
			else
			{
				BinaryInputStream reader(new MemoryStreamBackend(bytes));
				target.game.loadBuildProjects(&reader);
			}
		};
		source.game.buildProjects = {{8, 8, 0, type, 2, 3}, {16, 16, 0, type, 4, 5}};
		roundTrip();
		assert(target.game.buildProjects.size() == 2);
		auto first = target.game.buildProjects.begin(), second = std::next(first);
		assert(first->posX == 8 && first->posY == 8 && first->teamNumber == 0 && first->typeNum == type
			&& first->unitWorking == 2 && first->unitWorkingFuture == 3
			&& second->posX == 16 && second->posY == 16 && second->teamNumber == 0 && second->typeNum == type
			&& second->unitWorking == 4 && second->unitWorkingFuture == 5);
		target.game.map.setGroundUnit(8, 8, 0);
		target.game.buildProjectSyncStep(0);
		assert(target.game.buildProjects.size() == 1);
		target.game.map.setGroundUnit(8, 8, NOGUID);
		target.game.buildProjectSyncStep(0);
		assert(target.game.buildProjects.empty());
		assert(target.game.map.getBuilding(8, 8) != NOGBID);
		source.game.buildProjects.clear();
		roundTrip();
		assert(target.game.buildProjects.empty());
		source.game.buildProjects = {{8, 8, 99, type, 2, 3}};
		bool rejected = false;
		try { roundTrip(); }
		catch (const std::runtime_error&) { rejected = true; }
		assert(rejected && target.game.buildProjects.empty());
	}
	std::cout << "PASS pending construction binary/text round trips, queue order, staffing, delayed placement, empty queue and invalid reference" << std::endl;
}

static void checkRandomContinuation(bool text, bool ai)
{
	GameGUI gui;
	auto map = Engine::loadMapHeader("maps/balanced.map");
	GameHeader header;
	header.setNumberOfPlayers(1);
	header.setRandomSeed(123456);
	header.getBasePlayer(0) = BasePlayer(0, "Test", 0, ai ? BasePlayer::playerTypeFromImplementationID(AI::NUMBI) : BasePlayer::P_LOCAL);
	assert(gui.loadFromHeaders(map, header, true, true));
	for (int i=0; i<713; ++i) syncRand();
	const auto step = [](Game &game) {
		if (game.players[0]->ai)
		{
			auto order=game.players[0]->ai->getOrder(false);
			order->sender=0;
			game.executeOrder(order,0);
		}
		game.syncStep(0);
	};
	for (int i=0; i<100; ++i) step(gui.game);
	const auto savedRandom = syncRandEngine();
	auto *backend = new MemoryStreamBackend();
	class CheckpointOutput : public BinaryOutputStream
	{
	public:
		size_t runtimeStart=0, runtimeEnd=0;
		explicit CheckpointOutput(StreamBackend *backend) : BinaryOutputStream(backend) {}
		void writeEnterSection(const std::string name) override
		{
			if (name=="mapRuntime") runtimeStart=getPosition();
			if (name=="GameGUI") runtimeEnd=getPosition();
			BinaryOutputStream::writeEnterSection(name);
		}
	} output(backend);
	gui.save(&output, "RNG continuation");
	const std::string bytes(backend->getBuffer(), backend->getPosition());
	std::string runtimeText;
	if (text)
	{
		auto *storage=new MemoryStreamBackend();
		TextOutputStream writer(storage);
		gui.game.map.saveRuntimeState(&writer);
		runtimeText.assign(storage->getBuffer(),storage->getPosition());
	}

	if (!(syncRandEngine() == savedRandom))
	{
		std::ostringstream now, was;
		now << syncRandEngine();
		was << savedRandom;
		std::cerr << "sync RNG changed across save:\n  was " << was.str().substr(0, 96)
				  << "\n  now " << now.str().substr(0, 96) << std::endl;
		assert(false);
	}
	auto simulationState = [](Game &game) {
		std::vector<Uint32> result, buildings, units;
		game.checkSum(&result, &buildings, &units, true);
		result.erase(result.begin()); // save upgrades the map format header
		result.insert(result.end(), buildings.begin(), buildings.end());
		result.insert(result.end(), units.begin(), units.end());
		return result;
	};
	std::vector<std::vector<Uint32>> continuation;
	std::vector<std::vector<GameplayMeasurements>> measurementContinuation;
	const auto savedAI = ai ? gui.game.players[0]->ai->telemetrySeries->current : AITelemetry::Sample{};
	for (int i=0; i<700; ++i)
	{
		step(gui.game);
		continuation.push_back(simulationState(gui.game));
		std::vector<GameplayMeasurements> measurements;
		for (int t=0; t<gui.game.teamsCount(); ++t) measurements.push_back(gui.game.teams[t]->stats.measurements);
		measurementContinuation.push_back(measurements);
	}
	for (int i=0; i<37; ++i) syncRand();
	GameGUI restored;
	if (!text && !ai)
	{
		assert(output.runtimeStart>0 && output.runtimeEnd>output.runtimeStart);
		for (const auto cut : {output.runtimeStart,output.runtimeStart+7,output.runtimeEnd-1})
		{
			auto partial=input(bytes.substr(0,cut),true);
			bool rejected=false;
			try { rejected=!restored.load(partial.get()); }
			catch (const std::exception&) { rejected=true; }
			assert(rejected);
		}
		auto corrupt=bytes;
		corrupt[output.runtimeStart]=2; // fog buffer selector must be 0 or 1
		auto invalid=input(corrupt,true);
		bool rejected=false;
		try { rejected=!restored.load(invalid.get()); }
		catch (const std::exception&) { rejected=true; }
		assert(rejected);
	}
	auto stream=input(bytes,true);
	assert(restored.load(stream.get()));
	if (ai) assert(restored.game.players[0]->ai->telemetrySeries->current == savedAI);
	if (text)
	{
		MemoryStreamBackend source(runtimeText.data(),runtimeText.size());
		source.seekFromStart(0);
		TextInputStream reader(&source);
		restored.game.map.loadRuntimeState(&reader, VERSION_MINOR);
	}

	// The normal saved-game loader replaces the player header after loading.
	restored.game.setGameHeader(header, true);
	auto expected = savedRandom;
	for (int i=0; i<2000; ++i) assert(syncRand() == expected());
	syncRandEngine() = savedRandom;
	for (int i=0; i<700; ++i)
	{
		step(restored.game);
		const auto actual = simulationState(restored.game);

		for (int t=0; t<restored.game.teamsCount(); ++t)
			assert(restored.game.teams[t]->stats.measurements == measurementContinuation[i][t]);
		if (actual != continuation[i])
		{
			std::cerr << "Continuation mismatch at step " << i << " sizes " << actual.size() << '/' << continuation[i].size() << std::endl;
			for (size_t j=0; j<std::min(actual.size(),continuation[i].size()); ++j)
				if (actual[j]!=continuation[i][j]) std::cerr << " component " << j << ": " << actual[j] << " != " << continuation[i][j] << std::endl;
			assert(false);
		}
	}
	for (int t=0; t<restored.game.teamsCount(); ++t)
		assert(restored.game.teams[t]->stats.measurementHistory == gui.game.teams[t]->stats.measurementHistory);

	std::cout << "PASS " << (text ? "binary + text routing" : "binary") << (ai ? " AI" : " human") << " saved game continues RNG and 700 simulation steps and measurements across header replacement" << std::endl;
}

int main(int argc, char **argv)
{
	SDL_SetMainReady();
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::cout << "Starting headless savegame safety checks" << std::endl;
	assert(argc == 2 || argc == 3);
	assert(std::string(argv[1]).find("glob2-save-test-") == 0);
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.load();
	assert(!globals.settings.autosaveGames);
	globals.settings.rememberUnit = false;
	checkPendingConstruction();
	for (bool text : {false,true})
		for (bool ai : {false,true}) checkRandomContinuation(text,ai);
	const fs::path directory = fs::absolute(globals.fileManager->getDir(0));
	checkAtomicWrites(*globals.fileManager, directory);
	checkBackgroundWriter(*globals.fileManager, directory);
	{
		GameGUI gui;
		auto map = Engine::loadMapHeader("maps/balanced.map");
		GameHeader header;
		header.setNumberOfPlayers(1);
		header.setRandomSeed(123456);
		header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
		assert(gui.loadFromHeaders(map, header, true, true));
		gui.localPlayer = gui.localTeamNo = 0;
		gui.adjustLocalTeam();
		gui.game.stepCounter = AUTOSAVE_PHASE_TICKS;
		// The engine's initial replay save sets the serialized map offset.
		{
			BinaryOutputStream initial(new MemoryStreamBackend());
			gui.save(&initial, "Auto save");
		}
		const fs::path save = directory / "games" / "Auto_save.game";
		gui.syncStep();
		gui.waitForAutosave();
		assert(!fs::exists(save));
		globals.settings.autosaveGames = true;
		gui.syncStep();
		gui.waitForAutosave();
		std::cout << "PASS headless autosave defaults off and can be explicitly enabled" << std::endl;
		const auto bytes = contents(save);
		{
			// Autosave hashes on the writer thread; a direct save hashes as it writes.
			const fs::path reference = directory / "games" / "reference.game";
			assert(globals.fileManager->writeAtomically(reference.string(), [&](OutputStream& stream) { gui.save(&stream, "Auto save"); }));
			assert(contents(reference) == bytes);
			fs::remove(reference);
		}
		std::cout << "PASS background autosave bytes, SHA1 included, match direct serialization" << std::endl;
		{
			// A stale map offset makes the header backpatch rewrite hashed bytes; the deferred hash must still match.
			const auto serialize = [&](DeferredGameSHA1* deferred) {
				gui.game.mapHeader.setMapOffset(0);
				auto *backend = new MemoryStreamBackend();
				BinaryOutputStream stream(backend);
				gui.save(&stream, "Stale offset", deferred);
				return backend->takeContents();
			};
			const std::string inlineHashed = serialize(nullptr);
			DeferredGameSHA1 deferred;
			std::string deferredHashed = serialize(&deferred);
			assert(deferredHashed != inlineHashed);
			deferred.apply(deferredHashed);
			assert(deferredHashed == inlineHashed);
		}
		std::cout << "PASS deferred SHA1 matches an inline hash when the header backpatch changes hashed bytes" << std::endl;
		{
			// A stale map offset makes the header backpatch rewrite hashed bytes; the deferred hash must still match.
			const auto serialize = [&](DeferredGameSHA1* deferred) {
				gui.game.mapHeader.setMapOffset(0);
				auto *backend = new MemoryStreamBackend();
				BinaryOutputStream stream(backend);
				gui.save(&stream, "Stale offset", deferred);
				return backend->takeContents();
			};
			const std::string inlineHashed = serialize(nullptr);
			DeferredGameSHA1 deferred;
			std::string deferredHashed = serialize(&deferred);
			assert(deferredHashed != inlineHashed);
			deferred.apply(deferredHashed);
			assert(deferredHashed == inlineHashed);
		}
		std::cout << "PASS deferred SHA1 matches an inline hash when the header backpatch changes hashed bytes" << std::endl;

		{
			GameGUI restored;
			auto stream = input(bytes, true);
			assert(restored.load(stream.get()));
			std::vector<Uint32> before, after;
			gui.game.checkSum(&before, nullptr, nullptr);
			restored.game.checkSum(&after, nullptr, nullptr);
			assert(before.size() == after.size());
			// Component zero includes the map format version, upgraded on save.
			assert(std::equal(before.begin() + 1, before.end(), after.begin() + 1));
			assert(restored.game.stepCounter == gui.game.stepCounter);
		}
		std::cout << "PASS production autosave reloads with unchanged simulation checksum components" << std::endl;
		gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + AUTOSAVE_INTERVAL_TICKS - 1;
		gui.syncStep();
		gui.waitForAutosave();
		assert(contents(save) == bytes);
		std::cout << "PASS autosave waits for the configured interval" << std::endl;
#ifndef WIN32
		const pid_t child = fork();
		assert(child >= 0);
		if (child == 0)
		{
			std::signal(SIGXFSZ, SIG_IGN);
			struct rlimit budget = {0, 0};
			if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + AUTOSAVE_INTERVAL_TICKS;
			gui.syncStep();
			gui.waitForAutosave();
			_exit(0);
		}
		int status = 0;
		assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
		assert(contents(save) == bytes);
		std::cout << "PASS failed production autosave preserves the previous complete game" << std::endl;
#endif
		globals.settings.autosaveGames = false;
		gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + 2 * AUTOSAVE_INTERVAL_TICKS;
		gui.syncStep();
		gui.waitForAutosave();
		assert(contents(save) == bytes);
		globals.settings.autosaveGames = true;
		std::cout << "PASS disabled autosave leaves the previous save untouched" << std::endl;
		auto stream = input(bytes, false);
		MapHeader savedHeader;
		assert(savedHeader.load(stream.get()));
		assert(std::any_of(savedHeader.getGameSHA1(), savedHeader.getGameSHA1() + SHA1_BYTE_LEN, [](Uint8 byte) { return byte != 0; }));
		{
			// A joining client keeps its own copy only when its header, SHA1 included, matches the host's.
			Engine engine;
			MapHeader host = savedHeader;
			assert(host.getFileName() == "games/Auto_save.game");
			assert(engine.haveMap(host));
			host.getGameSHA1()[0] ^= 1;
			assert(!engine.haveMap(host));
		}
		std::cout << "PASS a joining client trusts a local save only when its SHA1 matches the host's" << std::endl;
		const size_t offset = savedHeader.getMapOffset();
		assert(bytes.substr(offset, 4) == "MapB");
		const auto mapBytes = bytes.substr(offset);
		const size_t cells = size_t(gui.game.map.getW()) * gui.game.map.getH();
		const size_t cellsStart = 12 + cells;
		const size_t cellsEnd = cellsStart + cells * 33;
		const size_t mapEnd = mapBytes.find("MapE");
		assert(mapEnd != std::string::npos && cellsEnd < mapEnd);
		const size_t cuts[] = {0, 1, 3, 4, 7, 11, 12, cellsStart - 1, cellsStart,
			cellsStart + 6, cellsStart + 7, cellsStart + 32, cellsEnd - 2171,
			cellsEnd - 1, cellsEnd, cellsEnd + 1, mapEnd, mapEnd + 3};
		int count = 0;
		for (bool file : {false, true})
			for (size_t cut : cuts)
			{
				Map loaded;
				auto truncated = input(mapBytes.substr(0, cut), file);
				assert(!loaded.load(truncated.get(), savedHeader, &gui.game));
				auto complete = input(mapBytes, file);
				assert(loaded.load(complete.get(), savedHeader, &gui.game));
				++count;
			}
		std::cout << "PASS " << count << " truncated map loads rejected, followed by successful reuse" << std::endl;
		for (bool file : {false, true})
		{
			std::string malformed = mapBytes;
			malformed.replace(cellsEnd, 4, 4, char(0xFF));
			auto oversized = input(malformed, file);
			Map loaded;
			assert(!loaded.load(oversized.get(), savedHeader, &gui.game));
		}
		std::cout << "PASS oversized map-area string rejected before allocation" << std::endl;

		if (argc == 3)
		{
			GameGUI broken;
			BinaryInputStream attachment(globals.fileManager->openInputStreamBackend(argv[2]));
			assert(!broken.load(&attachment));
			std::cout << "PASS issue #130 attachment rejected without abort" << std::endl;
		}
	}
	return 0;
}
