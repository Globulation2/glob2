// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <vector>
#include <string>
#include <utility>
#include <chrono>
#include <iterator>
#include <exception>
#include <PerformanceTelemetry.h>
#include "GlobalContainer.h"
#include "Version.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "FileFormatVersions.h"
#include "Utilities.h"
#include "Order.h"
#include "Player.h"
#include <BackgroundFileWriter.h>
#include "Version.h"
#include "FileImport.h"
#include "Campaign.h"
#include "KeyboardManager.h"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <FileManager.h>
#include <GzipUtil.h>
#include <algorithm>
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

namespace
{

using namespace GAGCore;
namespace fs = std::filesystem;

static std::string contents(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	return std::string(std::istreambuf_iterator<char>(file), {});
}

static void checkGzipWrites(FileManager& files, const fs::path& directory)
{
	const std::string original(200000, 'x');
	std::string compressed, again, decoded;
	REQUIRE(gzipCompress(original, 6, compressed));
	REQUIRE((gzipCompress(original, 6, again) && again == compressed));
	REQUIRE((gzipDecompress(compressed, decoded) && decoded == original));
	REQUIRE(!gzipDecompress(compressed, decoded, original.size() - 1));
	REQUIRE(!gzipDecompress(compressed.substr(0, compressed.size()-1), decoded));
	REQUIRE(!gzipDecompress(compressed + "trailing", decoded));
	again = compressed; again[again.size()-8] ^= 1;
	REQUIRE(!gzipDecompress(again, decoded));
	const auto path = (directory / "atomic.game.gz").string();
	REQUIRE(files.writeGzipAtomic(path, original));
	REQUIRE(contents(path) == compressed);
	REQUIRE(!files.writeGzipAtomically(path, [](OutputStream& stream) {
		stream.write("partial", 7, "data");
		throw std::runtime_error("injected serialization failure");
	}));
	REQUIRE(contents(path) == compressed);
	REQUIRE(!files.writeGzipAtomic((directory / "missing" / "save.game.gz").string(), original));
	const auto blocked = directory / "blocked.game.gz";
	fs::create_directory(blocked);
	REQUIRE(!files.writeGzipAtomic(blocked.string(), original));
	for (const auto& entry : fs::directory_iterator(directory))
		REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
	std::cout << "PASS gzip deterministic encoding, round trip, size limit, corruption rejection and failed atomic replacement" << std::endl;
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
	REQUIRE(files.writeAtomically(path, write));
	REQUIRE(contents(collision) == "keep");
	fs::remove(collision);
	REQUIRE(contents(path) == "complete");
	REQUIRE(!files.writeAtomically(path, [](OutputStream& stream) {
		stream.write("partial", 7, "data");
		throw std::runtime_error("injected serialization failure");
	}));
	REQUIRE(contents(path) == "complete");
	REQUIRE(files.writeAtomically(path, [](OutputStream& stream) {
		stream.write("placeholder", 11, "data");
		stream.seekFromStart(0);
		stream.write("replacement", 11, "data");
	}));
	REQUIRE(contents(path) == "replacement");
	const auto blocked = directory / "directory.game";
	fs::create_directory(blocked);
	REQUIRE(!files.writeAtomically(blocked.string(), write));
	REQUIRE(fs::is_directory(blocked));
	REQUIRE(!files.writeAtomically((directory / "missing" / "save.game").string(), write));
#ifndef WIN32
	for (rlim_t limit : {rlim_t(0), rlim_t(1024)})
	{
		const pid_t child = fork();
		REQUIRE(child >= 0);
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
		REQUIRE((waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0));
		REQUIRE(contents(path) == "replacement");
	}
#endif
	for (const auto& entry : fs::directory_iterator(directory))
		REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
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
		REQUIRE(contents(path) == "snapshot 49 finished 49");
		writer.write(path, "finished by the destructor");
	}
	REQUIRE(contents(path) == "finished by the destructor");
	{
		BackgroundFileWriter writer(&files);
		writer.write((directory / "missing" / "save.game").string(), "unwritable");
		writer.waitUntilIdle();
		writer.write(path, "written after a failure");
	}
	REQUIRE(contents(path) == "written after a failure");
	REQUIRE((perf.saved + perf.superseded == 52 && perf.failed == 1));
	REQUIRE(perf.window[unsigned(PerformanceTelemetry::Id::SaveWrite)].time.count ==
		   perf.saved + perf.failed);
	REQUIRE(perf.window[unsigned(PerformanceTelemetry::Id::SaveQueue)].time.count ==
		   perf.saved + perf.failed);
	for (const auto& entry : fs::directory_iterator(directory))
		REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
	std::cout << "PASS background writes keep the newest snapshot with its finish step, finish on destruction and continue after a failure" << std::endl;
}

