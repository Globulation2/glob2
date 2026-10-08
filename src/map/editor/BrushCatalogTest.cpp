// SPDX-License-Identifier: GPL-3.0-or-later
// The editor brush catalogue (BrushCatalog.h), its MapEdit cache, experiment gate,
// action round trips and the composed swatches (BrushSwatches.h).
#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "MapEdit.h"
#include "MapEditDialog.h"
#include "Race.h"
#include "TerrainExperiments.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace
{
using Json = nlohmann::json;

glob2test::GlobalsOptions display()
{
	return {.display = true,
			.width = 1024,
			.height = 768,
			.screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
}

void blank(MapEdit &editor)
{
	editor.game.map.setSize(5, 5, GRASS);
	editor.game.map.setGame(&editor.game);
	editor.game.addTeam();
	editor.game.teams[0]->race.loadDefault();
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
			editor.game.map.clearImmobileUnit(x, y);
	editor.viewportX = 0;
	editor.viewportY = 0;
	editor.updateCamera();
	editor.minimap.setMapSize(editor.game.map.getW(), editor.game.map.getH());
	editor.preparePresentation();
}

void importTerrain(MapEdit &editor, const Json &terrains)
{
	editor.game.map.importTerrainDefinitions(
		Json{{"schemaVersion", 1}, {"terrains", terrains}}.dump());
	editor.preparePresentation();
}

Json customTerrain(const std::string &key, const std::string &name, const char *appearance = "sand")
{
	return {{"key", key},
			{"name", name},
			{"base", "grass"},
			{"properties", {{"groundSpeedQ8", 192}}},
			{"appearance", appearance}};
}

std::vector<std::string> ids(const BrushGroup &group)
{
	std::vector<std::string> result;
	for (const auto &entry : group.entries)
		result.push_back(entry.id);
	return result;
}

void enableEverything(MapEdit &editor)
{
	for (const auto &definition : experimentDefinitions())
		REQUIRE(editor.enableExperimentForMap(definition.key));
	for (const auto &key : editor.game.map.resourceRegistry().experimentKeys())
		REQUIRE(editor.enableExperimentForMap(key));
}

struct Pixel
{
	int r, g, b, a;
};
Pixel pixel(GAGCore::DrawableSurface &surface, int x, int y)
{
	auto *sdl = surface.getSDLSurface();
	REQUIRE(sdl);
	REQUIRE(sdl->format == SDL_PIXELFORMAT_ARGB8888);
	const auto value = reinterpret_cast<const Uint32 *>(
		static_cast<const unsigned char *>(sdl->pixels) + y * sdl->pitch)[x];
	return {int((value >> 16) & 255), int((value >> 8) & 255), int(value & 255), int(value >> 24)};
}
// Mean absolute deviation of the luminance: zero for a flat colour.
double variation(GAGCore::DrawableSurface &surface)
{
	const int w = surface.getW(), h = surface.getH();
	std::vector<double> values;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const auto p = pixel(surface, x, y);
			values.push_back(0.3 * p.r + 0.59 * p.g + 0.11 * p.b);
		}
	double mean = 0;
	for (auto v : values)
		mean += v;
	mean /= double(values.size());
	double deviation = 0;
	for (auto v : values)
		deviation += std::abs(v - mean);
	return deviation / double(values.size());
}
bool fullyOpaque(GAGCore::DrawableSurface &surface)
{
	for (int y = 0; y < surface.getH(); ++y)
		for (int x = 0; x < surface.getW(); ++x)
			if (pixel(surface, x, y).a != 255)
				return false;
	return true;
}
} // namespace

