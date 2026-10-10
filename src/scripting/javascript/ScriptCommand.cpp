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
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace Script
{
std::string readSource(const std::string &path)
{
	std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
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
int runScriptCommand(const Cli::Request &request)
{
	const auto &command = request.command;
	if (command == "ai check" && request.get("--format") == "json")
	{
		nlohmann::json report{{"valid", false}};
		std::string stage = "syntax";
		try
		{
			const auto source = Script::readSource(request.positionals.at(0));
			auto metadata = Script::inspectAI(source, &stage);
			report["valid"] = true;
			report["metadata"] = {{"apiVersion", metadata.apiVersion},
								  {"name", metadata.name},
								  {"description", metadata.description},
								  {"version", metadata.version},
								  {"author", metadata.author}};
		}
		catch (const std::exception &e)
		{
			report["failedCheck"] = stage;
			report["message"] = std::string(e.what()).substr(0, 2000);
		}
		std::cout << report.dump() << '\n';
		return report["valid"] == true ? 0 : 2;
	}
	if (command != "script check" && command != "ai check" && command != "script attach")
		return -1;
	try
	{
		if (command == "ai check")
		{
			auto metadata = Script::inspectAI(Script::readSource(request.positionals.at(0)));
			std::cout << "JavaScript AI profile " << metadata.apiVersion << ": " << metadata.name
					  << " — startup, callback and persistent globals validated\n";
			return 0;
		}
		if (command == "script check")
		{
			Script::readSource(request.positionals.at(0));
			std::cout << "JavaScript profile 1: source compiled successfully\n";
			return 0;
		}
		auto source = Script::readSource(request.positionals.at(1));
		auto output = glob2GzipWritePath(request.positionals.at(2));
		if (std::filesystem::exists(output) || std::filesystem::exists(request.positionals.at(2)))
			throw std::invalid_argument("Output already exists");
		GlobalContainer globals("glob2-script-tools");
		globalContainer = &globals;
		globals.runNoX = true;
		for (const auto &directory : request.all("--data-dir"))
			globals.fileManager->addDir(directory);
		globals.load();
		// Loading a map for conversion must not persist GUI preferences on destruction.
		GameGUI gui(false);
		GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
			*Toolkit::getFileManager(), request.positionals.at(0)));
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
			gui.game.save(&stream, true,
						  std::filesystem::path(request.positionals.at(2)).stem().string());
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
		return dynamic_cast<const std::invalid_argument *>(&ex) ? 2 : 3;
	}
}