static std::unique_ptr<BinaryInputStream> input(const std::string& bytes, bool file)
{
	if (file)
	{
		FILE *fp = tmpfile();
		REQUIRE((fp && fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size()));
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
		REQUIRE(target.game.buildProjects.size() == 2);
		auto first = target.game.buildProjects.begin(), second = std::next(first);
		REQUIRE((first->posX == 8 && first->posY == 8 && first->teamNumber == 0 && first->typeNum == type
			&& first->unitWorking == 2 && first->unitWorkingFuture == 3
			&& second->posX == 16 && second->posY == 16 && second->teamNumber == 0 && second->typeNum == type
			&& second->unitWorking == 4 && second->unitWorkingFuture == 5));
		target.game.map.setGroundUnit(8, 8, 0);
		target.game.buildProjectSyncStep(0);
		REQUIRE(target.game.buildProjects.size() == 1);
		target.game.map.setGroundUnit(8, 8, NOGUID);
		target.game.buildProjectSyncStep(0);
		REQUIRE(target.game.buildProjects.empty());
		REQUIRE(target.game.map.getBuilding(8, 8) != NOGBID);
		source.game.buildProjects.clear();
		roundTrip();
		REQUIRE(target.game.buildProjects.empty());
		source.game.buildProjects = {{8, 8, 99, type, 2, 3}};
		bool rejected = false;
		try { roundTrip(); }
		catch (const std::runtime_error&) { rejected = true; }
		REQUIRE((rejected && target.game.buildProjects.empty()));
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
	REQUIRE(gui.loadFromHeaders(map, header, true, true));
	// Advance the game's own stream away from its seed before checkpointing.
	for (int i=0; i<713; ++i) gui.game.syncRandom();
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
	const auto savedRandom = gui.game.syncRandom;
	auto *backend = new MemoryStreamBackend();
	class CheckpointOutput : public BinaryOutputStream
	{
	public:
		size_t runtimeStart=0, runtimeEnd=0, pipelineStart=0;
		explicit CheckpointOutput(StreamBackend *backend) : BinaryOutputStream(backend) {}
		void writeEnterSection(const std::string name) override
		{
			if (name=="mapRuntime") runtimeStart=getPosition();
			if (name=="GameGUI") runtimeEnd=getPosition();
			if (name=="gradientPipeline") pipelineStart=getPosition();
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

	if (!(gui.game.syncRandom == savedRandom))
	{
		std::ostringstream now, was;
		now << gui.game.syncRandom;
		was << savedRandom;
		std::cerr << "sync RNG changed across save:\n  was " << was.str().substr(0, 96)
				  << "\n  now " << now.str().substr(0, 96) << std::endl;
		REQUIRE(false);
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
	// Unrelated draws from the process stream must not affect the restored game.
	for (int i=0; i<37; ++i) syncRand();
	GameGUI restored;
	if (!text && !ai)
	{
		REQUIRE((output.runtimeStart>0 && output.runtimeEnd>output.runtimeStart));
		for (const auto cut : {output.runtimeStart,output.runtimeStart+7,output.runtimeEnd-1})
		{
			auto partial=input(bytes.substr(0,cut),true);
			bool rejected=false;
			try { rejected=!restored.load(partial.get()); }
			catch (const std::exception&) { rejected=true; }
			REQUIRE(rejected);
		}
		REQUIRE(output.pipelineStart>output.runtimeStart);
		// Reject invalid queue sizes, deadlines, destinations and cancellation flags.
		const auto pipeline=output.pipelineStart;
		REQUIRE(static_cast<unsigned char>(bytes[pipeline+1])>0);
		for (const auto [offset,value] : std::vector<std::pair<size_t,unsigned char>>{
			{pipeline,0}, {pipeline,17}, {pipeline+1,17},
			{pipeline+2,255}, {pipeline+4,0}, {pipeline+4,17}, {pipeline+5,2}}) {
			auto malformed=bytes; malformed[offset]=value;
			auto bad=input(malformed,true); bool rejected=false;
			try { rejected=!restored.load(bad.get()); } catch(const std::exception &) { rejected=true; }
			REQUIRE(rejected);
		}
		auto corrupt=bytes;
		corrupt[output.runtimeStart]=2; // fog buffer selector must be 0 or 1
		auto invalid=input(corrupt,true);
		bool rejected=false;
		try { rejected=!restored.load(invalid.get()); }
		catch (const std::exception&) { rejected=true; }
		REQUIRE(rejected);
	}
	auto stream=input(bytes,true);
	REQUIRE(restored.load(stream.get()));
	if (ai) REQUIRE(restored.game.players[0]->ai->telemetrySeries->current == savedAI);
	if (text)
	{
		MemoryStreamBackend source(runtimeText.data(),runtimeText.size());
		source.seekFromStart(0);
		TextInputStream reader(&source);
		restored.game.map.loadRuntimeState(&reader, VERSION_MINOR);
	}

	// The normal saved-game loader replaces the player header after loading.
	restored.game.setGameHeader(header, true);
	REQUIRE(restored.game.syncRandom == savedRandom);
	for (int i=0; i<700; ++i)
	{
		step(restored.game);
		const auto actual = simulationState(restored.game);

		for (int t=0; t<restored.game.teamsCount(); ++t)
			REQUIRE(restored.game.teams[t]->stats.measurements == measurementContinuation[i][t]);
		if (actual != continuation[i])
		{
			std::cerr << "Continuation mismatch at step " << i << " sizes " << actual.size() << '/' << continuation[i].size() << std::endl;
			for (size_t j=0; j<std::min(actual.size(),continuation[i].size()); ++j)
				if (actual[j]!=continuation[i][j]) std::cerr << " component " << j << ": " << actual[j] << " != " << continuation[i][j] << std::endl;
			REQUIRE(false);
		}
	}
	for (int t=0; t<restored.game.teamsCount(); ++t)
		REQUIRE(restored.game.teams[t]->stats.measurementHistory == gui.game.teams[t]->stats.measurementHistory);

	std::cout << "PASS " << (text ? "binary + text routing" : "binary") << (ai ? " AI" : " human") << " saved game continues RNG and 700 simulation steps and measurements across header replacement" << std::endl;
}

static std::string headerBytes(const MapHeader& header)
{
	auto *backend = new MemoryStreamBackend;
	BinaryOutputStream output(backend);
	header.save(&output);
	backend->seekFromStart(0);
	std::string bytes;
	while (!backend->isEndOfStream()) bytes += char(backend->getChar());
	return bytes;
}

static void checkMapHeaders()
{
	MapHeader source;
	source.setMapName("Import validation");
	source.setNumberOfTeams(1);
	const std::string bytes = headerBytes(source);
	for (bool file : {false, true})
	{
		MapHeader header;
		header.setMapName("Previous selection");
		const std::string previous = headerBytes(header);
		for (size_t cut = 0; cut < bytes.size(); ++cut)
		{
			auto stream = input(bytes.substr(0, cut), file);
			bool rejected = false;
			try { rejected = !header.load(stream.get()); }
			catch (const std::ios_base::failure&) { rejected = true; }
			REQUIRE((rejected && headerBytes(header) == previous));
		}
		const size_t fields = 4 + source.getMapName().size();
		const auto replace = [&](size_t offset, Uint32 value) {
			std::string corrupt = bytes;
			for (int i = 0; i < 4; ++i) corrupt[offset + i] = char(value >> (24 - i * 8));
			return corrupt;
		};
		for (const auto& corrupt : {replace(fields, VERSION_MAJOR + 1),
			replace(fields + 4, MINIMUM_VERSION_MINOR - 1), replace(fields + 4, VERSION_MINOR + 1),
			replace(fields + 8, 0xffffffffu), replace(fields + 8, Team::MAX_COUNT + 1)})
		{
			auto stream = input(corrupt, file);
			REQUIRE((!header.load(stream.get()) && headerBytes(header) == previous));
		}
		auto corrupt = bytes;
		corrupt[fields + 16] = 2;
		auto invalid = input(corrupt, file);
		REQUIRE((!header.load(invalid.get()) && headerBytes(header) == previous));
		auto valid = input(bytes, file);
		REQUIRE((header.load(valid.get()) && headerBytes(header) == bytes));
	}
	std::cout << "PASS every truncated header, invalid versions/team counts/save flags rejected; previous header preserved; valid reload succeeds" << std::endl;
}

static void checkImports(const std::string& bytes, const fs::path& directory)
{
    using namespace ApplicationHost;
    const auto selected = [&](const std::string& name, const std::string& payload) {
        return SelectedFile{name, std::vector<unsigned char>(payload.begin(), payload.end())};
    };
    const auto validate = [](FileImport& operation) {
        for (unsigned i = 0; i < 100000 && operation.state() == FileImport::State::Validating; ++i) operation.advance();
        REQUIRE(operation.state() != FileImport::State::Validating);
    };
    const auto beforeRng = getSyncRandState();
    const auto preferences = directory / "preferences.txt";
    { std::ofstream out(preferences); out << "preserved preferences"; }
    const auto preferencesTime = fs::last_write_time(preferences);
    const bool remember = globalContainer->settings.rememberUnit;
    globalContainer->settings.rememberUnit = true;
    {
        FileImport cancelled(selected("cancel.game", bytes), "game", persistStorage,
            CooperativeSlice(std::chrono::steady_clock::now, std::chrono::milliseconds(4), 1));
        cancelled.advance();
        REQUIRE(cancelled.state() == FileImport::State::Validating);
    }
    REQUIRE((getSyncRandState() == beforeRng && !fs::exists(directory / "games/cancel.game")));
    for (const auto& payload : {bytes.substr(0, 3), bytes.substr(0, bytes.size()-1), bytes + "extra"}) {
        FileImport invalid(selected("invalid.game", payload), "game");
        validate(invalid);
        REQUIRE((invalid.state() == FileImport::State::Failed && invalid.path().empty()));
    }
    {
        auto stream = input(bytes, false);
        MapHeader header; REQUIRE(header.load(stream.get()));
        auto badCount = bytes;
        // GameHeader begins with latency (4), order rate (1), player count (4).
        std::fill_n(badCount.begin() + stream->getPosition() + 5, 4, char(0xff));
        auto badOffset = bytes;
        const auto offsetField = 4 + header.getMapName().size() + 12;
        const Uint32 offset = header.getMapOffset() + 1;
        for (int i = 0; i < 4; ++i) badOffset[offsetField+i] = char(offset >> (24-i*8));
        auto badPlayer = bytes;
        const auto player = badPlayer.find("PLYb"); REQUIRE(player != std::string::npos);
        badPlayer[player] = '!';
        for (const auto& corrupt : {badCount, badPlayer, badOffset}) {
            FileImport invalid(selected("invalid.game", corrupt), "game");
            validate(invalid); REQUIRE(invalid.state() == FileImport::State::Failed);
        }
    }
    for (const auto& name : {"../escape.game", "con.game", "wrong.map"}) {
        FileImport invalid(selected(name, bytes), "game");
        validate(invalid); REQUIRE(invalid.state() == FileImport::State::Failed);
    }
    {
        std::string compressed;
        REQUIRE(gzipCompress(bytes, 6, compressed));
        FileImport operation(selected("Compressed.GAME.GZ", compressed), "game");
        validate(operation); operation.advance();
        REQUIRE(operation.state() == FileImport::State::Succeeded);
        REQUIRE(operation.path() == "games/Compressed.game.gz");
        REQUIRE(contents(directory / operation.path()) == compressed);
        FileImport rawSibling(selected("Compressed.game", bytes), "game");
        validate(rawSibling); rawSibling.advance();
        REQUIRE(rawSibling.state() == FileImport::State::Succeeded);
        REQUIRE(rawSibling.path() == "games/Compressed_(1).game");
        FileImport gzipSibling(selected("Compressed_(1).game.gz", compressed), "game");
        validate(gzipSibling); gzipSibling.advance();
        REQUIRE(gzipSibling.state() == FileImport::State::Succeeded);
        REQUIRE(gzipSibling.path() == "games/Compressed_(1)_(1).game.gz");
        for (const auto& corrupt : {compressed.substr(0, compressed.size()-1), compressed + "extra"}) {
            FileImport invalid(selected("invalid.game.gz", corrupt), "game");
            validate(invalid); REQUIRE(invalid.state() == FileImport::State::Failed);
        }
    }
    const auto original = directory / "games/Imported.game";
    { std::ofstream out(original, std::ios::binary); out << "previous save"; }
    struct ControlledPersistence : Persistence {
        std::shared_ptr<PersistenceState> result;
        explicit ControlledPersistence(std::shared_ptr<PersistenceState> result) : result(std::move(result)) {}
        PersistenceState state() const override { return *result; }
    };
    auto result = std::make_shared<PersistenceState>(PersistenceState::Pending);
    const auto persist = [result] { return std::make_unique<ControlledPersistence>(result); };
    std::string imported;
    {
        FileImport operation(selected("Imported.game", bytes), "game", persist);
        validate(operation);
        REQUIRE(operation.state() == FileImport::State::Persisting);
        imported = operation.path();
        REQUIRE(imported == "games/Imported_(1).game");
        REQUIRE((contents(directory / imported) == bytes && contents(original) == "previous save"));
        operation.advance(); REQUIRE(operation.state() == FileImport::State::Persisting);
        *result = PersistenceState::Failed;
        operation.advance(); REQUIRE(operation.canRetry());
        operation.retryPersistence();
        *result = PersistenceState::Succeeded;
        operation.advance(); REQUIRE(operation.state() == FileImport::State::Succeeded);
    }
    REQUIRE((contents(directory / imported) == bytes && contents(original) == "previous save"));
    {
        *result = PersistenceState::Failed;
        FileImport operation(selected("abandoned.game", bytes), "game", persist);
        validate(operation); operation.advance(); REQUIRE(operation.canRetry());
    }
    REQUIRE(!fs::exists(directory / "games/abandoned.game"));
    REQUIRE(getSyncRandState() == beforeRng);
    REQUIRE((contents(preferences) == "preserved preferences" && fs::last_write_time(preferences) == preferencesTime));
    globalContainer->settings.rememberUnit = remember;
    std::cout << "PASS imported save full validation, cancellation/RNG restoration, name collision, pending/failure/retry persistence and abandoned-file cleanup" << std::endl;
}

static void checkCampaignProgress(const fs::path& directory)
{
    Campaign base;
    base.setName("Progress fixture");
    base.setPlayerName("First player");
    CampaignMapEntry first("First", "campaigns/first.map"), second("Second", "campaigns/second.map");
    first.unlockMap(); second.lockMap(); second.getUnlockedByMaps().push_back("First");
    base.appendMap(first); base.appendMap(second);
    Campaign source = base;
    source.setCompleted("First"); source.setPlayerName("Restored player");
    const auto backup = source.exportProgress();
    const auto unchanged = base.exportProgress();
    for (size_t cut = 0; cut < backup.size(); ++cut) {
        REQUIRE(!base.importProgress({backup.begin(), backup.begin()+cut}));
        REQUIRE(base.exportProgress() == unchanged);
    }
    auto extra = backup; extra.push_back(0);
    REQUIRE(!base.importProgress(extra));
    REQUIRE(!base.importProgress(std::vector<unsigned char>(1024*1024+1)));
    auto invalidFlag = backup; invalidFlag.back() = 2;
    REQUIRE(!base.importProgress(invalidFlag));
    auto future = backup; future[7] = 2;
    REQUIRE(!base.importProgress(future));
    Campaign changed = base; changed.getMap(0).setMapFileName("../different.map");
    REQUIRE(!changed.importProgress(backup));
    changed = base; changed.getMap(1).getUnlockedByMaps().clear();
    REQUIRE(!changed.importProgress(backup));
    changed = base; changed.setName("Different campaign");
    REQUIRE(!changed.importProgress(backup));
    base.getMap(1).unlockMap(); base.getMap(1).setCompleted(true);
    REQUIRE(base.importProgress(backup));
    REQUIRE((base.getMap(0).isCompleted() && base.getMap(1).isCompleted() && base.getMap(1).isUnlocked()));
    REQUIRE(base.getPlayerName() == "Restored player");
    REQUIRE(base.save(true));
    const auto file = directory / "games/Progress_fixture.txt";
    const auto previous = contents(file);
    Campaign restored; REQUIRE(restored.load(file.string()));
    REQUIRE(restored.exportProgress() == base.exportProgress());
#ifndef WIN32
    const auto child = fork(); REQUIRE(child >= 0);
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit budget = {0,0};
        if (setrlimit(RLIMIT_FSIZE, &budget)) _exit(2);
        base.setPlayerName("Unwritten");
        _exit(base.save(true) ? 3 : 0);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    REQUIRE(contents(file) == previous);
#endif
    std::cout << "PASS campaign progress truncation/version/definition validation, monotonic merge, legacy text round trip and atomic failure preservation" << std::endl;
}

static void checkPreferences(const fs::path& directory)
{
    Settings settings;
    settings.setGraphicsDetail(false);
    const auto file = directory / "preferences-test.txt";
    REQUIRE(settings.save(file.string()));
    Settings loaded;
    loaded.load(file.string());
    REQUIRE(loaded.optionFlags == settings.optionFlags);
    KeyboardManager game(GameGUIShortcuts), editor(MapEditShortcuts);
    REQUIRE((game.saveKeyboardLayout() && editor.saveKeyboardLayout()));
    const auto gamePath = directory / GameGUIKeyActions::getConfigurationFile();
    const auto editorPath = directory / MapEditKeyActions::getConfigurationFile();
    const auto previous = contents(file), gameBytes = contents(gamePath), editorBytes = contents(editorPath);
    REQUIRE((!gameBytes.empty() && !editorBytes.empty()));
    const auto blocked = directory / "blocked-preferences.txt";
    fs::create_directory(blocked);
    REQUIRE((!settings.save(blocked.string()) && fs::is_directory(blocked)));
#ifndef WIN32
    const pid_t child = fork(); REQUIRE(child >= 0);
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit budget = {0, 0};
        if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
        settings.optionFlags = 0;
        const bool preferencesFailed = !settings.save(file.string());
        const bool gameFailed = !game.saveKeyboardLayout();
        const bool editorFailed = !editor.saveKeyboardLayout();
        _exit(preferencesFailed && gameFailed && editorFailed ? 0 : 3);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    REQUIRE((contents(file) == previous && contents(gamePath) == gameBytes && contents(editorPath) == editorBytes));
#endif
    std::cout << "PASS preference/keyboard writes round trip and preserve prior files on failure" << std::endl;
}
}

TEST_SUITE("SavegameSafety")
{
	TEST_CASE("autosave defaults; atomic and background writes; truncated and malformed loads; imports and campaign progress [save-format][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
		globals->runNoX = true;
		globals->load();
		REQUIRE(!globals->settings.autosaveGames);
		globals->settings.rememberUnit = false;
		checkPendingConstruction();
		for (bool text : {false,true})
			for (bool ai : {false,true}) checkRandomContinuation(text,ai);
		const fs::path directory = fs::absolute(globals->fileManager->getDir(0));
		checkAtomicWrites(*globals->fileManager, directory);
		checkGzipWrites(*globals->fileManager, directory);
		checkBackgroundWriter(*globals->fileManager, directory);
	    checkPreferences(directory);
		checkMapHeaders();
	    checkCampaignProgress(directory);
		for (bool file : {false, true})
		{
			const std::string payload("before\0after", 12);
			const std::string bytes = std::string("\0\0\0\14", 4) + payload
				+ std::string("\0\0\0\12", 4) + "next field";
			auto restored = input(bytes, file);
			REQUIRE(restored->readText("binary string") == payload);
			REQUIRE(restored->readText("following string") == "next field");
		}
		{
			GameGUI gui;
			auto map = Engine::loadMapHeader("maps/balanced.map");
			GameHeader header;
			header.setNumberOfPlayers(1);
			header.setRandomSeed(123456);
			header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
			REQUIRE(gui.loadFromHeaders(map, header, true, true));
			// Loading now leaves resource/area gradients unallocated. A simulation
			// tick must finish even before any unit has requested one of them.
			gui.game.map.syncStep(0);
			gui.game.map.syncStep(1);
			gui.localPlayer = gui.localTeamNo = 0;
			gui.adjustLocalTeam();
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS;
			// The engine's initial replay save sets the serialized map offset.
			{
				BinaryOutputStream initial(new MemoryStreamBackend());
				gui.save(&initial, "Auto save");
			}
			const fs::path save = directory / "games" / "Auto_save.game.gz";
			gui.syncStep();
			gui.waitForAutosave();
			REQUIRE(!fs::exists(save));
			globals->settings.autosaveGames = true;
			gui.syncStep();
			gui.waitForAutosave();
			const auto compressedBytes = contents(save);
			std::string bytes;
			REQUIRE(gzipDecompress(compressedBytes, bytes));
			{
				// Autosave hashes on the writer thread; a direct save hashes as it writes.
				const fs::path reference = directory / "games" / "reference.game";
				REQUIRE(globals->fileManager->writeAtomically(reference.string(), [&](OutputStream& stream) { gui.save(&stream, "Auto save"); }));
				REQUIRE(contents(reference) == bytes);
				fs::remove(reference);
			}
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
				REQUIRE(deferredHashed != inlineHashed);
				deferred.apply(deferredHashed);
				REQUIRE(deferredHashed == inlineHashed);
			}
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
				REQUIRE(deferredHashed != inlineHashed);
				deferred.apply(deferredHashed);
				REQUIRE(deferredHashed == inlineHashed);
			}

			{
				GameGUI restored;
				auto stream = input(bytes, true);
				REQUIRE(restored.load(stream.get()));
				std::vector<Uint32> before, after;
				gui.game.checkSum(&before, nullptr, nullptr);
				restored.game.checkSum(&after, nullptr, nullptr);
				REQUIRE(before.size() == after.size());
				// Component zero includes the map format version, upgraded on save.
				REQUIRE(std::equal(before.begin() + 1, before.end(), after.begin() + 1));
				REQUIRE(restored.game.stepCounter == gui.game.stepCounter);
			}
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + AUTOSAVE_INTERVAL_TICKS - 1;
			gui.syncStep();
			gui.waitForAutosave();
			REQUIRE(contents(save) == compressedBytes);
	        checkImports(bytes, directory);
	#ifndef WIN32
			const pid_t child = fork();
			REQUIRE(child >= 0);
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
			REQUIRE((waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0));
			REQUIRE(contents(save) == compressedBytes);
	#endif
			globals->settings.autosaveGames = false;
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + 2 * AUTOSAVE_INTERVAL_TICKS;
			gui.syncStep();
			gui.waitForAutosave();
			REQUIRE(contents(save) == compressedBytes);
			globals->settings.autosaveGames = true;
			auto stream = input(bytes, false);
			MapHeader savedHeader;
			REQUIRE(savedHeader.load(stream.get()));
			REQUIRE(std::any_of(savedHeader.getGameSHA1(), savedHeader.getGameSHA1() + SHA1_BYTE_LEN, [](Uint8 byte) { return byte != 0; }));
			{
				// A joining client keeps its own copy only when its header, SHA1 included, matches the host's.
				Engine engine;
				MapHeader host = savedHeader;
				REQUIRE(host.getFileName() == "games/Auto_save.game");
				REQUIRE(engine.haveMap(host));
				host.getGameSHA1()[0] ^= 1;
				REQUIRE(!engine.haveMap(host));
			}
			const size_t offset = savedHeader.getMapOffset();
			REQUIRE(bytes.substr(offset, 4) == "MapB");
			const auto mapBytes = bytes.substr(offset);
			const size_t cells = size_t(gui.game.map.getW()) * gui.game.map.getH();
			const size_t cellsStart = 12 + cells;
			const size_t cellsEnd = cellsStart + cells * 33;
			const size_t mapEnd = mapBytes.find("MapE");
			REQUIRE((mapEnd != std::string::npos && cellsEnd < mapEnd));
			const size_t cuts[] = {0, 1, 3, 4, 7, 11, 12, cellsStart - 1, cellsStart,
				cellsStart + 6, cellsStart + 7, cellsStart + 32, cellsEnd - 2171,
				cellsEnd - 1, cellsEnd, cellsEnd + 1, mapEnd, mapEnd + 3};
			int count = 0;
			for (bool file : {false, true})
				for (size_t cut : cuts)
				{
					Map loaded;
					auto truncated = input(mapBytes.substr(0, cut), file);
					REQUIRE(!loaded.load(truncated.get(), savedHeader, &gui.game));
					auto complete = input(mapBytes, file);
					REQUIRE(loaded.load(complete.get(), savedHeader, &gui.game));
					++count;
				}
			std::cout << "PASS " << count << " truncated map loads rejected, followed by successful reuse" << std::endl;
				const auto replaceSint32 = [](std::string& value, size_t offset, Uint32 replacement)
				{
					for (int byte = 0; byte < 4; ++byte)
						value[offset + byte] = char(replacement >> (24 - 8 * byte));
				};
				for (const auto [widthExponent, heightExponent] : {
					std::pair<Uint32, Uint32>{10, 9}, {9, 10}, {3, 9}, {9, 3},
					{0x7fffffffU, 9}, {9, 0x7fffffffU}})
				{
					std::string malformed = mapBytes;
					replaceSint32(malformed, 4, widthExponent);
					replaceSint32(malformed, 8, heightExponent);
					auto invalid = input(malformed, false);
					Map loaded;
					REQUIRE(!loaded.load(invalid.get(), savedHeader, &gui.game));
					REQUIRE((loaded.getW() == 0 && loaded.getH() == 0));
				}
				REQUIRE(Map::supportedDimensions(4, 4));
				REQUIRE(Map::supportedDimensions(9, 9));
				for (bool file : {false, true})
			{
				std::string malformed = mapBytes;
				malformed.replace(cellsEnd, 4, 4, char(0xFF));
				auto oversized = input(malformed, file);
				Map loaded;
				REQUIRE(!loaded.load(oversized.get(), savedHeader, &gui.game));
			}

		}
	}
}
