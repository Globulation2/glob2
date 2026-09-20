#include "GenerationContext.h"
#include "GeneratorRegistry.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameScreen.h"
#include "CustomGamePreferences.h"
#include "AINames.h"
#include "CustomGameScreen.h"
#include "LobbyMapPreview.h"
#include "LandscapePickerScreen.h"
#include "LandscapePreviewer.h"
#include "Game.h"
#include "GenerationService.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "LobbyMapCatalog.h"
#include "Player.h"
#include "Unit.h"
#include "StartQualityScreen.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
std::string tr(const std::string &s)
{
	return Toolkit::getStringTable()->getString("[" + s + "]");
}
const Uint32 A = ALIGN_SCREEN_CENTERED;
std::vector<std::string> localized(std::vector<std::string> v)
{
	for (auto &s : v)
		s = tr(s);
	return v;
}
} // namespace

CustomGameChoiceScreen::CustomGameChoiceScreen(const std::string &title,
											   const std::vector<std::string> &choices,
											   int selected, bool profiles,
											   const std::vector<bool> &enabled)
	: title(title), choices(choices), selected(selected), profiles(profiles), enabled(enabled)
{
	gfx = globalContainer->gfx;
	controls = new LobbyControls();
	controls->regions[20].offset = std::max(0, (selected + 1) * 58 - (gfx->getH() - 135));
	addWidget(controls);
	controls->render = [this]
	{
		auto &ui = *controls;
		int w = std::min(gfx->getW() - 32, 1000), x = (gfx->getW() - w) / 2, height = gfx->getH();
		ui.setDimensions(gfx->getW(), height);
		ui.box({x - 8, 8, w + 16, height - 16}, Color(232, 237, 218), 8);
		ui.text(x + 8, 20, this->title, "standard", w - 16);
		int left = w < 800 ? 174 : 235, right = x + left + 22, rightW = w - left - 22;
		ui.beginRegion(20, {x, 60, left, height - 135});
		int yy = 60 - ui.regions[20].offset;
		for (size_t i = 0; i < this->choices.size(); ++i)
		{
			auto name = this->choices[i];
			auto split = name.find(" - ");
			if (split != std::string::npos)
				name = name.substr(0, split);
			ui.button(
				"profile/" + std::to_string(i), {x, yy + int(i) * 58, left - 8, 52}, "",
				[this, i]
				{
					this->selected = i;
					controls->regions[21].offset = 0;
				},
				int(i) == this->selected);
			ui.text(x + 9, yy + int(i) * 58 + 8, name, "standard", left - 25);
			auto full = this->choices[i];
			auto separator = full.find(" - ");
			if (separator != std::string::npos)
				full = full.substr(separator + 3);
			ui.text(x + 9, yy + int(i) * 58 + 31, full, "little", left - 25, true);
		}
		ui.endRegion(this->choices.size() * 58);
		ui.beginRegion(21, {right, 60, rightW, height - 135});
		yy = 60 - ui.regions[21].offset;
		int start = yy;
		ui.text(right, yy, this->choices[this->selected], "standard", rightW - 15);
		yy += 32;
		std::string content =
			this->profiles ? AINames::getAIProfile(AINames::selectionOrder()[this->selected]) : "";
		auto first = content.find("\n\n");
		if (first != std::string::npos)
			content = content.substr(first + 2);
		size_t pos = 0;
		while (pos < content.size())
		{
			size_t end = content.find("\n\n", pos);
			auto part =
				content.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
			auto colon = part.find(':');
			if (colon != std::string::npos && colon < 40)
			{
				ui.text(right, yy, part.substr(0, colon), "standard", rightW - 15);
				yy += 25;
				part = part.substr(colon + 1);
			}
			yy += ui.paragraph(right, yy, rightW - 18, part, "standard") + 20;
			if (end == std::string::npos)
				break;
			pos = end + 2;
		}
		ui.endRegion(yy - start);
		ui.button(
			"profile/back", {x, height - 55, 100, 34}, tr("Back"), [this] { endExecute(-2); },
			false, true, true);
		ui.button(
			"profile/use", {right, height - 55, rightW, 34},
			tr("Use") + " " +
				this->choices[this->selected].substr(0, this->choices[this->selected].find(" - ")),
			[this] { endExecute(this->selected); }, true);
	};
}
void CustomGameChoiceScreen::onSDLEvent(SDL_Event *e)
{
	if (controls->handle(e))
		return;
	if (e->type != SDL_KEYDOWN)
		return;
	if (e->key.keysym.sym == SDLK_ESCAPE)
		endExecute(-2);
	if (e->key.keysym.sym == SDLK_RETURN)
		endExecute(selected);
	if (e->key.keysym.sym == SDLK_UP || e->key.keysym.sym == SDLK_DOWN)
	{
		selected = std::clamp(selected + (e->key.keysym.sym == SDLK_UP ? -1 : 1), 0,
							  int(choices.size()) - 1);
		controls->regions[21].offset = 0;
		auto &region = controls->regions[20];
		if (selected * 58 < region.offset)
			region.offset = selected * 58;
		else if ((selected + 1) * 58 > region.offset + region.box.h)
			region.offset = (selected + 1) * 58 - region.box.h;
	}
}
void CustomGameChoiceScreen::onAction(Widget *, Action action, int code, int)
{
	if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT)
	{
		if (code == -2)
			endExecute(-2);
		if (code == -3)
			endExecute(selected);
	}
}

namespace
{
std::string colorName(Color c)
{
	struct Named
	{
		const char *name;
		int r, g, b;
	};
	static const Named colors[] = {
		{"Red", 255, 0, 0},      {"Yellow", 255, 255, 0}, {"Lime", 128, 255, 0},
		{"Teal", 0, 255, 128},   {"Azure", 0, 128, 255},  {"Green", 0, 255, 0},
		{"Cyan", 0, 255, 255},   {"Blue", 0, 0, 255},     {"Magenta", 255, 0, 255},
		{"Orange", 255, 128, 0}, {"Purple", 128, 0, 255}, {"White", 255, 255, 255},
		{"Gray", 128, 128, 128}, {"Brown", 128, 64, 0},   {"Pink", 255, 128, 192}};
	int best = 1000000;
	std::string name;
	for (const auto &n : colors)
	{
		int d = (int(c.r) - n.r) * (int(c.r) - n.r) + (int(c.g) - n.g) * (int(c.g) - n.g) +
				(int(c.b) - n.b) * (int(c.b) - n.b);
		if (d < best)
		{
			best = d;
			name = n.name;
		}
	}
	return tr(name);
}
} // namespace