TEST_SUITE("BrushCatalog")
{
	TEST_CASE("catalogue lists classic ground every catalogue member and every registry resource "
			  "[display]")
	{
		glob2test::HeadlessGlobals globals(display());
		MapEdit editor;
		blank(editor);
		const auto &catalog = editor.brushCatalog();
		REQUIRE_FALSE(catalog.empty());
		CHECK(catalog.front().section == BrushSection::Terrain);
		CHECK(catalog.front().key == "classic");
		CHECK(ids(catalog.front()) ==
			  std::vector<std::string>{"terrain/water", "terrain/sand", "terrain/grass"});
		// Every selectable built-in type is listed, locked exactly when the editor
		// would not offer it; all members of an expanded group appear inline.
		const auto &registry = editor.game.map.terrainRegistry();
		for (unsigned id = 0; id < TERRAIN_COUNT; ++id)
		{
			const auto type = TerrainType(id);
			const auto *entry = editor.findBrush("terrain/" + registry.key(type));
			CAPTURE(registry.key(type));
			if (!registry.presentation(type).editorSelectable)
			{
				CHECK_FALSE(entry);
				continue;
			}
			REQUIRE(entry);
			CHECK(entry->section == BrushSection::Terrain);
			CHECK(entry->locked == !terrainBrushOffered(registry, type, [&](const std::string &key)
														{ return editor.experimentEnabled(key); }));
			CHECK(entry->swatch.terrain == type);
			if (id >= 3)
				CHECK(entry->group == terrainGroupDefinition(terrainGroup(type)).key);
		}
		const auto *obstacles = findBrushGroup(catalog, BrushSection::Terrain, "obstacles");
		REQUIRE(obstacles);
		CHECK(ids(*obstacles) ==
			  std::vector<std::string>{"terrain/boulders", "terrain/hedge", "terrain/thicket"});
		CHECK(std::all_of(obstacles->entries.begin(), obstacles->entries.end(),
						  [](const BrushEntry &e) { return e.locked; }));
		CHECK_FALSE(obstacles->rules.empty());

		// Resources: every registry entry, foundation ones included, with placement
		// validity taken from the map's habitat rules.
		const auto &resources = editor.game.map.resourceRegistry();
		std::set<std::string> experiments;
		for (unsigned id = 0; id < resources.size(); ++id)
		{
			const auto resource = static_cast<ResourceId>(id);
			const auto *entry = editor.findBrush("resource/" + resources.key(resource));
			CAPTURE(resources.key(resource));
			REQUIRE(entry);
			CHECK(entry->section == BrushSection::Resources);
			CHECK(entry->action == "select resource " + resources.key(resource));
			std::vector<TerrainType> expected;
			for (unsigned t = 0; t < registry.size(); ++t)
				if (editor.game.map.terrainSupportsResourceType(TerrainType(t), resource))
					expected.push_back(TerrainType(t));
			CHECK(entry->validOn == expected);
			CHECK(entry->tooltip.find(entry->label) == 0);
			CHECK(entry->locked == !resources.requiredExperiment(resource).empty());
			if (!entry->experiment.empty())
			{
				experiments.insert(entry->experiment);
				CHECK(entry->group == entry->experiment);
			}
		}
		CHECK(editor.findBrush("resource/wheat"));
		REQUIRE(editor.findBrush("resource/gold-ore"));
		CHECK(editor.findBrush("resource/gold-ore")->locked);
		// Wheat grows on grass but not water; algae the reverse.
		const auto &wheat = editor.findBrush("resource/wheat")->validOn;
		CHECK(std::find(wheat.begin(), wheat.end(), GRASS) != wheat.end());
		CHECK(std::find(wheat.begin(), wheat.end(), WATER) == wheat.end());
		CHECK(editor.findBrush("resource/algae")->swatch.terrain == WATER);

		// Enabling a locked experiment for the map unlocks its entries, marks the
		// map modified, advances the revision and makes the brush selectable.
		REQUIRE(experiments.count("foundation-resources"));
		REQUIRE(experiments.count("landscape-resources"));
		REQUIRE(editor.findBrush("resource/scrub"));
		CHECK(editor.findBrush("resource/scrub")->locked);
		CHECK(editor.findBrush("resource/scrub")->group == "landscape-resources");
		// Fish live in water, scrub on grass.
		const auto &fish = editor.findBrush("resource/fish")->validOn;
		CHECK(std::find(fish.begin(), fish.end(), WATER) != fish.end());
		CHECK(std::find(fish.begin(), fish.end(), GRASS) == fish.end());
		editor.performAction("select resource gold-ore");
		CHECK(editor.currentBrushId().empty());
		editor.hasMapBeenModified = false;
		const auto revision = editor.catalogRevision();
		CHECK(editor.enableExperimentForMap("foundation-resources"));
		CHECK(editor.hasMapBeenModified);
		CHECK(editor.catalogRevision() > revision);
		CHECK(editor.experimentEnabled("foundation-resources"));
		CHECK(editor.game.gameHeader.getExperiments().has("foundation-resources"));
		CHECK_FALSE(editor.findBrush("resource/gold-ore")->locked);
		editor.performAction("select resource gold-ore");
		CHECK(editor.currentBrushId() == "resource/gold-ore");
		// Already enabled: nothing more to save.
		editor.hasMapBeenModified = false;
		CHECK(editor.enableExperimentForMap("foundation-resources"));
		CHECK_FALSE(editor.hasMapBeenModified);
		CHECK_FALSE(editor.enableExperimentForMap("no-such-experiment"));

		// Terrain groups follow the map header too, not just the player's settings.
		editor.performAction("select boulders");
		CHECK(editor.currentBrushId() == "resource/gold-ore"); // still locked: the selection stays
		CHECK(editor.enableExperimentForMap("obstacle-terrain"));
		CHECK_FALSE(editor.findBrush("terrain/hedge")->locked);
		editor.performAction("select terrain hedge");
		CHECK(editor.currentBrushId() == "terrain/hedge");
		// The farm zone is gated the same way.
		CHECK(editor.findBrush("zone/farm")->locked);
		CHECK(editor.enableExperimentForMap("farm-areas"));
		CHECK_FALSE(editor.findBrush("zone/farm")->locked);
	}

	TEST_CASE("imported terrain joins the custom group in natural order and refreshes the "
			  "catalogue [display]")
	{
		glob2test::HeadlessGlobals globals(display());
		MapEdit editor;
		blank(editor);
		const auto revision = editor.catalogRevision();
		CHECK_FALSE(findBrushGroup(editor.brushCatalog(), BrushSection::Terrain, "custom"));
		importTerrain(editor, Json::array({customTerrain("example:t10", "Terrain 10"),
										   customTerrain("example:t2", "Terrain 2"),
										   customTerrain("example:a", "Alpha", "water")}));
		CHECK(editor.catalogRevision() > revision);
		const auto *custom = findBrushGroup(editor.brushCatalog(), BrushSection::Terrain, "custom");
		REQUIRE(custom);
		CHECK(ids(*custom) == std::vector<std::string>{"terrain/example:a", "terrain/example:t2",
													   "terrain/example:t10"});
		CHECK(custom->entries[1].label == "Terrain 2");
		CHECK_FALSE(custom->entries[1].rules.empty());
		CHECK_FALSE(custom->entries[1].locked);
		// The terrain section stays classic, catalogue groups, then custom.
		std::vector<std::string> keys;
		for (const auto &group : editor.brushCatalog())
			if (group.section == BrushSection::Terrain)
				keys.push_back(group.key);
		CHECK(keys.front() == "classic");
		CHECK(keys.back() == "custom");
		// Imported types appear among resource habitats as well.
		const auto type = *editor.game.map.terrainRegistry().find("example:t2");
		const auto &wheat = editor.findBrush("resource/wheat")->validOn;
		CHECK((std::find(wheat.begin(), wheat.end(), type) != wheat.end()) ==
			  editor.game.map.terrainSupportsResourceType(
				  type, *editor.game.map.resourceRegistry().find("wheat")));
		editor.performAction("select terrain example:t2");
		CHECK(editor.currentBrushId() == "terrain/example:t2");
	}

	TEST_CASE("every unlocked catalogue action selects the entry it belongs to [display]")
	{
		glob2test::HeadlessGlobals globals(display());
		MapEdit editor;
		blank(editor);
		importTerrain(editor, Json::array({customTerrain("example:one", "One")}));
		auto custom = Json::parse(editor.game.map.resourceRegistry().serialize())["resources"][1];
		custom["key"] = "example:berries";
		editor.game.map.installResourceDefinitions(
			Json{{"schemaVersion", 1}, {"resources", Json::array({custom})}}.dump());
		enableEverything(editor);
		std::set<BrushSection> sections;
		std::size_t checked = 0;
		for (const auto &group : editor.brushCatalog())
			for (const auto &entry : group.entries)
			{
				CAPTURE(entry.id);
				CAPTURE(entry.action);
				CHECK(entry.group == group.key);
				CHECK(entry.section == group.section);
				CHECK_FALSE(entry.locked);
				editor.performAction("unselect");
				editor.performAction(entry.action);
				CHECK(editor.currentBrushId() == entry.id);
				sections.insert(entry.section);
				++checked;
			}
		CHECK(sections.size() == std::size_t(BrushSection::Count));
		CHECK(checked > TERRAIN_COUNT);
		REQUIRE(editor.findBrush("resource/example:berries"));
		CHECK(editor.findBrush("resource/example:berries")->group == "custom");
		CHECK(editor.findBrush("terrain/example:one")->group == "custom");

		// Legacy aliases select the same canonical brush as the registry action, so
		// the side panel and the palette highlight one selection.
		editor.performAction("select wheat");
		CHECK(editor.currentBrushId() == "resource/wheat");
		const auto wheat = editor.terrainType;
		editor.performAction("select resource wheat");
		CHECK(editor.terrainType == wheat);
		CHECK(editor.terrainType == TerrainSelector::selectorForResource(
										*editor.game.map.resourceRegistry().find("wheat")));
		CHECK(editor.canonicalSelector(TerrainSelector::Wheat) == wheat);
		editor.performAction("select stone");
		CHECK(editor.currentBrushId() == "resource/rocks");
		editor.performAction("select orange tree");
		CHECK(editor.currentBrushId() == "resource/orange-tree");
		editor.performAction("select prune");
		CHECK(editor.currentBrushId() == "resource/prune-tree");
		editor.performAction("select grass");
		CHECK(editor.currentBrushId() == "terrain/grass");
		editor.performAction("select road");
		CHECK(editor.currentBrushId() == "terrain/road");
	}

	TEST_CASE("imported terrain names cannot take over resource or built-in actions [display]")
	{
		glob2test::HeadlessGlobals globals(display());
		MapEdit editor;
		blank(editor);
		importTerrain(editor, Json::array({customTerrain("example:stone", "stone"),
										   customTerrain("example:wheat", "wheat"),
										   customTerrain("example:grass", "grass")}));
		editor.performAction("select stone");
		CHECK(editor.currentBrushId() == "resource/rocks");
		editor.performAction("select wheat");
		CHECK(editor.currentBrushId() == "resource/wheat");
		editor.performAction("select grass");
		CHECK(editor.currentBrushId() == "terrain/grass");
		editor.performAction("select terrain example:stone");
		CHECK(editor.currentBrushId() == "terrain/example:stone");
		// Unknown keys leave the selection alone.
		editor.performAction("select terrain example:missing");
		CHECK(editor.currentBrushId() == "terrain/example:stone");
		editor.performAction("select resource example:missing");
		CHECK(editor.currentBrushId() == "terrain/example:stone");
	}

	TEST_CASE("brush swatches are opaque and composed like the map [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(display());
		MapEdit editor;
		blank(editor);
		importTerrain(editor, Json::array({customTerrain("example:dune", "Dune", "sand"),
										   customTerrain("example:pond", "Pond", "deep_water")}));
		enableEverything(editor);
		auto &swatches = editor.brushSwatches();
		const auto &registry = editor.game.map.terrainRegistry();
		for (auto type : {WATER, DEEP_WATER, DARK_WATER, SAND, GRASS, VOID_HOLE,
						  *registry.find("example:dune"), *registry.find("example:pond")})
		{
			CAPTURE(registry.key(type));
			auto *surface = swatches.terrain(type, 64);
			REQUIRE(surface);
			CHECK(surface->getW() == 64);
			CHECK(fullyOpaque(*surface));
			// Every material here has texture
			// (the void is a uniform black hole on the map too).
			if (type != VOID_HOLE)
				CHECK(variation(*surface) > 0.5);
		}
		// Water materials draw their own opaque tiles rather than black.
		for (auto type : {WATER, DEEP_WATER, *registry.find("example:pond")})
		{
			const auto centre = pixel(*swatches.terrain(type, 64), 32, 32);
			CAPTURE(registry.key(type));
			CHECK(centre.a == 255);
			CHECK(centre.r + centre.g + centre.b > 60);
		}
		// An imported type uses the look it names, not its flat saved colour.
		CHECK(variation(*swatches.terrain(*registry.find("example:dune"), 64)) > 2.0);
		const auto *dune = swatches.terrain(*registry.find("example:dune"), 64);
		const auto *sand = swatches.terrain(SAND, 64);
		CHECK(std::memcmp(const_cast<GAGCore::DrawableSurface *>(dune)->getSDLSurface()->pixels,
						  const_cast<GAGCore::DrawableSurface *>(sand)->getSDLSurface()->pixels,
						  64 * 4) == 0);
		// Cached per size; resource swatches draw the sprite over valid ground.
		CHECK(swatches.terrain(GRASS, 64) == swatches.terrain(GRASS, 64));
		CHECK(swatches.terrain(GRASS, 40)->getW() == 40);
		const auto *wheatEntry = editor.findBrush("resource/wheat");
		auto *wheat = swatches.get(*wheatEntry, 64);
		REQUIRE(wheat);
		CHECK(fullyOpaque(*wheat));
		CHECK(std::memcmp(wheat->getSDLSurface()->pixels,
						  swatches.terrain(wheatEntry->swatch.terrain, 64)->getSDLSurface()->pixels,
						  std::size_t(wheat->getSDLSurface()->pitch) * 64) != 0);
		for (const auto &group : editor.brushCatalog())
			for (const auto &entry : group.entries)
				if (entry.section == BrushSection::Resources)
				{
					CAPTURE(entry.id);
					auto *surface = swatches.get(entry, 48);
					REQUIRE(surface);
					CHECK(fullyOpaque(*surface));
				}
		CHECK_FALSE(swatches.get(*editor.findBrush("tool/delete"), 64));
		// Gallery inspection composes mixed terrain and resource stock stages
		// without changing simulation state.
		const auto checksum = editor.game.map.checkSum(true);
		for (unsigned phase = 0; phase < 4; ++phase)
		{
			auto *scene =
				swatches.terrainScene(*registry.find("example:dune"), GRASS, phase, phase, 192);
			REQUIRE(scene);
			CHECK(scene->getW() == 192);
			CHECK(fullyOpaque(*scene));
			CHECK(variation(*scene) > 2.0);
			for (unsigned stock : {0u, 1u, 2u})
			{
				auto *stage = swatches.resourceStage(
					wheatEntry->swatch.resource, wheatEntry->swatch.terrain, stock, phase, phase);
				REQUIRE(stage);
				CHECK(fullyOpaque(*stage));
				auto *ground = swatches.terrain(wheatEntry->swatch.terrain, 64);
				CHECK(std::memcmp(stage->getSDLSurface()->pixels, ground->getSDLSurface()->pixels,
								  std::size_t(ground->getSDLSurface()->pitch) * 64) != 0);
			}
		}
		CHECK(editor.game.map.checkSum(true) == checksum);
		SDL_SaveBMP(
			swatches.terrainScene(SAND, WATER, 0, 0)->getSDLSurface(),
			(glob2test::artifactDirFromWorkingDirectory() + "/terrain-gallery.bmp").c_str());

		// Contact sheet of every terrain and resource swatch for review.
		std::vector<const BrushEntry *> shown;
		for (const auto &group : editor.brushCatalog())
			for (const auto &entry : group.entries)
				if (entry.section == BrushSection::Terrain ||
					entry.section == BrushSection::Resources)
					shown.push_back(&entry);
		const int columns = 12;
		GAGCore::DrawableSurface sheet(columns * 68,
									   int((shown.size() + columns - 1) / columns) * 68);
		sheet.drawFilledRect(0, 0, sheet.getW(), sheet.getH(), GAGCore::Color(0, 0, 0));
		for (std::size_t i = 0; i < shown.size(); ++i)
			sheet.drawSurface(int(i % columns) * 68 + 2, int(i / columns) * 68 + 2,
							  swatches.get(*shown[i], 64));
		SDL_SaveBMP(sheet.getSDLSurface(),
					(glob2test::artifactDirFromWorkingDirectory() + "/brush-swatches.bmp").c_str());

		// A changed registry or a device reset drops cached swatches.
		CHECK(swatches.size() > 0);
		importTerrain(editor, Json::array({customTerrain("example:late", "Late")}));
		CHECK(editor.brushSwatches().size() == 0);
		editor.brushSwatches().terrain(GRASS, 64);
		SDL_Event reset{};
		reset.type = SDL_EVENT_RENDER_TARGETS_RESET;
		GAGCore::GraphicContext::translateMouseEvent(&reset);
		CHECK(editor.brushSwatches().terrain(SAND, 64));
		CHECK(editor.brushSwatches().size() == 1);
	}
}
