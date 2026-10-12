// SPDX-License-Identifier: GPL-3.0-or-later
#include <Environment.h>
#include "MapCommand.h"
#include "MapAssetBundle.h"
#include "BrushSwatches.h"
#include "online/Sha256.h"
#include <nlohmann/json.hpp>
#include "MapReport.h"
#include "MapImage.h"
#include "GUIMapPreview.h"
#include "Glob2Style.h"
#include <SDL3_image/SDL_image.h>
#include <Toolkit.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifndef PRIMARY_FONT
#define PRIMARY_FONT "sans.ttf"
#endif
#include "Game.h"
#include "MapRender.h"
#include "render/scene/SceneExtract.h"
#include "GenerationService.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
#include "GeneratorPackage.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Race.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <GzipUtil.h>
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
		throw std::invalid_argument("Expected key=value: " + text);
	settings[trim(text.substr(0, eq))] = trim(text.substr(eq + 1));
}
std::uint32_t number(const std::string &text)
{
	if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
		throw std::invalid_argument("Expected an unsigned decimal integer: " + text);
	const auto n = std::stoull(text);
	if (n > std::numeric_limits<std::uint32_t>::max())
		throw std::invalid_argument("Integer exceeds 4294967295: " + text);
	return static_cast<std::uint32_t>(n);
}
int method(const std::string &name)
{
	const auto &registry = GeneratorRegistry::active();
	for (int id : registry.methods())
		if (name == registry.at(id).id || name == std::to_string(id))
			return id;
	throw std::invalid_argument("Unknown generator: " + name + "; use glob2 map generators");
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
			throw std::invalid_argument("Unknown setting: " + entry.first);
		const auto n = number(entry.second);
		const auto values = c->values();
		const auto value = std::find_if(values.begin(), values.end(), [&](int v)
										{ return n == static_cast<unsigned>(c->displayValue(v)); });
		if (value == values.end())
			throw std::invalid_argument("Invalid value for " + entry.first + ": " + entry.second +
									 "; use glob2 map generators " +
									 GeneratorRegistry::active().at(request.method).id);
		c->set(request, *value);
	}
	// Relationship validation runs in GenerationService so failures retain JSON diagnostics.
}
void catalog(const std::string &name, bool json = false)
{
	const auto &registry = GeneratorRegistry::active();
	const auto methods = name.empty() ? registry.methods() : std::vector<int>{method(name)};
    if(json) {
        nlohmann::json list=nlohmann::json::array();
        for(const auto id:methods) {
            const auto &d=registry.at(id);
            nlohmann::json row={{"id",d.id},{"method",id},{"revision",d.revision},{"editorOnly",d.editorOnly},{"controls",nlohmann::json::array()}};
            for(const auto &c:controls(id)) {
                std::vector<int> values;for(int v:c.values())values.push_back(c.displayValue(v));
                row["controls"].push_back({{"id",c.id},{"default",c.displayValue(c.defaultValue)},{"values",values}});
            }
            list.push_back(row);
        }
        std::cout<<nlohmann::json({{"schema_version",1},{"generators",list}}).dump()<<'\n';return;
    }
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
			std::cout << "  search:";
			for (int value : c.searchValues())
				std::cout << " " << c.displayValue(value);
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

bool endsWithGz(const std::string &path)
{
	static const std::string suffix = ".gz";
	return path.size() >= suffix.size() &&
		   path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
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
	std::string contents;
	{
		GAGCore::BinaryOutputStream stream(backend);
		game.save(&stream, true, name);
		contents = backend->takeContents();
	}
	const std::string gzipPath = glob2GzipWritePath(path);
	parentDirectory(gzipPath);
	if (!GAGCore::writeGzipAtomicToPath(gzipPath, contents))
		throw std::runtime_error("Cannot write map: " + gzipPath);
	std::cout << "Map: " << gzipPath << "\n";
}
// The same widget paints CLI images and in-game previews. Only its static layout and
// destination differ: exports have no interaction or transition animation.
class ExportMapPreview : public MapPreview
{
  public:
	ExportMapPreview(int width, int height)
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
	GAGCore::setProcessEnvironment("SDL_VIDEODRIVER", "dummy", 1);
	GAGCore::setProcessEnvironment("SDL_AUDIODRIVER", "dummy", 1);
	GAGCore::setProcessEnvironment("GLOB2_UI_SCALE", "1", 1);
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
	ExportMapPreview preview(width, height);
	preview.setMapThumbnail(thumbnail);
	for (int i = 0; i < game.teamsCount(); ++i)
		preview.starts.push_back(
			{game.teams[i]->startPosX, game.teams[i]->startPosY, game.teams[i]->color});
	preview.paint(&target);
	if (!IMG_SavePNG(target.getSDLSurface(), path.c_str()))
		throw std::runtime_error("Cannot write PNG " + path + ": " + SDL_GetError());
}

} // namespace