CustomGameScreen::CustomGameScreen() : Glob2TabScreen(false, true)
{
	gfx = globalContainer->gfx;
	username = globalContainer->settings.getUsername();
	// Windows uses a shared working-directory map folder. Unix can also lack
	// a per-user root when HOME is unavailable; do not hide those shipped maps.
	auto files = Toolkit::getFileManager();
	const auto firstRoot = files->getDirCount() ? files->getDir(0) : std::string();
	separateMapLibraries =
		!firstRoot.empty() && firstRoot != "." && firstRoot != "./Contents/Resources";
	groups[0] = addGroup(tr("Map"));
	groups[1] = addGroup(tr("Players & Teams"));
	groups[2] = addGroup(tr("Game Rules"));
	// TabScreen still owns activation; the lobby supplies composed tab headers.
	for (auto widget : widgets)
		widget->visible = false;
	preview = new LobbyMapPreview();
	addWidget(preview);
	preview->visible = false;
	controls = new LobbyControls();
	controls->render = [this] { renderLobby(); };
	addWidget(controls);
	CustomGamePreferences preferences;
	if (preferences.load(*files))
	{
		setup = preferences.setup;
		userMaps = preferences.userMaps && separateMapLibraries;
		landscapeSortOrder = preferences.landscapeSortOrder;
		std::copy(std::begin(preferences.expanded), std::end(preferences.expanded), expanded);
		listMaps();
		if (setup.random)
			invalidate();
		else
			loadMap(setup.premadeMap);
		// The visible library may differ from the selected map (e.g. an empty library).
		std::copy(std::begin(preferences.librarySelection), std::end(preferences.librarySelection),
				  librarySelection);
	}
	else
	{
		// No saved lobby: a random map at four colonies (FEEDBACK 2026-09-14: random maps are the
		// default, not the premade library).
		listMaps();
		setup.random = true;
		setup.setCapacity(4);
		validMap = false;
		source.clear();
		invalidate();
	}

	activateGroup(groups[0]);
}
GameHeader &CustomGameScreen::getGameHeader()
{
	setup.writeHeader(gameHeader, username);
	for (int i = 0; i < gameHeader.getNumberOfPlayers(); ++i)
	{
		auto &player = gameHeader.getBasePlayer(i);
		if (player.type != BasePlayer::P_LOCAL)
			player.name = AINames::getAIText(setup.colonies[player.teamNumber].ai) + " / " +
						  colonyLabel(player.teamNumber);
	}
	return gameHeader;
}
CustomGameScreen::~CustomGameScreen()
{
	savePreferences();
	if (!snapshot.empty())
	{
		std::error_code error;
		std::filesystem::remove_all(std::filesystem::path(snapshot).parent_path(), error);
	}
}
void CustomGameScreen::savePreferences()
{
	CustomGamePreferences preferences;
	preferences.setup = setup;
	preferences.userMaps = userMaps;
	preferences.landscapeSortOrder = landscapeSortOrder;
	std::copy(std::begin(librarySelection), std::end(librarySelection),
			  preferences.librarySelection);
	std::copy(std::begin(expanded), std::end(expanded), preferences.expanded);
	const auto text = preferences.encode();
	if (text == lastSavedPreferences)
		return;
	if (preferences.save(*Toolkit::getFileManager()))
		lastSavedPreferences = text;
	else
		preferencesRetryAt = SDL_GetTicks() + 5000;
}
int CustomGameScreen::choose(const std::string &title, const std::vector<std::string> &values,
							 int selected, bool profiles, const std::vector<bool> &enabled)
{
	CustomGameChoiceScreen screen(title, values, selected, profiles, enabled);
	int result = screen.execute(globalContainer->gfx, 40);
	if (result == QUIT_APPLICATION)
		endExecute(QUIT_APPLICATION);
	return result;
}
void CustomGameScreen::onGroupActivated(int group)
{
	currentTab = group;
	preview->cancelDrag();
}
// One request per playable landscape, exactly as picking it would leave the draft: the
// settings last used with it, or its defaults, at the current size and colony count.
std::vector<std::pair<int, GenerationRequest>> CustomGameScreen::landscapeEntries() const
{
	std::vector<std::pair<int, GenerationRequest>> entries;
	for (int method : GeneratorRegistry::builtins().methods(false))
	{
		auto draft = setup;
		draft.generatorHistory.select(draft.generator, method);
		draft.generator.nbTeams = setup.capacity;
		entries.emplace_back(method, draft.generator);
	}
	return entries;
}
void CustomGameScreen::chooseLandscape()
{
	const auto entries = landscapeEntries();
	std::vector<LandscapePickerScreen::Entry> shown;
	int selected = 0;
	for (const auto &[method, request] : entries)
	{
		if (method == setup.generator.method)
			selected = int(shown.size());
		LandscapePickerScreen::Entry entry{tr(GenerationRequest::methodName(method)), request,
										   method};
		if (const auto *definition = GeneratorRegistry::builtins().find(method))
			entry.tags = definition->tags;
		shown.push_back(std::move(entry));
	}
	LandscapePickerScreen picker(tr("Landscape"), std::move(shown), selected,
								 LandscapePickerScreen::SortOrder(landscapeSortOrder));
	const int result = picker.execute(globalContainer->gfx, 40);
	// Persisted the next time preferences save, the way any other lobby choice on this screen is.
	landscapeSortOrder = int(picker.currentSortOrder());
	// Size and colony count are shared lobby settings the picker can also change (FEEDBACK
	// 2026-09-17); bring them back whether or not a landscape was actually picked, so backing
	// out still keeps what was chosen there, the same two-way relationship the sort order has.
	if (picker.sharedWDec() != setup.generator.wDec || picker.sharedHDec() != setup.generator.hDec ||
		picker.sharedTeams() != setup.capacity)
	{
		setup.generator.wDec = picker.sharedWDec();
		setup.generator.hDec = picker.sharedHDec();
		setup.generator.nbTeams = picker.sharedTeams();
		setup.setCapacity(picker.sharedTeams());
		++setup.mapRevision;
		invalidate();
	}
	if (result == QUIT_APPLICATION)
		endExecute(QUIT_APPLICATION);
	else if (result >= 0 && result < int(entries.size()))
	{
		const GenerationRequest request = picker.chosenRequest();
		applyLandscape(entries[result].first, picker.chosenSeed(), &request);
	}
}
void CustomGameScreen::applyLandscape(int method, std::optional<std::uint32_t> seed,
									  const GenerationRequest *shown)
{
	setup.generatorHistory.select(setup.generator, method);
	// The parameters the picker rolled the map with come along, so the lobby shows the map it
	// showed: the landscape's own, or a random set from "Randomize parameters".
	if (shown && shown->method == method)
		setup.generator.options = shown->options;
	chosenSeed = seed;
	randomAttempts = 0;
	++setup.mapRevision;
	invalidate();
	// An explicit choice, not an edit in progress: preview it now rather than after the debounce.
	previewDue = SDL_GetTicks();
}
// Reset keeps the chosen landscape and the Game Rules tab's starting workers, and returns every
// other field in the map column to the landscape's registered defaults.
void CustomGameScreen::resetParameters()
{
	GenerationRequest reset;
	reset.setMethodDefaults(setup.generator.method);
	reset.nbWorkers = setup.generator.nbWorkers;
	reset.terrainType = setup.generator.terrainType;
	reset.seed = setup.generator.seed;
	setup.generator = reset;
	setup.setCapacity(reset.nbTeams);
	chosenSeed.reset();
	randomAttempts = 0;
	++setup.mapRevision;
	invalidate();
}
// Random parameters (FEEDBACK 2026-09-14): every one of the landscape's own controls drawn at
// random, keeping the size, colony count and workers the player chose. Some combinations make no
// map: a draw the generator refuses up front is redrawn on the spot (randomizeControls), and one
// the world refuses is redrawn when the preview's candidates come back empty (collectCandidates),
// up to kRandomAttempts times, so the first set that generates a valid map is the one kept.
void CustomGameScreen::randomizeParameters()
{
	randomAttempts = kRandomAttempts;
	if (!drawRandomParameters())
	{
		randomAttempts = 0;
		message = tr("Generation failed. Adjust settings or press Start to retry.");
		return;
	}
	chosenSeed.reset();
	++setup.mapRevision;
	invalidate();
	previewDue = SDL_GetTicks();
}
bool CustomGameScreen::drawRandomParameters()
{
	auto draft = setup.generator;
	draft.nbTeams = setup.capacity;
	if (!draft.randomizeControls(GenerationContext::randomSeed()))
		return false;
	setup.generator = draft;
	return true;
}
void CustomGameScreen::showStartQuality()
{
	if (!quality.measured)
		return;
	std::vector<std::string> labels;
	std::vector<Color> colors;
	for (size_t i = 0; i < quality.colonies.size(); ++i)
	{
		labels.push_back(colonyLabel(int(i)));
		colors.push_back(i < preview->starts.size() ? preview->starts[i].color
													: Color(160, 172, 149));
	}
	StartQualityScreen screen(quality, labels, colors);
	if (screen.execute(globalContainer->gfx, 40) == QUIT_APPLICATION)
		endExecute(QUIT_APPLICATION);
}
void CustomGameScreen::invalidate()
{
	preview->cancelDrag();
	validMap = false;
	// Keep the scores belonging to the retained generated preview during a reroll.
	// A premade map must not inherit scores from an older generated snapshot.
	if (source != snapshot)
		quality = {};
	previewRevision = ~0u;
	previewPending = setup.random;
	previewDue = SDL_GetTicks() + 500;
	message.clear();
}
std::string CustomGameScreen::colonyLabel(int i) const
{
	return std::to_string(i + 1) + " - " +
		   (i < (int)preview->starts.size() ? colorName(preview->starts[i].color) : tr("Colony"));
}
void CustomGameScreen::listMaps()
{
	std::vector<std::filesystem::path> roots;
	auto fm = Toolkit::getFileManager();
	for (unsigned d = 0; d < fm->getDirCount(); ++d)
		if (!separateMapLibraries || (d == 0) == userMaps)
			roots.push_back(std::filesystem::path(fm->getDir(d)) / "maps");
	mapPaths.clear();
	mapNames.clear();
	for (auto &entry : lobbyMapCatalog(roots))
	{
		mapPaths.push_back(entry.path);
		mapNames.push_back(entry.name);
	}
}

