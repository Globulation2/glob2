// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapCommand.h"
#include "LobbyMapPreview.h"
#include <SDL_image.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifndef PRIMARY_FONT
#define PRIMARY_FONT "sans.ttf"
#endif
#include "Game.h"
#include "GenerationService.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Race.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <StreamBackend.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

namespace
{
using MapSettings = std::map<std::string, std::string>;
std::string trim(const std::string &s)
{
	const auto first = s.find_first_not_of(" \t\r\n");
	return first == std::string::npos ? ""
									  : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
void setting(MapSettings &settings, const std::string &text)
{
	const auto eq = text.find('=');
	if (eq == std::string::npos || trim(text.substr(0, eq)).empty() ||
		trim(text.substr(eq + 1)).empty())
		throw std::runtime_error("Expected key=value: " + text);
	settings[trim(text.substr(0, eq))] = trim(text.substr(eq + 1));
}
std::uint32_t number(const std::string &text)
{
	if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
		throw std::runtime_error("Expected an unsigned decimal integer: " + text);
	const auto n = std::stoull(text);
	if (n > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("Integer exceeds 4294967295: " + text);
	return static_cast<std::uint32_t>(n);
}
int method(const std::string &name)
{
	const auto &registry = GeneratorRegistry::builtins();
	for (int id : registry.methods())
		if (name == registry.at(id).id || name == std::to_string(id))
			return id;
	throw std::runtime_error("Unknown generator: " + name + "; use --list-map-generators");
}
std::vector<GeneratorControl> controls(int id)
{
	auto all = GenerationRequest::sharedControls();
	const auto &specific = GenerationRequest::controls(id);
	all.insert(all.end(), specific.begin(), specific.end());
	return all;
}
void configure(GenerationRequest &request, const MapSettings &settings)
{
	const auto all = controls(request.method);
	for (const auto &entry : settings)
	{
		if (entry.first == "seed")
		{
			request.seed = number(entry.second);
			continue;
		}
		auto c = std::find_if(all.begin(), all.end(),
							  [&](const auto &c) { return c.id == entry.first; });
		if (c == all.end())
			throw std::runtime_error("Unknown setting: " + entry.first);
		const auto n = number(entry.second);
		const auto values = c->values();
		const auto value = std::find_if(values.begin(), values.end(), [&](int v)
										{ return n == static_cast<unsigned>(c->displayValue(v)); });
		if (value == values.end())
			throw std::runtime_error("Invalid value for " + entry.first + ": " + entry.second +
									 "; use --list-map-generators " +
									 GeneratorRegistry::builtins().at(request.method).id);
		c->set(request, *value);
	}
	const auto error =
		validateGenerationRequest(request, GeneratorRegistry::builtins().at(request.method));
	if (!error.empty())
		throw std::runtime_error(error);
}
void catalog(const std::string &name)
{
	const auto &registry = GeneratorRegistry::builtins();
	const auto methods = name.empty() ? registry.methods() : std::vector<int>{method(name)};
	for (int id : methods)
	{
		const auto &d = registry.at(id);
		std::cout << d.id << " (id " << id << ", revision " << d.revision
				  << (d.editorOnly ? ", editor only" : "") << ")\n";
		if (name.empty())
			continue;
		for (const auto &c : controls(id))
		{
			std::cout << "  " << c.id << "=" << c.displayValue(c.defaultValue) << "  values:";
			for (int value : c.values())
			{
				std::cout << " " << c.displayValue(value);
				if (const char *label = c.valueLabel(value))
					std::cout << "(" << label << ")";
			}
			std::cout << "\n";
		}
	}
}
void parentDirectory(const std::string &path)
{
	const auto parent = std::filesystem::path(path).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent);
}
bool samePath(const std::string &a, const std::string &b)
{
	return !a.empty() && !b.empty() &&
		   ((std::filesystem::exists(a) && std::filesystem::exists(b) &&
			 std::filesystem::equivalent(a, b)) ||
			std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b));
}
void saveMap(Game &game, const std::string &path, const std::string &name)
{
	auto *backend = new GAGCore::MemoryStreamBackend();
	GAGCore::BinaryOutputStream stream(backend);
	game.save(&stream, true, name);
	stream.flush();
	backend->seekFromEnd(0);
	parentDirectory(path);
	std::ofstream out(path, std::ios::binary);
	out.write(backend->getBuffer(), backend->getPosition());
	out.close();
	if (!out)
		throw std::runtime_error("Cannot write map: " + path);
}
// Use exactly the thumbnail and marker drawing path shared by the lobby and landscape picker.
// Only the destination surface and PNG encoding belong to the command-line tool.
void exportPreview(const Game &game, const std::string &path, int size)
{
	const int extent = std::max(game.map.getW(), game.map.getH());
	const int width = std::max(1, size * game.map.getW() / extent);
	const int height = std::max(1, size * game.map.getH() / extent);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	SDL_setenv("GLOB2_UI_SCALE", "1", 1);
	auto *target = Toolkit::initGraphic(std::max(640, width), std::max(480, height),
										GraphicContext::USEGPU, "Map preview", "glob2");
	globalContainer->gfx = target;
	if (!(target->getOptionFlags() & GraphicContext::USEGPU))
		throw std::runtime_error("Map previews require the existing OpenGL renderer");
	SDL_HideWindow(SDL_GL_GetCurrentWindow());
	const std::string font = std::string("data/fonts/") + PRIMARY_FONT;
	Toolkit::loadFont(font, 13, "standard");
	MapThumbnail thumbnail;
	thumbnail.loadFromMap(game.map);
	DrawableSurface source(MapPreview::PreviewSize, MapPreview::PreviewSize);
	thumbnail.loadIntoSurface(&source);
	std::vector<MapStart> starts;
	for (int i = 0; i < game.teamsCount(); ++i)
		starts.push_back(
			{game.teams[i]->startPosX, game.teams[i]->startPosY, game.teams[i]->color});
	drawMapThumbnail(target, {0, 0, width, height}, &source, game.map.getW(), game.map.getH(),
					 starts);
	// Use the normal screenshot readback, then crop the context's minimum-size padding.
	DrawableSurface capture(target->getW(), target->getH());
	capture.drawSurface(0, 0, target);
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> output(
		SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32),
		SDL_FreeSurface);
	SDL_Rect crop{0, 0, width, height};
	if (!output || SDL_BlitSurface(capture.getSDLSurface(), &crop, output.get(), nullptr) != 0 ||
		IMG_SavePNG(output.get(), path.c_str()) != 0)
		throw std::runtime_error("Cannot write PNG " + path + ": " + SDL_GetError());
}
} // namespace