int runMapCommand(const Cli::Request &cli)
{
	try
	{
		const auto &mode = cli.command;
		const auto source = cli.positionals.empty() ? std::string() : cli.positionals.at(0);
		if (mode == "map inspect-package")
		{
			if (samePath(source, cli.get("--output")) ||
				samePath(source, cli.get("--report-file")) ||
				samePath(cli.get("--output"), cli.get("--report-file")))
				throw std::invalid_argument("Input and output paths must be distinct");
			const auto package = MapGeneration::JavaScript::Package::load(source);
			auto metadata = nlohmann::json::parse(package->canonical).at("manifest");
			metadata["description"] = package->description;
			metadata["editorOnly"] = package->editorOnly;
			metadata["controls"] = nlohmann::json::array();
			for (const auto &c : package->controls)
			{
				nlohmann::json control{{"id", c.id},
									   {"label", c.label},
									   {"group", c.group == ControlGroup::Terrain     ? "terrain"
												 : c.group == ControlGroup::Resources ? "resources"
																					  : "layout"},
									   {"kind", c.isChoice()   ? "choice"
												: c.isToggle() ? "toggle"
															   : "range"},
									   {"minimum", c.minimum},
									   {"maximum", c.maximum},
									   {"step", c.step},
									   {"default", c.defaultValue},
									   {"values", c.values()},
									   {"powerOfTwo", c.powerOfTwo}};
				if (c.isChoice())
				{
					control["choices"] = nlohmann::json::array();
					for (const auto *label : c.valueLabels)
						control["choices"].push_back(label);
				}
				metadata["controls"].push_back(std::move(control));
			}
			metadata["toolkitVersion"] = 1;
			metadata["packageHash"] = package->hash;
			for (const auto &path : {cli.get("--output"), cli.get("--report-file")})
				parentDirectory(path);
			std::ofstream canonical(cli.get("--output"), std::ios::binary),
				report(cli.get("--report-file"), std::ios::binary);
			canonical << package->canonical;
			report << metadata.dump();
			canonical.close();
			report.close();
			if (!canonical || !report)
				throw std::runtime_error("Cannot write generator inspection");
			return 0;
		}
		if (mode == "map validate-set")
		{
			const auto reportPath = cli.get("--report-file");
			const auto previewPath = cli.get("--preview");
			const bool gallery = cli.get("--gallery") == "1";
			const auto phase = unsigned(std::stoul(cli.get("--phase")));
			const auto variation = unsigned(std::stoul(cli.get("--variation")));
			if (reportPath.empty() || samePath(source, reportPath) ||
				samePath(source, previewPath) || samePath(reportPath, previewPath))
				throw std::invalid_argument("Set input and output paths must be distinct");
			std::ifstream input(source, std::ios::binary | std::ios::ate);
			if (!input || input.tellg() < 0 ||
				input.tellg() > std::streamoff(MapAssetBundle::MaximumBytes))
				throw std::runtime_error("Cannot read set or set exceeds 16 MiB");
			std::string bytes(std::size_t(input.tellg()), '\0');
			input.seekg(0);
			if (!input.read(bytes.data(), bytes.size()))
				throw std::runtime_error("Cannot read set");
			nlohmann::json report{{"hash", Online::Sha256::hex(bytes)},
								  {"suite", 1},
								  {"valid", false},
								  {"minVersionMinor", 144},
								  {"terrainCount", 0},
								  {"resourceCount", 0}};
			struct Reset
			{
				~Reset() { globalContainer = nullptr; }
			} reset;
			try
			{
				GlobalContainer globals;
				globalContainer = &globals;
				globals.runNoX = true;
				Map map;
				map.setSize(5, 5, GRASS);
				map.importSet(bytes);
				const auto package = nlohmann::json::parse(bytes);
				report["terrainCount"] = package.at("terrains").size();
				report["resourceCount"] = package.at("resources").size();
				if (!previewPath.empty())
				{
					GAGCore::setProcessEnvironment("SDL_VIDEODRIVER", "dummy", 1);
					GAGCore::setProcessEnvironment("SDL_AUDIODRIVER", "dummy", 1);
					GAGCore::setProcessEnvironment("GLOB2_UI_SCALE", "1", 1);
					globals.loadOffscreenGraphics();
					BrushSwatches swatches;
					swatches.bind(map.frozenTerrainRegistry(), map.frozenResourceRegistry(),
								  map.frozenAssetBundle());
					const int count = std::min(
						64, int(package.at("terrains").size() + package.at("resources").size()));
					GAGCore::DrawableSurface preview(gallery ? 768 : 512,
													 gallery ? std::max(1, count) * 192 : 512);
					preview.drawFilledRect(0, 0, preview.getW(), preview.getH(),
										   GAGCore::Color(28, 36, 30));
					unsigned index = 0;
					for (const auto &entry : package.at("terrains"))
					{
						if (index == 64)
							break;
						const auto type =
							map.terrainRegistry().find(entry.at("key").get<std::string>());
						if (gallery)
						{
							TerrainType neighbor = GRASS;
							for (const auto &other : package.at("terrains"))
								if (other.at("key") != entry.at("key"))
								{
									neighbor = *map.terrainRegistry().find(
										other.at("key").get<std::string>());
									break;
								}
							if (auto *image =
									swatches.terrainScene(*type, neighbor, phase, variation))
								preview.drawSurface(0, int(index * 192), image);
							if (auto *image = swatches.terrainScene(*type, WATER, phase, variation))
								preview.drawSurface(192, int(index * 192), image);
							if (auto *image = swatches.terrainScene(*type, SAND, phase, variation))
								preview.drawSurface(384, int(index * 192), image);
							if (auto *image = swatches.terrain(*type, 192))
								preview.drawSurface(576, int(index * 192), image);
						}
						else if (auto *image = swatches.terrain(*type, 64))
							preview.drawSurface(int((index % 8) * 64), int((index / 8) * 64),
												image);
						++index;
					}
					for (const auto &entry : package.at("resources"))
					{
						if (index == 64)
							break;
						const auto type =
							map.resourceRegistry().find(entry.at("key").get<std::string>());
						if (gallery)
						{
							TerrainType backdrop = GRASS;
							if (!package.at("terrains").empty())
								backdrop = *map.terrainRegistry().find(
									package.at("terrains").front().at("key").get<std::string>());
							const auto &levels = map.resourceRegistry().presentation(*type).levels;
							unsigned column = 0;
							for (const auto &level : levels)
							{
								if (column >= 12)
									break;
								if (auto *image = swatches.resourceStage(
										*type, backdrop, level.stock, phase, variation, 64))
									preview.drawSurface(int(column * 64), int(index * 192 + 64),
														image);
								++column;
							}
						}
						else if (auto *image = swatches.resource(*type, GRASS, 64))
							preview.drawSurface(int((index % 8) * 64), int((index / 8) * 64),
												image);
						++index;
					}
					if (!IMG_SavePNG(preview.getSDLSurface(), previewPath.c_str()))
						throw std::runtime_error("Cannot write set preview");
				}
				report["valid"] = true;
			}
			catch (const std::exception &error)
			{
				std::string reason(error.what());
				if (reason.size() > 1900)
				{
					// Preserve a valid UTF-8 prefix when an author-supplied
					// field name appears in a bounded validation diagnostic.
					std::size_t length = 1900;
					while (length && (static_cast<unsigned char>(reason[length]) & 0xc0) == 0x80)
						--length;
					reason.resize(length);
				}
				report["reason"] = std::move(reason);
			}
			writeJsonReport(reportPath, report.dump());
			return report["valid"].get<bool>() ? 0 : 2;
		}
		if (mode == "map generators")
		{
			catalog(source, cli.get("--format") == "json");
			return 0;
		}
		const bool render = mode == "map render";
		const bool generate = mode == "map generate";
		const bool importing = mode == "map import-image";
		const bool exporting = mode == "map export-image";
		const bool writesMap = generate || importing;
		std::string output, preview, config, json, mapImage, renderField, fieldColor,
			generatorExport;
		int renderPixels = MapRender::DefaultPixels;
		MapSettings overrides, settings;
		std::vector<std::string> directories;
		int previewSize = 0, previewScale = Cli::DefaultPreviewScale, imageSeamWidth = -1;
		bool sizeSpecified = false, scaleSpecified = false;
		for (const auto &[arg, value] : cli.occurrences)
		{
			if (arg == "--output")
				output = value;
			else if (arg == "--render-max-pixels" && render)
			{
				const auto n = number(value);
				if (!n || n > MapRender::MaximumPixels)
					throw std::runtime_error("Render limit must be 1..8192");
				renderPixels = int(n);
			}
			else if (arg == "--render-field" && render)
				renderField = value;
			else if (arg == "--field-color" && render)
				fieldColor = value;
			else if (arg == "--export-generator-package" && generate)
				generatorExport = value;
			else if (arg == "--report-file" && !render)
				json = value;
			else if (arg == "--preview" && (writesMap || mode == "map preview"))
				preview = value;
			else if (arg == "--map-image" && generate)
				mapImage = value;
			else if (arg == "--image-seam-width" && importing)
			{
				const auto n = number(value);
				if (n > 16)
					throw std::runtime_error("Image seam width must be 0..16 tiles");
				imageSeamWidth = int(n);
			}
			else if (arg == "--config" && generate)
				config = value;
			else if (arg == "--set" && writesMap)
				setting(overrides, value);
			else if (writesMap && (arg == "--seed" || arg == "--width" || arg == "--height" ||
								   arg == "--teams" || arg == "--workers"))
				overrides[arg.substr(2)] = value;
			else if (arg == "--preview-scale" && !render)
			{
				const auto n = number(value);
				if (n != 2 && n != 4 && n != 8)
					throw std::runtime_error("Preview scale must be 2, 4 or 8");
				previewScale = int(n);
				scaleSpecified = true;
			}
			else if (arg == "--preview-size" && !render)
			{
				const auto n = number(value);
				if (n < 128 || n > 4096)
					throw std::runtime_error("Preview size must be 128..4096");
				previewSize = int(n);
				sizeSpecified = true;
			}
			else if (arg == "--data-dir")
				directories.push_back(value);
			else if (arg != "--generator-package")
				throw std::invalid_argument("Unknown option for " + mode + ": " + arg);
		}
		if (!writesMap && !exporting && !render)
		{
			if (!preview.empty() && !output.empty())
				throw std::invalid_argument("Choose --output or --preview, not both");
			if (!output.empty())
				preview = output;
		}
		if ((importing || exporting) && output.empty())
			throw std::invalid_argument("Image import/export requires --output");
		if (output.empty() && preview.empty() && json.empty() && mapImage.empty())
			throw std::invalid_argument("Specify an output path; use " + mode + " --help");
		if ((sizeSpecified || scaleSpecified) && preview.empty())
			throw std::invalid_argument("Preview size/scale requires a PNG output");
		if (sizeSpecified && scaleSpecified)
			throw std::invalid_argument("Choose --preview-size or --preview-scale, not both");
		// Compare the actual gzip destination as well as the user-supplied name.
		std::vector<std::string> paths{
			output, writesMap ? preview : "", config, json, mapImage, renderField, generatorExport};
		if (!generate)
		{
			paths.push_back(source);
			if (!importing && !endsWithGz(source) &&
				std::filesystem::exists(std::string(source) + ".gz"))
				paths.push_back(std::string(source) + ".gz");
		}
		if (writesMap && !output.empty() && !endsWithGz(output))
			paths.push_back(glob2GzipWritePath(output));
		for (size_t a = 0; a < paths.size(); ++a)
			for (size_t b = a + 1; b < paths.size(); ++b)
				if (samePath(paths[a], paths[b]))
					throw std::invalid_argument("Input and output paths must be distinct");
		GenerationRequest request;
		if (generate)
		{
			request.setMethodDefaults(method(source));
			request.seed = 1;
			if (!generatorExport.empty())
			{
				const auto &definition = request.definition();
				if (!definition.apiVersion || !definition.owner)
					throw std::invalid_argument("Package export requires a custom generator");
				auto package = std::static_pointer_cast<const MapGeneration::JavaScript::Package>(
					definition.owner);
				parentDirectory(generatorExport);
				std::ofstream file(generatorExport, std::ios::binary);
				file << package->canonical;
				file.close();
				if (!file)
					throw std::runtime_error("Cannot export generator package");
			}
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
		if (importing)
		{
			request.setMethodDefaults(GenerationRequest::eUNIFORM);
			request.seed = 1;
			configure(request, overrides);
		}
		struct ClearGlobal
		{
			~ClearGlobal() { globalContainer = nullptr; }
		} clear;
		if (render)
		{
			if (output.empty())
				throw std::runtime_error("map render requires --output");
			if (!fieldColor.empty() && renderField.empty())
				throw std::runtime_error("--field-color requires --render-field");
			GAGCore::setProcessEnvironment("SDL_VIDEODRIVER", "dummy", 1);
			GAGCore::setProcessEnvironment("SDL_AUDIODRIVER", "dummy", 1);
		}
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
		MapImageImportReport imageReport;
		if (generate)
		{
			result = GenerationService().generate(game, request, !json.empty());
			if (!result)
			{
				if (!json.empty())
					writeJsonReport(json, describeGenerationFailure(request, result));
				throw std::invalid_argument(result.diagnostic());
			}
			std::cout << result.diagnostic() << "\n";
		}
		else if (importing)
		{
			const int expected = overrides.count("teams") ? int(number(overrides.at("teams"))) : 0;
			try
			{
				importMapImage(game, source, request, expected, imageReport, imageSeamWidth);
			}
			catch (...)
			{
				if (!json.empty())
					writeJsonReport(json,
									"{\"report_type\":\"image_import_failure\",\"image_import\":" +
										imageReport.json() + "}\n");
				throw;
			}
			std::cout << "Image import: " << imageReport.json() << "\n";
		}
		else
		{
			// Explicit paths use the same loader as normal games, without stepping simulation.
			// A bare ".map"/".game" path prefers an existing ".gz" sibling, matching how
			// the rest of the engine resolves map/save names.
			std::string inputPath = source;
			std::error_code exists;
			if (!endsWithGz(inputPath) && std::filesystem::exists(inputPath + ".gz", exists))
				inputPath += ".gz";
			GAGCore::BinaryInputStream stream(GAGCore::openInflatingFileStreamBackend(inputPath));
			if (stream.isEndOfStream() || !game.load(&stream))
				throw std::runtime_error("Cannot load map/save: " + inputPath);
		}
		if (render)
		{
			MapRender::Field field;
			if (!renderField.empty())
				field = MapRender::readField(renderField);
			if (!fieldColor.empty())
			{
				std::istringstream color(fieldColor);
				char a = 0, b = 0;
				if (!(color >> field.red >> a >> field.green >> b >> field.blue) || a != ',' ||
					b != ',' || !(color >> std::ws).eof())
					throw std::runtime_error("Expected --field-color r,g,b");
			}
			PresentationFrame scene;
			SceneRequest request;
			request.includePanels = false;
			SceneExtractor().prepare(
				(game).captureReadBoundary({}, true, SceneExtractor::requirements(request)),
				request, scene);
			MapRender::toPng(scene, output, renderPixels, renderField.empty() ? nullptr : &field);
			return 0;
		}
		// Analyze the original snapshot before any serializer updates its header metadata.
		std::string report = json.empty() ? ""
										  : describeMap(game, generate ? &request : nullptr,
														generate ? &result : nullptr);
		if (importing && !report.empty())
		{
			const auto closing = report.find_last_of('}');
			if (closing == std::string::npos)
				throw std::runtime_error("Cannot append image import details to map report");
			report.insert(closing, ",\"image_import\":" + imageReport.json());
		}
		if (exporting || !mapImage.empty())
		{
			const std::string destination = exporting ? output : mapImage;
			parentDirectory(destination);
			exportMapImage(game, destination);
			std::cout << "Map image: " << destination << "\n";
		}
		if (!preview.empty())
		{
			parentDirectory(preview);
			exportPreview(game, preview, previewSize, previewScale);
			std::cout << "Preview: " << preview << " (" << game.map.getW() << "x" << game.map.getH()
					  << " tiles)\n";
		}
		if (writesMap && !output.empty())
			saveMap(game, output, std::filesystem::path(output).stem().string());
		if (!json.empty())
			writeJsonReport(json, report);
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Map command: " << e.what() << "\n";
		return dynamic_cast<const std::invalid_argument*>(&e) ? 2 : 3;
	}
}