bool CustomGameScreen::loadMap(const std::string &requestedPath)
{
	std::error_code pathError;
	auto canonical = std::filesystem::canonical(requestedPath, pathError);
	const std::string path = (pathError || setup.random) ? requestedPath : canonical.string();
	try
	{
		const auto stamp = std::filesystem::last_write_time(path);
		const auto bytes = std::filesystem::file_size(path);
		auto cached = std::find_if(
			preview->cache.begin(), preview->cache.end(), [&](const auto &entry)
			{ return entry.path == path && entry.time == stamp && entry.bytes == bytes; });
		LobbyMapPreview::CachedMap entry;
		if (cached != preview->cache.end())
		{
			entry = *cached;
			preview->cache.erase(cached);
		}
		else
		{
			auto world = std::make_unique<Game>(nullptr);
			BinaryInputStream body(Toolkit::getFileManager()->openInputStreamBackend(path));
			if (!body.isValid() || !world->load(&body) || world->teamsCount() < 1 ||
				world->teamsCount() > Team::MAX_COUNT)
				throw std::runtime_error("map body");
			entry.path = path;
			entry.time = stamp;
			entry.bytes = bytes;
			entry.header = world->mapHeader;
			entry.terrain.loadFromMap(world->map);
			if (!entry.terrain.isLoaded())
				throw std::runtime_error("map terrain");
			for (int i = 0; i < world->teamsCount(); ++i)
				entry.starts.push_back({world->teams[i]->startPosX, world->teams[i]->startPosY,
										world->teams[i]->color});
		}
		int old = setup.capacity;
		mapHeader = entry.header;
		setup.setCapacity(mapHeader.getNumberOfTeams());
		source = path;
		validMap = true;
		preview->setMapThumbnail(entry.terrain);
		preview->starts = entry.starts;
		preview->cache.erase(std::remove_if(preview->cache.begin(), preview->cache.end(),
											[&](const auto &item) { return item.path == path; }),
							 preview->cache.end());
		preview->cache.insert(preview->cache.begin(), std::move(entry));
		if (preview->cache.size() > 8)
			preview->cache.pop_back();
		if (!setup.random)
		{
			setup.premadeMap = path;
			librarySelection[userMaps] = path;
		}
		if (setup.capacity < old)
			message = tr("Colony") + " " + std::to_string(setup.capacity + 1) + "-" +
					  std::to_string(old) + ": " +
					  tr("Extra colony assignments are retained for larger maps.");
		else
			message.clear();
		return true;
	}
	catch (const std::exception &)
	{
		preview->setState(MapPreview::State::Failed);
		validMap = false;
		message = tr("Could not load this map. Choose another map or retry.");
		return false;
	}
}
bool CustomGameScreen::generateMap()
{
	// Consume this request even on failure; retry on a new edit or launch, not every frame.
	previewPending = false;
	std::string candidate;
	if (!setup.validation().empty())
	{
		message = tr(setup.validation());
		return false;
	}
	try
	{
		// Private snapshot ownership, never a user map and never a predictable
		// filename.
		std::random_device random;
		std::filesystem::path directory;
		for (int attempt = 0; attempt < 32; ++attempt)
		{
			directory =
				std::filesystem::temp_directory_path() /
				("glob2-custom-" + std::to_string(random()) + "-" + std::to_string(random()));
			if (std::filesystem::create_directory(directory))
				break;
			directory.clear();
		}
		if (directory.empty())
			throw std::runtime_error("snapshot directory");
		candidate = (directory / "preview.map").string();
		std::unique_ptr<Game> game;
		GenerationService generator;
		const auto rootSeed = GenerationContext::randomSeed();
		GenerationResult generationResult;
		setup.generator.nbTeams = setup.capacity;
		// A map the landscape picker showed is rolled once, exactly as shown. Otherwise some
		// rolls cannot fit every starting colony, and among those that can, some hand one
		// colony far better ground than another: roll the whole budget either way and keep the
		// best-scoring world rather than the first that fits.
		const auto shown = chosenSeed;
		chosenSeed.reset();
		double bestScore = -1.0;
		const int candidates = shown ? 1 : GenerationService::kSampledCandidates;
		for (int attempt = 0; attempt < candidates; ++attempt)
		{
			auto roll = std::make_unique<Game>(nullptr);
			auto request = setup.generator;
			request.seed =
				shown
					? *shown
					: GenerationContext::deriveSeed(rootSeed, "attempt/" + std::to_string(attempt));
			const auto rollResult = generator.generate(*roll, request);
			std::cout << "Map generation: " << rollResult.diagnostic() << std::endl;
			if (!rollResult || roll->teamsCount() != setup.capacity)
			{
				if (!game)
					generationResult = rollResult; // keep a failure worth reporting
				continue;
			}
			if (rollResult.quality.score <= bestScore)
				continue;
			bestScore = rollResult.quality.score;
			game = std::move(roll);
			generationResult = rollResult;
		}
		if (!game)
			throw std::runtime_error(generationResult.diagnostic());
		// Veteran/Fast start: the generators place level-0 workers; raise the chosen world's
		// before it is saved, so the snapshot every client loads carries them.
		if (setup.startingUnitLevel != 0)
			for (int team = 0; team < game->teamsCount(); ++team)
				for (int i = 0; i < Unit::MAX_COUNT; ++i)
					if (Unit *unit = game->teams[team]->myUnits[i])
						unit->resetAtLevel(setup.startingUnitLevel);
		GameHeader initial;
		initial.setRandomSeed(generationResult.seed);
		setup.writeHeader(initial, username);
		game->setGameHeader(initial);
		{
			BinaryOutputStream stream(
				Toolkit::getFileManager()->openOutputStreamBackend(candidate));
			if (!stream.isValid())
				throw std::runtime_error("snapshot");
			game->save(&stream, true, "Random map");
			stream.flush();
		}
		// Keep the actual generated world for rasterization. Only read back its
		// finalized header (offset and SHA1), not a second whole Game and Map.
		BinaryInputStream headerStream(
			Toolkit::getFileManager()->openInputStreamBackend(candidate));
		if (!headerStream.isValid() || !mapHeader.load(&headerStream))
			throw std::runtime_error("snapshot header");
		MapThumbnail terrain;
		terrain.loadFromMap(game->map);
		if (!terrain.isLoaded())
			throw std::runtime_error("snapshot preview");
		preview->setMapThumbnail(terrain);
		preview->starts.clear();
		for (int i = 0; i < game->teamsCount(); ++i)
			preview->starts.push_back(
				{game->teams[i]->startPosX, game->teams[i]->startPosY, game->teams[i]->color});
		source = candidate;
		validMap = true;
		if (!snapshot.empty())
			std::filesystem::remove_all(std::filesystem::path(snapshot).parent_path());
		snapshot = candidate;
		previewRevision = setup.mapRevision;
		quality = generationResult.quality;
		randomAttempts = 0;
		message.clear();
		return true;
	}
	catch (const std::exception &error)
	{
		std::cerr << "Map preview: " << error.what() << std::endl;
		if (!candidate.empty())
			std::filesystem::remove_all(std::filesystem::path(candidate).parent_path());
		validMap = false;
		message = tr("Generation failed. Adjust settings or press Start to retry.");
		return false;
	}
}
void CustomGameScreen::updateLayout()
{
	if (gfx)
	{
		controls->setScreenPosition(0, 0);
		controls->setDimensions(gfx->getW(), gfx->getH());
	}
}
void CustomGameScreen::onSDLEvent(SDL_Event *event)
{
	if (currentTab == groups[0] && validMap && !controls->popup.open && controls->pressed.empty() &&
		preview->handlePreviewEvent(event))
		return;
	if (controls->handle(event))
		return;
	if (event->type == SDL_KEYDOWN && currentTab == groups[0] && !setup.random &&
		(event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN) &&
		!mapPaths.empty())
	{
		auto found = std::find(mapPaths.begin(), mapPaths.end(), librarySelection[userMaps]);
		int selected = found == mapPaths.end() ? 0 : int(found - mapPaths.begin());
		selected = std::clamp(selected + (event->key.keysym.sym == SDLK_UP ? -1 : 1), 0,
							  int(mapPaths.size()) - 1);
		if (loadMap(mapPaths[selected]))
		{
			auto &region = controls->regions[10 + userMaps];
			int top = selected * 28;
			if (top < region.offset)
				region.offset = top;
			else if (top + 28 > region.offset + region.box.h)
				region.offset = top + 28 - region.box.h;
			controls->focus = "map/entry/" + std::to_string(selected);
		}
		return;
	}
	if (event->type != SDL_KEYDOWN)
		return;
	if (event->key.keysym.mod & KMOD_CTRL)
	{
		if (event->key.keysym.sym >= SDLK_1 && event->key.keysym.sym <= SDLK_3)
		{
			activateGroup(groups[event->key.keysym.sym - SDLK_1]);
			controls->resetFocus();
		}
	}
	else if (event->key.keysym.sym == SDLK_ESCAPE)
		endExecute(CANCEL);
	else if (event->key.keysym.sym == SDLK_RETURN)
		onAction(nullptr, BUTTON_SHORTCUT, OK, 0);
}
void CustomGameScreen::onAction(Widget *widget, Action action, int code, int value)
{
	if (action != BUTTON_RELEASED && action != BUTTON_SHORTCUT)
		return;
	if (code == CANCEL)
		endExecute(CANCEL);
	if (code == OK && setup.validation().empty())
	{
		if (setup.random && candidates)
			finishPreview();
		if (setup.random && (!validMap || previewRevision != setup.mapRevision))
			if (!generateMap())
				return;
		if (validMap)
		{
			savePreferences();
			endExecute(OK);
		}
	}
}
void CustomGameScreen::onTimer(Uint32 tick)
{
	if (controls->pressed.empty() && !controls->popup.open &&
		Sint32(tick - preferencesRetryAt) >= 0)
		savePreferences();
	// Wait until the last edit settles and a dragged control/menu is released.
	if (previewPending && setup.random && Sint32(tick - previewDue) >= 0 &&
		controls->pressed.empty() && !controls->popup.open)
	{
		// A map the picker showed, or a draft that fails validation, resolves at once; anything
		// else rolls its candidates off this thread first.
		if (chosenSeed || !setup.validation().empty())
			generateMap();
		else
			startCandidates();
	}
	if (candidates && !candidates->busy())
		collectCandidates();
}
void CustomGameScreen::startCandidates()
{
	previewPending = false;
	setup.generator.nbTeams = setup.capacity;
	std::vector<GenerationRequest> requests(GenerationService::kSampledCandidates, setup.generator);
	if (candidates)
		candidates->restart(std::move(requests));
	else
		candidates = std::make_unique<LandscapePreviewer>(std::move(requests));
	candidateRevision = setup.mapRevision;
	message.clear();
}
// The candidates are in: keep the best-scoring one by rolling its seed for the snapshot, or
// report that none seated every colony. Returns whether a map came of it.
bool CustomGameScreen::collectCandidates()
{
	if (!candidates)
		return false;
	std::optional<std::uint32_t> best;
	double bestScore = -1.0;
	for (std::size_t i = 0; i < candidates->size(); ++i)
	{
		const auto preview = candidates->preview(i);
		if (preview.state == LandscapePreviewer::State::Ready && preview.score > bestScore)
		{
			bestScore = preview.score;
			best = preview.seed;
		}
	}
	const bool stale = candidateRevision != setup.mapRevision;
	candidates.reset();
	if (stale)
		return false; // an edit meanwhile already asked for a new preview
	if (!best)
	{
		// A random set of parameters the world refused on every seed: draw another while the
		// draws last (randomizeParameters), rather than leave the player a failure to fix by hand.
		if (randomAttempts > 0)
		{
			--randomAttempts;
			if (drawRandomParameters())
			{
				chosenSeed.reset();
				++setup.mapRevision;
				invalidate();
				previewDue = SDL_GetTicks();
				return false;
			}
		}
		randomAttempts = 0;
		validMap = false;
		message = tr("Generation failed. Adjust settings or press Start to retry.");
		return false;
	}
	chosenSeed = best;
	return generateMap();
}
// Wait for candidates in flight and apply them: for a launch that cannot wait for the timer,
// and for tests that drive the timer by hand.
void CustomGameScreen::finishPreview()
{
	while (candidates && candidates->busy())
		SDL_Delay(5);
	if (candidates)
		collectCandidates();
}
void CustomGameScreen::setMapMode(bool random)
{
	if (setup.random == random)
		return;
	setup.random = random;
	previewPending = false;
	validMap = false;
	// Candidates still rolling for the random map are dropped with it, or they would come back
	// and replace the premade choice (the lobby now opens on a random map, so a player can reach
	// the library while its first preview is still rolling).
	candidates.reset();
	if (random)
	{
		setup.setCapacity(setup.generator.nbTeams);
		invalidate();
	}
	else if (!setup.premadeMap.empty())
		loadMap(setup.premadeMap);
	else
	{
		// The first visit to the library with nothing chosen yet (the lobby now opens on a random
		// map): FourSquares1, the lobby's old opening map, or failing that the first map listed.
		listMaps();
		std::string first = mapPaths.empty() ? "" : mapPaths.front();
		for (const auto &p : mapPaths)
			if (std::filesystem::path(p).filename() == "FourSquares1.map")
			{
				first = p;
				break;
			}
		if (!first.empty())
			loadMap(first);
	}
}
void CustomGameScreen::showAIProfile(int colony)
{
	std::vector<std::string> labels;
	for (int i : AINames::selectionOrder())
		labels.push_back(AINames::getAISelectorText(i));
	int result = choose(colonyLabel(colony) + " / " + tr("AI strategy & counterplay"), labels,
						AINames::selectionIndex(setup.colonies[colony].ai), true);
	if (result >= 0)
		setup.colonies[colony].ai = (AI::ImplementationID)AINames::selectionOrder()[result];
}

