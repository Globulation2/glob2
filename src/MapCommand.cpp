// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapCommand.h"
#include "MapReport.h"
#include "GUIMapPreview.h"
#include "Glob2Style.h"
#include <SDL_image.h>
#include <Toolkit.h>
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
	// Relationship validation runs in GenerationService so failures retain JSON diagnostics.
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
void writeJsonReport(const std::string &path, const std::string &report)
{
	parentDirectory(path);
	std::ofstream file(path, std::ios::binary);
	file << report;
	file.close();
	if (!file)
		throw std::runtime_error("Cannot write JSON: " + path);
	std::cout << "Map report: " << path << "\n";
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
// The same widget paints CLI images and in-game previews. Only its static layout and
// destination differ: exports have no interaction or transition animation.
class ExportMapPreview : public MapPreview
{
  public:
	ExportMapPreview(int width, int height)
		: MapPreview(0, 0, ALIGN_LEFT, ALIGN_TOP, "", "standard")
	{
		animateChanges = false;
		setDimensions(width, height);
	}
};
void exportPreview(const Game &game, const std::string &path, int size, int scale)
{
	MapThumbnail thumbnail;
	thumbnail.loadFromMap(game.map);
	if (!thumbnail.isLoaded())
		throw std::runtime_error("Cannot create map thumbnail");
	const int extent = std::max(game.map.getW(), game.map.getH());
	const int width =
		size ? std::max(1, size * game.map.getW() / extent) : thumbnail.pixels()->width * scale;
	const int height =
		size ? std::max(1, size * game.map.getH() / extent) : thumbnail.pixels()->height * scale;
	// GAG surfaces need a context for their pixel format. SDL's dummy driver keeps
	// this small software context independent of the desktop and export dimensions.
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	SDL_setenv("GLOB2_UI_SCALE", "1", 1);
	globalContainer->gfx = Toolkit::initGraphic(640, 480, 0, "Map preview", "glob2");
	const std::string font = std::string("data/fonts/") + PRIMARY_FONT;
	Toolkit::loadFont(font, 13, "standard");
	DrawableSurface target(width, height);
	Glob2Style style;
	struct RestoreStyle
	{
		Style *previous = Style::style;
		~RestoreStyle() { Style::style = previous; }
	} restore;
	Style::style = &style;
	struct ExportScreen : Screen
	{
		explicit ExportScreen(DrawableSurface *target) { gfx = target; }
		void onAction(Widget *, Action, int, int) override {}
	} screen(&target);
	auto *preview = new ExportMapPreview(width, height);
	screen.addWidget(preview);
	screen.dispatchInit();
	preview->setMapThumbnail(thumbnail);
	for (int i = 0; i < game.teamsCount(); ++i)
		preview->starts.push_back(
			{game.teams[i]->startPosX, game.teams[i]->startPosY, game.teams[i]->color});
	preview->paint();
	if (IMG_SavePNG(target.getSDLSurface(), path.c_str()) != 0)
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
		<< "Map launch modes (put the mode first; no display required):\n"
		   "  --generate-map <generator> [--output file.map] [--preview file.png] [--json "
		   "report.json]\n"
		   "    [--config file] [--set key=value ...] [--seed N]\n"
		   "    [--width tiles] [--height tiles] [--teams N] [--workers N]\n"
		   "  --preview-map <file.map|file.game> [--output file.png] [--json report.json]\n"
		   "  --list-map-generators [generator]  List IDs, or settings and allowed values\n"
		   "Preview scale: --preview-scale 2|4|8 (default 2, relative to retained thumbnail "
		   "pixels).\n"
		   "Or --preview-size 128..4096 (explicit longest side; cannot combine with scale).\n"
		   "Asset search: -d directory (repeatable).\n"
		   "Supply at least one output: --output, --preview, or --json. Defaults: seed=1, "
		   "registered settings.\n"
		   "Config: key=value lines; blank lines and # comments allowed. CLI overrides config.\n"
		   "Width/height are tile counts, not exponents. See docs/map-generators/CLI.md.\n"
		   "JSON fields, units and formulas: docs/map-generators/REPORT.md.\n";
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
		std::string output, preview, config, json;
		MapSettings overrides, settings;
		std::vector<std::string> directories;
		int previewSize = 0, previewScale = 2;
		bool sizeSpecified = false, scaleSpecified = false;
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
			else if (arg == "--json")
				json = value;
			else if (arg == "--preview" && generate)
				preview = value;
			else if (arg == "--config" && generate)
				config = value;
			else if (arg == "--set" && generate)
				setting(overrides, value);
			else if (generate && (arg == "--seed" || arg == "--width" || arg == "--height" ||
								  arg == "--teams" || arg == "--workers"))
				overrides[arg.substr(2)] = value;
			else if (arg == "--preview-scale")
			{
				const auto n = number(value);
				if (n != 2 && n != 4 && n != 8)
					throw std::runtime_error("Preview scale must be 2, 4 or 8");
				previewScale = int(n);
				scaleSpecified = true;
			}
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
		if (output.empty() && preview.empty() && json.empty())
			throw std::runtime_error("Specify an output path; use " + mode + " --help");
		if ((sizeSpecified || scaleSpecified) && preview.empty())
			throw std::runtime_error("Preview size/scale requires a PNG output");
		if (sizeSpecified && scaleSpecified)
			throw std::runtime_error("Choose --preview-size or --preview-scale, not both");
		if ((generate && samePath(output, preview)) || (!generate && samePath(argv[2], preview)) ||
			samePath(config, output) || samePath(config, preview) || samePath(json, config) ||
			samePath(json, output) || samePath(json, preview) ||
			(!generate && samePath(json, argv[2])))
			throw std::runtime_error("Input, config, map, PNG and JSON paths must be distinct");
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
		GenerationResult result;
		if (generate)
		{
			result = GenerationService().generate(game, request, !json.empty());
			if (!result)
			{
				if (!json.empty())
					writeJsonReport(json, describeGenerationFailure(request, result));
				throw std::runtime_error(result.diagnostic());
			}
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
		// Analyze the original snapshot before any serializer updates its header metadata.
		const std::string report = json.empty() ? ""
												: describeMap(game, generate ? &request : nullptr,
															  generate ? &result : nullptr);
		if (!preview.empty())
		{
			parentDirectory(preview);
			exportPreview(game, preview, previewSize, previewScale);
			std::cout << "Preview: " << preview << " (" << game.map.getW() << "x" << game.map.getH()
					  << " tiles)\n";
		}
		if (generate && !output.empty())
			saveMap(game, output, std::filesystem::path(output).stem().string());
		if (!json.empty())
			writeJsonReport(json, report);
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Map command: " << e.what() << "\n";
		return 1;
	}
}