bool isMapCommand(const char *arg)
{
	const std::string s = arg;
	return s == "--generate-map" || s == "--preview-map" || s == "--list-map-generators";
}
void printMapCommandHelp()
{
	std::cout
		<< "Map launch modes (put the mode first; PNG export requires OpenGL):\n"
		   "  --generate-map <generator> [--output file.map] [--preview file.png]\n"
		   "    [--config file] [--set key=value ...] [--seed N]\n"
		   "    [--width tiles] [--height tiles] [--teams N] [--workers N]\n"
		   "  --preview-map <file.map|file.game> --output file.png\n"
		   "  --list-map-generators [generator]  List IDs, or settings and allowed values\n"
		   "Preview size: --preview-size 128..4096 (longest side in pixels, default 512).\n"
		   "Asset search: -d directory (repeatable).\n"
		   "Generation requires --output and/or --preview. Defaults: seed=1, registered settings.\n"
		   "Config: key=value lines; blank lines and # comments allowed. CLI overrides config.\n"
		   "Width/height are tile counts, not exponents. See docs/map-generators/CLI.md.\n";
}
int runMapCommand(int argc, char **argv)
{
	try
	{
		const std::string mode = argv[1];
		if (argc == 3 && std::string(argv[2]) == "--help")
		{
			printMapCommandHelp();
			return 0;
		}
		if (mode == "--list-map-generators")
		{
			if (argc > 3)
				throw std::runtime_error("Expected at most one generator ID");
			catalog(argc == 3 ? argv[2] : "");
			return 0;
		}
		if (argc < 3 || std::string(argv[2]).rfind("--", 0) == 0)
			throw std::runtime_error("Missing generator or input path; use " + mode + " --help");
		const bool generate = mode == "--generate-map";
		std::string output, preview, config;
		MapSettings overrides, settings;
		std::vector<std::string> directories;
		int previewSize = 512;
		bool sizeSpecified = false;
		for (int i = 3; i < argc; ++i)
		{
			const std::string arg = argv[i];
			if (arg == "--help")
			{
				printMapCommandHelp();
				return 0;
			}
			if (i + 1 == argc)
				throw std::runtime_error("Missing value for " + arg);
			const std::string value = argv[++i];
			if (value.empty() || value.rfind("--", 0) == 0)
				throw std::runtime_error("Missing value for " + arg);
			if (arg == "--output")
				output = value;
			else if (arg == "--preview" && generate)
				preview = value;
			else if (arg == "--config" && generate)
				config = value;
			else if (arg == "--set" && generate)
				setting(overrides, value);
			else if (generate && (arg == "--seed" || arg == "--width" || arg == "--height" ||
								  arg == "--teams" || arg == "--workers"))
				overrides[arg.substr(2)] = value;
			else if (arg == "--preview-size")
			{
				const auto n = number(value);
				if (n < 128 || n > 4096)
					throw std::runtime_error("Preview size must be 128..4096");
				previewSize = int(n);
				sizeSpecified = true;
			}
			else if (arg == "-d")
				directories.push_back(value);
			else
				throw std::runtime_error("Unknown option for " + mode + ": " + arg);
		}
		if (!generate)
			preview = output;
		if (output.empty() && preview.empty())
			throw std::runtime_error("Specify an output path; use " + mode + " --help");
		if (sizeSpecified && preview.empty())
			throw std::runtime_error("Preview size requires --preview");
		if ((generate && samePath(output, preview)) || (!generate && samePath(argv[2], preview)) ||
			samePath(config, output) || samePath(config, preview))
			throw std::runtime_error("Input, config, map and PNG paths must be distinct");
		GenerationRequest request;
		if (generate)
		{
			request.setMethodDefaults(method(argv[2]));
			request.seed = 1;
			if (!config.empty())
			{
				std::ifstream in(config);
				if (!in)
					throw std::runtime_error("Cannot read config: " + config);
				std::string line;
				int lineNumber = 0;
				while (std::getline(in, line))
				{
					++lineNumber;
					line = trim(line.substr(0, line.find('#')));
					if (line.empty())
						continue;
					try
					{
						setting(settings, line);
					}
					catch (const std::exception &e)
					{
						throw std::runtime_error(config + ":" + std::to_string(lineNumber) + ": " +
												 e.what());
					}
				}
				if (in.bad())
					throw std::runtime_error("Cannot read config: " + config);
			}
			for (const auto &entry : overrides)
				settings[entry.first] = entry.second;
			configure(request, settings);
		}
		struct ClearGlobal
		{
			~ClearGlobal() { globalContainer = nullptr; }
		} clear;
		GlobalContainer globals;
		globalContainer = &globals;
		globals.runNoX = true;
		globals.settings.rememberUnit = false;
		for (const auto &directory : directories)
			globals.fileManager->addDir(directory);
		globals.buildingsTypes.init();
		IntBuildingType::init();
		Race::loadDefault();
		Game game(nullptr);
		if (generate)
		{
			const auto result = GenerationService().generate(game, request);
			if (!result)
				throw std::runtime_error(result.diagnostic());
			std::cout << result.diagnostic() << "\n";
		}
		else
		{
			// Explicit paths use the same loader as normal games, without stepping simulation.
			GAGCore::BinaryInputStream stream(
				new GAGCore::FileStreamBackend(std::fopen(argv[2], "rb")));
			if (stream.isEndOfStream() || !game.load(&stream))
				throw std::runtime_error("Cannot load map/save: " + std::string(argv[2]));
		}
		if (!preview.empty())
		{
			parentDirectory(preview);
			exportPreview(game, preview, previewSize);
			std::cout << "Preview: " << preview << " (" << game.map.getW() << "x" << game.map.getH()
					  << " tiles)\n";
		}
		if (generate && !output.empty())
			saveMap(game, output, std::filesystem::path(output).stem().string());
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Map command: " << e.what() << "\n";
		return 1;
	}
}