void CustomGameScreen::renderLobby()
{
	auto &ui = *controls;
	int width = gfx->getW(), height = gfx->getH();
	int w = std::min(width - 32, 1120), x = (width - w) / 2;
	ui.box({x - 8, 8, w + 16, height - 16}, Color(232, 237, 218), 8);
	auto speed = globalContainer->settings;
	speed.gameSpeed = setup.speed;
	std::vector<std::string> titles = localized({"Map", "Players & Teams", "Game Rules"});
	std::vector<std::string> details = {
		setup.random ? tr(GenerationRequest::methodName(setup.generator.method))
					 : mapHeader.getMapName(),
		std::to_string(setup.activeColonies()) + " " + tr("colonies") + " / " + tr(setup.format),
		tr(setup.ruleset)};
	for (int i = 0; i < 3; ++i)
	{
		int tx = x + i * w / 3;
		ui.button(
			"tab/" + std::to_string(i), {tx, 16, w / 3 - 6, 38}, titles[i],
			[this, i]
			{
				activateGroup(groups[i]);
				controls->resetFocus();
			},
			currentTab == groups[i], true, false, width < 800 ? "standard" : "menu");
		ui.text(tx + 9, 59, details[i], "little", w / 3 - 20, true);
	}
	// Clear inactive viewports so wheel/page navigation only affects this tab.
	for (auto &pair : ui.regions)
		pair.second.box = {0, 0, 0, 0};
	int bodyY = 85, bodyH = height - 181;
	if (currentTab == groups[1])
		renderPlayers(x, bodyY, w, bodyH);
	else if (currentTab == groups[2])
		renderRules(x, bodyY, w, bodyH);
	else
		renderMap(x, bodyY, w, bodyH);
	gfx->setClipRect();
	gfx->drawLine(x, height - 85, x + w, height - 85, ui.line);
	std::string error = setup.validation();
	if (!setup.random && !validMap)
		error = tr("Select a valid map.");
	ui.text(x + 4, height - 77,
			tr(setup.format) + "  /  " + std::to_string(setup.activeColonies()) + " " +
				tr("colonies") + "  /  " + tr(setup.ruleset) + "  /  " + speed.getGameSpeedText(),
			"little", w - 8);
	ui.paragraph(x + 4, height - 53, w - 270, error.empty() ? message : tr(error));
	ui.button(
		"back", {x + w - 255, height - 52, 80, 34}, tr("Back"), [this] { endExecute(CANCEL); },
		false, true, true);
	auto label =
		!setup.humanColony()
			? tr(setup.random && !validMap ? "Generate & watch" : "Watch game")
			: tr(setup.random ? (validMap ? "Play this map" : "Generate & play") : "Start game");
	ui.button(
		"start", {x + w - 165, height - 52, 165, 34}, label,
		[this] { onAction(nullptr, BUTTON_SHORTCUT, OK, 0); }, true, error.empty());
}

