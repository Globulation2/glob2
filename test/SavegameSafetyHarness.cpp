// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Engine.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifndef WIN32
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
	const auto write = [](OutputStream& stream) { stream.write("complete", 8, "data"); };
	assert(files.writeAtomically(path, write));
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
	std::cout << "PASS atomic creation/replacement, seek, serialization/open/rename failure, temporary cleanup" << std::endl;
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
