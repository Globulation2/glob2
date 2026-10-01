// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptCommand.h"
#include "ScriptRuntime.h"
#include "GlobalContainer.h"
#include "GameGUI.h"
#include <FileManager.h>
#include <Toolkit.h>
#include "Game.h"
#include <BinaryStream.h>
#include <GzipUtil.h>
#include <StreamBackend.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace Script
{
std::string readSource(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw std::invalid_argument("Cannot open script: " + path);
	std::string source;
	char buffer[4096];
	while (file.read(buffer, sizeof(buffer)) || file.gcount())
	{
		source.append(buffer, file.gcount());
		if (source.size() > SourceLimit)
			throw std::invalid_argument("Script exceeds source limit");
	}
	if (!file.eof())
		throw std::invalid_argument("Cannot read script: " + path);
	makeRuntime()->validate(source);
	return source;
}
} // namespace Script
int runScriptCommand(int argc, char **argv)
{
	if (argc < 2)
		return -1;
	std::string command = argv[1];
	if (command != "--check-script" && command != "--attach-map-script")
		return -1;
	try
	{
		if (command == "--check-script")
		{
			if (argc != 3)
				throw std::invalid_argument("--check-script source.js");
			Script::readSource(argv[2]);
			std::cout << "JavaScript profile 1: source compiled successfully\n";
			return 0;
		}
		if (argc != 5)
			throw std::invalid_argument("--attach-map-script input.map source.js output.map");
		auto source = Script::readSource(argv[3]);
		auto output = glob2GzipWritePath(argv[4]);
		if (std::filesystem::exists(output) || std::filesystem::exists(argv[4]))
			throw std::invalid_argument("Output already exists");
		GlobalContainer globals("glob2-script-tools");
		globalContainer = &globals;
		globals.runNoX = true;
		globals.load();
		GameGUI gui;
		GAGCore::BinaryInputStream input(
			glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), argv[2]));
		if (!gui.load(&input))
			throw std::invalid_argument("Cannot load input map");
		auto &script = gui.game.mapscript;
		script.setMapScriptMode(MapScript::JavaScript);
		script.setMapScript(source);
		if (!script.compileCode())
			throw std::invalid_argument(script.getError().getMessage());
		auto *memory = new GAGCore::MemoryStreamBackend();
		std::string bytes;
		{
			GAGCore::BinaryOutputStream stream(memory);
			gui.game.save(&stream, true, std::filesystem::path(argv[4]).stem().string());
			bytes = memory->takeContents();
		}
		if (!GAGCore::writeGzipAtomicToPath(output, bytes))
			throw std::runtime_error("Cannot write scripted map");
		std::cout << "Wrote JavaScript map: " << output << '\n';
		return 0;
	}
	catch (const std::exception &ex)
	{
		std::cerr << ex.what() << '\n';
		return 2;
	}
}