void CustomGameScreen::renderPlayers(int x, int y, int w, int h)
{
	auto &ui = *controls;
	std::vector<std::string> formats = localized({"FFA", "2 vs 2", "You vs all"});
	int selected = setup.format == "FFA"          ? 0
				   : setup.format == "2 vs 2"     ? 1
				   : setup.format == "You vs all" ? 2
												  : -1;
	ui.segments("format", {x, y, std::min(w, 450), 30}, formats, selected,
				[this](int i) { setup.presetTeams(i); },
				{true, setup.activeColonies() == 4,
				 bool(setup.humanColony()) && setup.activeColonies() > 1});
	if (w > 760)
		ui.text(x + 470, y + 9,
				std::to_string(setup.controllerCount()) + " / 12 " + tr("controllers"), "little",
				w - 475, true);
	int top = y + 42, rowH = w < 760 ? 64 : 88;
	ui.beginRegion(1, {x, top, w, h - 42});
	int offset = ui.regions[1].offset;
	for (int i = 0; i < setup.capacity; ++i)
	{
		auto &c = setup.colonies[i];
		int ry = top + i * rowH - offset;
		ui.box({x, ry, w - 12, rowH - 8}, ui.panel);
		Color color =
			i < int(preview->starts.size()) ? preview->starts[i].color : Color(160, 172, 149);
		ui.box({x + 10, ry + 10, 30, 30}, color, 4);
		gfx->drawRect(x + 10, ry + 10, 30, 30, ui.ink);
		auto font = Toolkit::getFont("standard");
		font->pushStyle(Font::Style(
			Font::STYLE_NORMAL,
			Color((int(color.r) * 299 + int(color.g) * 587 + int(color.b) * 114) > 140000 ? 0 : 255,
				  (int(color.r) * 299 + int(color.g) * 587 + int(color.b) * 114) > 140000 ? 0 : 255,
				  (int(color.r) * 299 + int(color.g) * 587 + int(color.b) * 114) > 140000 ? 0
																						  : 255)));
		gfx->drawString(x + 18, ry + 16, font, std::to_string(i + 1));
		font->popStyle();
		ui.text(x + 48, ry + 16, colorName(color), "little", 65);
		int cx = x + 116, cw = w < 760 ? 100 : 126, teamW = 92;
		int ax = cx + cw + 8, aw = x + w - 22 - teamW - 8 - ax;
		std::vector<bool> enabled;
		for (int j = 0; j < 4; ++j)
		{
			auto draft = setup;
			enabled.push_back(draft.setController(i, (CustomGameSetup::Controller)j));
		}
		std::string id = "colony/" + std::to_string(i);
		ui.dropdown(
			id + "/controller", {cx, ry + 8, cw, 30},
			localized({"You", "AI", "You + AI", "Closed"}), c.controller,
			[this, i](int value) { setup.setController(i, (CustomGameSetup::Controller)value); },
			enabled, tr("Shared control needs a free controller slot (maximum 12)."));
		if (c.controller == CustomGameSetup::Computer || c.controller == CustomGameSetup::Shared)
		{
			std::vector<std::string> names;
			for (int j : AINames::selectionOrder())
				names.push_back(AINames::getAISelectorText(j));
			ui.dropdown(
				id + "/ai", {ax, ry + 8, aw, 30}, names, AINames::selectionIndex(c.ai),
				[this, i](int value)
				{ setup.colonies[i].ai = (AI::ImplementationID)AINames::selectionOrder()[value]; });
			ui.button(
				id + "/info", {x + w - 66, ry + 38, 44, 18}, tr("Info"),
				[this, i] { showAIProfile(i); }, false, true, true, "little");
		}
		else
			ui.text(
				ax + 6, ry + 16,
				tr(c.controller == CustomGameSetup::Human ? "You control this colony." : "Closed"),
				"little", aw - 10, true);
		std::vector<std::string> teams;
		for (int j = 0; j < setup.capacity; ++j)
			teams.push_back(tr("Team") + " " + std::to_string(j + 1));
		ui.dropdown(id + "/team", {x + w - 22 - teamW, ry + 8, teamW, 30}, teams, c.alliance,
					[this, i](int value)
					{
						setup.colonies[i].alliance = value;
						setup.format = "Custom teams";
					});
		auto summary = c.controller == CustomGameSetup::Human ? username
					   : c.controller == CustomGameSetup::Closed
						   ? tr("This colony will not join the match.")
						   : AINames::getAISummary(c.ai);
		ui.text(cx, ry + 45, summary, "little", w - 196, true);
		if (c.controller == CustomGameSetup::Shared && rowH > 64)
			ui.text(cx, ry + 62, tr("You and the AI both issue orders. Uses two controller slots."),
					"little", w - 150, true);
	}
	ui.endRegion(setup.capacity * rowH);
}

