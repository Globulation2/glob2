// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Engine.h"
#include "Version.h"
#include "FileImport.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
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
	std::cout << "PASS atomic creation/replacement, temporary-name collision, seek, serialization/open/rename failure, temporary cleanup" << std::endl;
#ifndef WIN32
	std::cout << "PASS injected short write and buffered flush failure preserve previous bytes" << std::endl;
#endif
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
			assert(rejected && headerBytes(header) == previous);
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
			assert(!header.load(stream.get()) && headerBytes(header) == previous);
		}
		auto corrupt = bytes;
		corrupt[fields + 16] = 2;
		auto invalid = input(corrupt, file);
		assert(!header.load(invalid.get()) && headerBytes(header) == previous);
		auto valid = input(bytes, file);
		assert(header.load(valid.get()) && headerBytes(header) == bytes);
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
        assert(operation.state() != FileImport::State::Validating);
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
        assert(cancelled.state() == FileImport::State::Validating);
    }
    assert(getSyncRandState() == beforeRng && !fs::exists(directory / "games/cancel.game"));
    for (const auto& payload : {bytes.substr(0, 3), bytes.substr(0, bytes.size()-1), bytes + "extra"}) {
        FileImport invalid(selected("invalid.game", payload), "game");
        validate(invalid);
        assert(invalid.state() == FileImport::State::Failed && invalid.path().empty());
    }
    {
        auto stream = input(bytes, false);
        MapHeader header; assert(header.load(stream.get()));
        auto badCount = bytes;
        // GameHeader begins with latency (4), order rate (1), player count (4).
        std::fill_n(badCount.begin() + stream->getPosition() + 5, 4, char(0xff));
        auto badOffset = bytes;
        const auto offsetField = 4 + header.getMapName().size() + 12;
        const Uint32 offset = header.getMapOffset() + 1;
        for (int i = 0; i < 4; ++i) badOffset[offsetField+i] = char(offset >> (24-i*8));
        auto badPlayer = bytes;
        const auto player = badPlayer.find("PLYb"); assert(player != std::string::npos);
        badPlayer[player] = '!';
        for (const auto& corrupt : {badCount, badPlayer, badOffset}) {
            FileImport invalid(selected("invalid.game", corrupt), "game");
            validate(invalid); assert(invalid.state() == FileImport::State::Failed);
        }
    }
    for (const auto& name : {"../escape.game", "con.game", "wrong.map"}) {
        FileImport invalid(selected(name, bytes), "game");
        validate(invalid); assert(invalid.state() == FileImport::State::Failed);
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
        assert(operation.state() == FileImport::State::Persisting);
        imported = operation.path();
        assert(imported == "games/Imported_(1).game");
        assert(contents(directory / imported) == bytes && contents(original) == "previous save");
        operation.advance(); assert(operation.state() == FileImport::State::Persisting);
        *result = PersistenceState::Failed;
        operation.advance(); assert(operation.canRetry());
        operation.retryPersistence();
        *result = PersistenceState::Succeeded;
        operation.advance(); assert(operation.state() == FileImport::State::Succeeded);
    }
    assert(contents(directory / imported) == bytes && contents(original) == "previous save");
    {
        *result = PersistenceState::Failed;
        FileImport operation(selected("abandoned.game", bytes), "game", persist);
        validate(operation); operation.advance(); assert(operation.canRetry());
    }
    assert(!fs::exists(directory / "games/abandoned.game"));
    assert(getSyncRandState() == beforeRng);
    assert(contents(preferences) == "preserved preferences" && fs::last_write_time(preferences) == preferencesTime);
    globalContainer->settings.rememberUnit = remember;
    std::cout << "PASS imported save full validation, cancellation/RNG restoration, name collision, pending/failure/retry persistence and abandoned-file cleanup" << std::endl;
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
	const fs::path directory = fs::absolute(globals.fileManager->getDir(0));
	checkAtomicWrites(*globals.fileManager, directory);
	checkMapHeaders();
	for (bool file : {false, true})
	{
		const std::string payload("before\0after", 12);
		const std::string bytes = std::string("\0\0\0\14", 4) + payload
			+ std::string("\0\0\0\12", 4) + "next field";
		auto restored = input(bytes, file);
		assert(restored->readText("binary string") == payload);
		assert(restored->readText("following string") == "next field");
	}
	std::cout << "PASS embedded zero bytes preserved in binary strings" << std::endl;
	{
		GameGUI gui;
		auto map = Engine::loadMapHeader("maps/balanced.map");
		GameHeader header;
		header.setNumberOfPlayers(1);
		header.setRandomSeed(123456);
		header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
		assert(gui.loadFromHeaders(map, header, true, true));
		// Loading now leaves resource/area gradients unallocated. A simulation
		// tick must finish even before any unit has requested one of them.
		gui.game.map.syncStep(0);
		gui.game.map.syncStep(1);
		std::cout << "PASS ticks finish before lazy gradients are requested" << std::endl;
		gui.localPlayer = gui.localTeamNo = 0;
		gui.adjustLocalTeam();
		gui.game.stepCounter = 79;
		// The engine's initial replay save sets the serialized map offset.
		{
			BinaryOutputStream initial(new MemoryStreamBackend());
			gui.save(&initial, "Auto save");
		}
		gui.syncStep();
		const fs::path save = directory / "games" / "Auto_save.game";
		const auto bytes = contents(save);
		{
			auto *backend = new MemoryStreamBackend();
			BinaryOutputStream reference(backend);
			gui.save(&reference, "Auto save");
			const std::string expected(backend->getBuffer(), backend->getPosition());
			assert(bytes == expected);
		}
		std::cout << "PASS atomic autosave bytes match direct serialization" << std::endl;

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
        checkImports(bytes, directory);
#ifndef WIN32
		const pid_t child = fork();
		assert(child >= 0);
		if (child == 0)
		{
			std::signal(SIGXFSZ, SIG_IGN);
			struct rlimit budget = {0, 0};
			if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
			gui.game.stepCounter = 335;
			gui.syncStep();
			_exit(0);
		}
		int status = 0;
		assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
		assert(contents(save) == bytes);
		std::cout << "PASS failed production autosave preserves the previous complete game" << std::endl;
#endif
		auto stream = input(bytes, false);
		MapHeader savedHeader;
		assert(savedHeader.load(stream.get()));
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