void CustomGameScreen::renderRules(int x, int y, int w, int h)
{
	auto &ui = *controls;
	ui.beginRegion(2, {x, y, w, h});
	int yy = y - ui.regions[2].offset, startY = yy;
	ui.text(x, yy, tr("Try a ruleset, then make it your own."), "standard", w - 20);
	yy += 24;
	std::vector<std::string> names =
		localized({"Standard", "Quick clash", "Open book", "Last colony standing"});
	std::vector<std::string> effects =
		localized({"Classic colony building",
				   setup.random ? "8 workers / 2x speed" : "Premade: only speed changes (2x)",
				   "Start with terrain known", "Win through conquest"});
	int columns = w >= 800 ? 4 : 2, tileW = (w - 12) / columns;
	for (int i = 0; i < 4; ++i)
	{
		int tx = x + (i % columns) * tileW, ty = yy + (i / columns) * 54;
		ui.button(
			"ruleset/" + std::to_string(i), {tx, ty, tileW - 7, 48}, "",
			[this, i]
			{
				auto rev = setup.mapRevision;
				setup.presetRules(i);
				if (setup.random && rev != setup.mapRevision)
					invalidate();
			},
			tr(setup.ruleset) == names[i]);
		ui.text(tx + 9, ty + 7, names[i], "standard", tileW - 22);
		ui.text(tx + 9, ty + 31, effects[i], "little", tileW - 22, true);
	}
	yy += ((4 + columns - 1) / columns) * 54 + 6;
	// Rules are numbered in the order they were added, so a later rule can belong to an earlier
	// category: list them grouped by category, in each category's first-appearance order, so
	// every category heading appears once.
	std::vector<int> order;
	for (int first = 0; first < int(CustomGameSetup::ruleDefinitions.size()); ++first)
	{
		const std::string heading = CustomGameSetup::ruleDefinitions[first].category;
		bool seen = false;
		for (int earlier = 0; earlier < first; ++earlier)
			seen = seen || heading == CustomGameSetup::ruleDefinitions[earlier].category;
		if (!seen)
			for (int index = first; index < int(CustomGameSetup::ruleDefinitions.size()); ++index)
				if (heading == CustomGameSetup::ruleDefinitions[index].category)
					order.push_back(index);
	}
	std::string category;
	for (int index : order)
	{
		auto definition = CustomGameSetup::ruleDefinitions[index];
		if (category != definition.category)
		{
			category = definition.category;
			ui.text(x + 4, yy, tr(category), "standard", w - 20);
			yy += 24;
		}
		ui.box({x, yy, w - 12, 58}, ui.panel);
		ui.text(x + 10, yy + 8, tr(definition.label) + (setup.ruleChanged(index) ? " *" : ""),
				"standard", 170);
		int fieldX = x + 182, fieldW = w - 208;
		auto apply = [this, index](int value)
		{
			if (index == 0)
				setup.prestige = value == 0;
			if (index == 1)
				setup.revealed = value;
			if (index == 2)
				setup.locked = value == 0;
			if (index == 3)
				setup.speed = value;
			if (index == 5)
				setup.noResourceGrowth = value;
			if (index == 6)
				setup.resourceScarcity = value;
			if (index == 7)
				setup.instantConstruction = value;
			if (index == 8)
				setup.stockpileStart = value;
			if (index == 9)
				setup.noHunger = value;
				setup.unitUpgradesDisabled = value;
			if (index == 11)
				setup.glassCannonLevel = value;
			if (index == 12)
				setup.unitsFearless = value;
			if (index == 13)
				setup.permadeathDisabled = value;
			if (index == 14)
				setup.peacefulMode = value;
			if (index == 15)
				setup.buildingHpLevel = value;
			if (index == 17)
				setup.suddenDeathMinutes = value;
			if (index == 18)
				setup.winProbabilityPermille = value;
			setup.ruleset = "Custom";
		};
		std::string help;
		if (index < 3)
		{
			auto options = index == 0   ? localized({"Conquest or prestige", "Conquest only"})
						   : index == 1 ? localized({"Explore as you play", "Terrain revealed"})
										: localized({"Locked teams", "Can change in game"});
			ui.segments("rule/" + std::to_string(index), {fieldX, yy + 5, fieldW, 29}, options,
						index == 0   ? !setup.prestige
						: index == 1 ? setup.revealed
									 : !setup.locked,
						apply, {}, w < 800 ? "little" : "standard");
			help = tr(index == 0   ? "Conquest only removes prestige victory; map "
									 "scripts still apply."
					  : index == 1 ? "Revealed terrain does not reveal all enemy activity."
								   : "Choose whether teams can change during the match.");
		}
		else if (index == 3)
		{
			auto settings = globalContainer->settings;
			std::vector<std::string> options;
			for (int i = 0; i <= Settings::GAME_SPEED_MAXIMUM; ++i)
			{
				settings.gameSpeed = i;
				options.push_back(settings.getGameSpeedText());
			}
			ui.dropdown("rule/speed", {fieldX, yy + 5, fieldW, 29}, options, setup.speed, apply);
			help = tr("Changes the pace of the whole simulation.");
		}
		else if (index == 4)
		{
			if (setup.random)
				ui.stepper("rule/workers", {fieldX, yy + 5, 130, 29}, setup.generator.nbWorkers,
						   GenerationRequest::control(setup.generator.method, "workers").minimum,
						   GenerationRequest::control(setup.generator.method, "workers").maximum,
						   [this](int v)
						   {
							   setup.generator.nbWorkers = v;
							   ++setup.mapRevision;
							   setup.ruleset = "Custom";
							   invalidate();
						   });
			else
				ui.text(fieldX + 9, yy + 12, tr("Map-defined starting units"), "standard", fieldW);
			help = tr(setup.random ? "More workers jump-start colony growth. Changes the "
									 "generated map."
								   : "Premade maps retain their authored starting units.");
		}
		else if (index == 5 || index == 7 || index == 9)
		{
			auto options = index == 5 ? localized({"Grow normally", "No growth"})
						   : index == 7 ? localized({"Normal construction", "Instant"})
										: localized({"Units get hungry", "No hunger"});
			bool current = index == 5	 ? setup.noResourceGrowth
						   : index == 7 ? setup.instantConstruction
										: setup.noHunger;
			ui.segments("rule/" + std::to_string(index), {fieldX, yy + 5, fieldW, 29}, options,
						current, apply, {}, w < 800 ? "little" : "standard");
			help = tr(index == 5	? "Resources never grow or spread across the map."
					  : index == 7 ? "Building sites complete immediately, skipping delivery."
								   : "Units never grow hungry and never starve.");
		}
		else if (index == 6 || index == 8)
		{
			std::vector<std::string> options =
				index == 6 ? localized({"Off (today's growth)", "Scarce (2x slower)",
										 "Very scarce (4x slower)", "Extremely scarce (8x slower)"})
						   : localized({"None (today's default)", "Small (+50 each)",
										"Medium (+150 each)", "Large (+300 each)"});
			int current = index == 6 ? setup.resourceScarcity : setup.stockpileStart;
			ui.dropdown("rule/" + std::to_string(index), {fieldX, yy + 5, fieldW, 29}, options,
						current, apply);
			help = tr(index == 6 ? "Slows how often resources grow or spread across the map."
								  : "Seeds each team's shared market/exchange resource pool at "
									"game start.");
		}
		else if (index == 10 || index == 12 || index == 13 || index == 14)
		{
			auto options = index == 10	 ? localized({"Trains normally", "No upgrades"})
						   : index == 12 ? localized({"Retreats when damaged", "Fearless"})
						   : index == 13 ? localized({"Can die permanently", "No permadeath"})
										: localized({"Normal combat", "Peaceful mode"});
			bool current = index == 10	 ? setup.unitUpgradesDisabled
						   : index == 12 ? setup.unitsFearless
						   : index == 13 ? setup.permadeathDisabled
										: setup.peacefulMode;
			ui.segments("rule/" + std::to_string(index), {fieldX, yy + 5, fieldW, 29}, options,
						current, apply, {}, w < 800 ? "little" : "standard");
			help = tr(index == 10	? "Units still visit schools but never gain a level."
					  : index == 12 ? "Units fight to the death instead of retreating to heal."
					  : index == 13 ? "Units are never permanently lost -- HP just stops at 1."
									: "Disables all combat between every team.");
		}
		else if (index == 11 || index == 15)
		{
			std::vector<std::string> options =
				index == 11 ? localized({"Off (today's balance)", "Glass cannon x2", "Glass cannon x3"})
						   : localized({"Off (today's HP)", "Fortress x5", "Fortress x10"});
			int current = index == 11 ? setup.glassCannonLevel : setup.buildingHpLevel;
			ui.dropdown("rule/" + std::to_string(index), {fieldX, yy + 5, fieldW, 29}, options,
						current, apply);
			help = tr(index == 11 ? "Higher tiers deal more damage but have less HP and armor."
								  : "Higher tiers give every building much more HP.");
		}
		else if (index == 16)
		{
			if (setup.random)
			{
				std::vector<std::string> options = localized({"Standard", "Veteran", "Elite", "Legendary"});
				ui.dropdown("rule/startingLevel", {fieldX, yy + 5, fieldW, 29}, options,
					setup.startingUnitLevel,
					[this](int v)
					{
						setup.startingUnitLevel = v;
						++setup.mapRevision;
						setup.ruleset = "Custom";
						invalidate();
					});
			}
			else
				ui.text(fieldX + 9, yy + 12, tr("Map-defined starting units"), "standard", fieldW);
			help = tr(setup.random ? "Starting units spawn already leveled up. Changes the "
									 "generated map."
								   : "Premade maps retain their authored starting units.");
		}
		else if (index == 17)
		{
			std::vector<std::string> options =
				localized({"Off (no timer)", "30 minutes", "45 minutes", "60 minutes", "90 minutes"});
			const auto &minutes = CustomGameSetup::suddenDeathMinuteChoices;
			const int current = int(std::find(minutes.begin(), minutes.end(), setup.suddenDeathMinutes) -
									minutes.begin());
			ui.dropdown("rule/suddenDeath", {fieldX, yy + 5, fieldW, 29}, options,
						current < int(minutes.size()) ? current : 0,
						[apply, minutes](int v) { apply(minutes[v]); });
			help = tr("Match ends at the timer; highest prestige at that instant wins.");
		}
		else
		{
			std::vector<std::string> options = localized(
				{"Off (play it out)", "95% sure", "97% sure", "99% sure"});
			const auto &choices = CustomGameSetup::winProbabilityChoices;
			const int current = int(std::find(choices.begin(), choices.end(),
											  setup.winProbabilityPermille) - choices.begin());
			ui.dropdown("rule/winProbability", {fieldX, yy + 5, fieldW, 29}, options,
						current < int(choices.size()) ? current : 0,
						[apply, choices](int v) { apply(choices[v]); });
			help = tr("Ends the match once the result is no longer in doubt. Spectators "
					  "can always see each player's chance of winning in the statistics.");
		}
		ui.text(x + 10, yy + 39, help, "little", w - 40, true);
		yy += 65;
	}
	ui.button(
		"rules/restore", {x, yy, 230, 30}, tr("Restore standard rules"),
		[this]
		{
			auto rev = setup.mapRevision;
			setup.presetRules(0);
			if (setup.random && rev != setup.mapRevision)
				invalidate();
		},
		false, true, true);
	ui.text(x + 245, yy + 8, tr("* Changed from standard."), "little", w - 270, true);
	yy += 36;
	ui.endRegion(yy - startY);
}

void CustomGameScreen::renderMap(int x, int y, int w, int h)
{
	auto &ui = *controls;
	ui.segments("map/mode", {x, y, 300, 30}, localized({"Premade maps", "Random map"}),
				setup.random, [this](int value) { setMapMode(value); });
	int leftW = w < 800 ? 280 : w * 46 / 100;
	int rightX = x + leftW + 24, rightW = w - leftW - 24;
	int top = y + 43;
	if (!setup.random)
	{
		if (separateMapLibraries)
			ui.segments(
				"map/library", {x, top, leftW, 27}, localized({"Built-in maps", "Your maps"}),
				userMaps,
				[this](int value)
				{
					if (userMaps != bool(value))
					{
						userMaps = value;
						listMaps();
						if (!librarySelection[userMaps].empty())
							loadMap(librarySelection[userMaps]);
					}
				},
				{}, "little");
		else
			ui.text(x + 4, top + 6, tr("Map"), "little", leftW, true);
		int region = 10 + userMaps, listTop = top + 35, rowH = 28;
		ui.beginRegion(region, {x, listTop, leftW, h - 78});
		int yy = listTop - ui.regions[region].offset;
		for (size_t i = 0; i < mapPaths.size(); ++i)
			ui.button(
				"map/entry/" + std::to_string(i), {x, yy + int(i) * rowH, leftW - 12, rowH - 1},
				mapNames[i], [this, i] { loadMap(mapPaths[i]); },
				librarySelection[userMaps] == mapPaths[i]);
		if (mapPaths.empty())
			ui.paragraph(x + 10, yy + 12, leftW - 25, tr("No maps in this library."));
		ui.endRegion(std::max(30, int(mapPaths.size()) * rowH));
	}
	else
	{
		ui.beginRegion(3, {x, top, leftW, h - 43});
		int yy = top - ui.regions[3].offset, startY = yy;
		auto &g = setup.generator;
		auto changed = [this]
		{
			chosenSeed.reset();
			++setup.mapRevision;
			invalidate();
		};
		ui.text(x + 4, yy, tr("Landscape"), "little", leftW - 20, true);
		yy += 18;
		ui.chooser("generator/landscape", {x, yy, leftW - 16, 30},
				   tr(GenerationRequest::methodName(g.method)), [this] { chooseLandscape(); });
		yy += 36;
		// Right under the landscape, before its controls (FEEDBACK 2026-09-14): Reset to defaults,
		// and beside it Random parameters, which draws every control below at random.
		{
			GenerationRequest defaults;
			defaults.setMethodDefaults(g.method);
			const bool atDefaults = g.options == defaults.options && g.wDec == defaults.wDec &&
									g.hDec == defaults.hDec && setup.capacity == defaults.nbTeams;
			const int half = (leftW - 16 - 6) / 2;
			// The narrow column of the compact layout: a label the standard font cannot fit in
			// its half takes the small one rather than being cut off.
			const auto fitting = [half](const std::string &label)
			{
				return Toolkit::getFont("standard")->getStringWidth(label) + 16 <= half ? "standard"
																						: "little";
			};
			const std::string reset = tr("Reset to defaults"), random = tr("Random parameters");
			ui.button(
				"generator/reset", {x, yy, half, 30}, reset, [this] { resetParameters(); }, false,
				!atDefaults, true, fitting(reset));
			ui.button(
				"generator/random", {x + half + 6, yy, half, 30}, random,
				[this] { randomizeParameters(); }, false, true, true, fitting(random));
			yy += 38;
		}
		auto discrete = [&](const GenerationRequest::Control &c, const std::string &id)
		{
			ui.text(x + 4, yy + 7, tr(c.label), "little", 110, true);
			std::vector<std::string> options;
			for (int v : c.values())
				options.push_back(c.isChoice() ? tr(c.valueLabel(v))
											   : std::to_string(c.displayValue(v)));
			ui.dropdown(id, {x + 112, yy, leftW - 128, 28}, options, c.indexOf(c.get(g)),
						[this, c, changed](int i)
						{
							c.set(setup.generator, c.valueAt(i));
							if (c.id == "teams")
								setup.setCapacity(setup.generator.nbTeams);
							changed();
						});
			yy += 36;
		};
		for (const auto &c : GenerationRequest::sharedControls())
			if (c.id != "workers")
			{
				if (c.id == "teams")
					g.nbTeams = setup.capacity;
				const char *id = c.id == "width"    ? "generator/width"
								 : c.id == "height" ? "generator/height"
													: "generator/colonies";
				discrete(c, id);
			}

		ui.text(x + 4, yy, tr("Starting workers: Game Rules tab."), "little", leftW - 20, true);
		yy += 27;
		for (int section = 0; section < 3; ++section)
		{
			auto sectionName = section == 0 ? "Terrain" : section == 1 ? "Resources" : "Layout";
			ui.button(
				"generator/section/" + std::to_string(section), {x, yy, leftW - 16, 30},
				std::string(expanded[section] ? "-  " : "+  ") + tr(sectionName),
				[this, section] { expanded[section] = !expanded[section]; }, expanded[section]);
			yy += 38;
			if (!expanded[section])
				continue;
			bool any = false;
			for (const auto &c : GenerationRequest::controls(g.method))
			{
				if (static_cast<int>(c.group) != section)
					continue;
				any = true;
				std::string id = "generator/" + c.id;
				if (c.isToggle())
				{
					ui.checkbox(id, {x, yy, leftW - 16, 28}, tr(c.label), c.get(g) != 0,
								[this, c, changed](bool on)
								{
									c.set(setup.generator, on ? 1 : 0);
									changed();
								});
					yy += 34;
					continue;
				}
				if (c.powerOfTwo || !c.allowedValues.empty())
				{
					discrete(c, id);
					continue;
				}
				ui.text(x + 5, yy, tr(c.label), "little", leftW - 55, true);
				ui.text(x + leftW - 45, yy, std::to_string(c.get(g)), "little", 35);
				yy += 18;
				auto apply = [this, c, changed](int v)
				{
					if (c.get(setup.generator) != c.normalize(v))
					{
						c.set(setup.generator, v);
						changed();
					}
				};
				if ((c.maximum - c.minimum) / c.step > 8)
					ui.slider(id, {x + 8, yy, leftW - 32, 22}, (c.get(g) - c.minimum) / c.step, 0,
							  (c.maximum - c.minimum) / c.step,
							  [c, apply](int i) { apply(c.minimum + i * c.step); });
				else
					ui.stepper(id, {x + 8, yy, 130, 26}, c.get(g), c.minimum, c.maximum, apply,
							   c.step);
				yy += 34;
			}
			if (!any)
				yy +=
					ui.paragraph(x + 5, yy, leftW - 22,
								 tr(section == 1 ? "This landscape uses fixed resource placement."
												 : "Dimensions and colony count are set above.")) +
					8;
		}
		ui.endRegion(yy - startY + 8);
	}
	ui.text(rightX, top,
			setup.random ? tr(GenerationRequest::methodName(setup.generator.method))
						 : mapHeader.getMapName(),
			w < 800 ? "standard" : "menu", rightW);
	std::string dimensions = validMap       ? std::to_string(preview->getLastWidth()) + " x " +
												  std::to_string(preview->getLastHeight())
							 : setup.random ? std::to_string(1 << setup.generator.wDec) + " x " +
												  std::to_string(1 << setup.generator.hDec)
											: "";
	ui.text(rightX, top + 29,
			dimensions + "  /  " + std::to_string(setup.capacity) + " " + tr("colonies"), "little",
			rightW, true);
	// The generated map's start quality (FEEDBACK 2026-09-14): the fairness the lobby ranked its
	// candidate rolls by, and a small (i) that opens the breakdown behind it. Fairness is the
	// whole ranking now, so there is no second number to show beside it.
	if (setup.random && (validMap || previewBusy()) && quality.measured)
	{
		char summary[96];
		std::snprintf(summary, sizeof summary, "%s %.2f", tr("Fairness").c_str(),
					  quality.fairness);
		const int sw = Toolkit::getFont("little")->getStringWidth(summary);
		ui.text(rightX + rightW - sw - 30, top + 29, summary, "little", sw + 2, true);
		ui.button(
			"quality/info", {rightX + rightW - 24, top + 26, 22, 20}, "i",
			[this] { showStartQuality(); }, false, true, false, "little");
	}
	// A random map keeps a row under its preview for the Randomize button.
	const int previewLimit = std::max(1, h - (setup.random ? 210 : 174));
	int size = std::min(rightW, previewLimit);
	int previewW = size, previewH = size;
	// A reroll invalidates the launch snapshot, not the image being displayed.
	// Keep its geometry, terrain and instructions until the replacement is ready.
	const bool displayPreview = validMap || (previewBusy() && preview->isThumbnailLoaded());
	const int mapW = displayPreview ? preview->getLastWidth()
					 : setup.random ? (1 << setup.generator.wDec)
									: 0;
	const int mapH = displayPreview ? preview->getLastHeight()
					 : setup.random ? (1 << setup.generator.hDec)
									: 0;
	if (mapW > 0 && mapH > 0)
	{
		const auto fitted = MapPreviewGeometry::fit({0, 0, rightW, previewLimit}, mapW, mapH);
		previewW = fitted.w;
		previewH = fitted.h;
	}
	int px = rightX + (rightW - previewW) / 2, py = top + 51;
	int helpHeight = 0;
	ui.box({px - 3, py - 3, previewW + 6, previewH + 6}, ui.line);
	if (displayPreview)
	{
		preview->setScreenPosition(px - (gfx->getW() - 640) / 2, py - (gfx->getH() - 480) / 2);
		preview->setDimensions(previewW, previewH);
		preview->paint();
		helpHeight = ui.paragraph(rightX, py + previewH + 7, rightW, tr("Map preview controls"),
								  "little", true);
	}
	else
	{
		ui.box({px, py, previewW, previewH}, Color(211, 223, 197));
		helpHeight = ui.paragraph(
			rightX, py + previewH + 7, rightW,
			tr(previewBusy() ? "Map preview controls"
							 : "Preview unavailable. Adjust settings or start to retry."),
			"little", true);
	}
	if (setup.random)
	{
		const int buttonW = std::min(rightW, 160);
		ui.button(
			"map/randomize",
			{rightX + (rightW - buttonW) / 2, py + previewH + helpHeight + 17, buttonW, 30},
			tr("Randomize"),
			[this]
			{
				// Same settings, new seed: generateMap draws a fresh root seed on every run, so this
				// only has to ask for the preview now instead of after the edit debounce.
				invalidate();
				previewDue = SDL_GetTicks();
			},
			false, setup.validation().empty() && !previewBusy());
	}
}
