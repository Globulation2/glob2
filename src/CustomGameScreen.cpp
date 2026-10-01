// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameScreen.h"
#include "AINames.h"
#include "CustomGamePreferences.h"
#include "Engine.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "GlobalContainer.h"
#include "LandscapePickerScreen.h"
#include "LandscapePreviewer.h"
#include "LobbyMapCatalog.h"
#include "LobbyMapPreview.h"
#include "Player.h"
#include "StartQualityScreen.h"
#include "Unit.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <ApplicationHost.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

namespace
{
std::string tr(const std::string &s)
{
	return Toolkit::getStringTable()->getString("[" + s + "]");
}
std::vector<std::string> localized(std::vector<std::string> v)
{
	for (auto &s : v)
		s = tr(s);
	return v;
}
} // namespace

// AI profile / choice screen ---------------------------------------------------------

CustomGameChoiceScreen::CustomGameChoiceScreen(const std::string &title, const std::vector<std::string> &choices,
											   int selected, bool profiles, const std::vector<bool> &enabled)
	: title(title), choices(choices), selected(selected), profiles(profiles), enabled(enabled)
{
}

Element CustomGameChoiceScreen::build(const Presentation &p)
{
	std::vector<std::string> names, descriptions;
	for (const auto &full : choices)
	{
		const auto split = full.find(" - ");
		names.push_back(split == std::string::npos ? full : full.substr(0, split));
		descriptions.push_back(split == std::string::npos ? std::string() : full.substr(split + 3));
	}
	const int current = std::clamp(selected, 0, std::max(0, int(choices.size()) - 1));
	std::vector<Element> detail{fe::heading(choices.empty() ? std::string() : choices[std::size_t(current)])};
	std::string content = profiles && !choices.empty() ? AINames::getAIProfile(AINames::selectionOrder()[std::size_t(current)]) : "";
	auto first = content.find("\n\n");
	if (first != std::string::npos)
		content = content.substr(first + 2);
	std::size_t pos = 0;
	while (pos < content.size())
	{
		const std::size_t end = content.find("\n\n", pos);
		auto part = content.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
		const auto colon = part.find(':');
		if (colon != std::string::npos)
		{
			detail.push_back(fe::paragraph(part.substr(0, colon), {fe::FontRole::Body}));
			part = part.substr(colon + 1);
		}
		detail.push_back(fe::paragraph(part, {fe::FontRole::Body, true}));
		if (end == std::string::npos)
			break;
		pos = end + 2;
	}
	auto details = fe::scroll("profile/details", fe::column(std::move(detail), {p.pt(8)}));
	fe::ListOptions listOptions;
	listOptions.enabled = enabled;
	listOptions.visibleRows = 12;
	listOptions.activate = [this](int) { use(); };
	auto list = fe::listView("profile/list", names, current, [this](int i) { choose(i); }, listOptions);
	Element body = fe::adaptive(
		[list, details, names, current, this](const fe::LayoutContext &ctx, fe::Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return fe::column({fe::choice("profile/select", names, current, [this](int i) { choose(i); }), fe::expanded(details)});
			return fe::row({fe::width(ctx.presentation.pt(260), list), fe::expanded(details)}, {-1, fe::CrossAlign::Stretch});
		});
	const std::string useLabel = tr("Use") + (choices.empty() ? std::string() : " " + names[std::size_t(current)]);
	return fe::page(title, body,
					fe::actions({{"profile/back", tr("Back"), [this] { endExecute(-2); }, false, SDLK_ESCAPE},
								 {"profile/use", useLabel, [this] { use(); }, true, SDLK_RETURN, !choices.empty()}},
								p),
					p, 1000);
}

// Lobby ------------------------------------------------------------------------------

CustomGameScreen::CustomGameScreen(GAGGUI::ScreenStack &screens) : screens(screens)
{
	GAGCore::ApplicationHost::customGameReady(false);
	username = globalContainer->settings.getUsername();
	// Windows uses a shared working-directory map folder. Unix can also lack
	// a per-user root when HOME is unavailable; do not hide those shipped maps.
	auto files = Toolkit::getFileManager();
	const auto firstRoot = files->getDirCount() ? files->getDir(0) : std::string();
	separateMapLibraries = !firstRoot.empty() && firstRoot != "." && firstRoot != "./Contents/Resources";
	preview = std::make_unique<LobbyMapPreview>();
	CustomGamePreferences preferences;
	if (preferences.load(*files))
	{
		setup = preferences.setup;
		userMaps = preferences.userMaps && separateMapLibraries;
		landscapeSortOrder = preferences.landscapeSortOrder;
		std::copy(std::begin(preferences.expanded), std::end(preferences.expanded), expanded);
		listMaps();
		if (setup.random)
			invalidatePreview();
		else
			loadMap(setup.premadeMap);
		// The visible library may differ from the selected map (e.g. an empty library).
		std::copy(std::begin(preferences.librarySelection), std::end(preferences.librarySelection), librarySelection);
	}
	else
	{
		// No saved lobby: a random map at four colonies (random maps are the default).
		listMaps();
		setup.random = true;
		setup.setCapacity(4);
		validMap = false;
		source.clear();
		invalidatePreview();
	}
}

void CustomGameScreen::selectTab(int tab)
{
	currentTab = std::clamp(tab, 0, 2);
	preview->cancelDrag();
	host().closePopup();
	invalidate();
}

bool CustomGameScreen::interceptEvent(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & SDL_KMOD_CTRL) && event.key.key >= SDLK_1 &&
		event.key.key <= SDLK_3)
	{
		selectTab(event.key.key - SDLK_1);
		return true;
	}
	return false;
}

void CustomGameScreen::launch()
{
	if (!setup.validation().empty())
		return;
	// A random map still resolving in the background: the disabled Start button
	// and its "Generating preview..." note explain why, instead of blocking here
	// on finishPreview()'s busy-wait with no visible feedback.
	if (setup.random && previewBusy())
		return;
	if (setup.random && candidates)
		finishPreview();
	if (setup.random && (!validMap || previewRevision != setup.mapRevision))
		if (!generateMap())
		{
			invalidate();
			return;
		}
	if (validMap)
	{
		savePreferences();
		endExecute(OK);
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

GameHeader &CustomGameScreen::getGameHeader()
{
	setup.writeHeader(gameHeader, username);
	gameHeader.getExperiments().clear();
	Engine::applyLocalExperiments(gameHeader, mapHeader);
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
	GAGCore::ApplicationHost::customGameReady(false);
	savePreferences();
}
std::shared_ptr<std::string> CustomGameScreen::releaseSnapshot()
{
	if (!setup.random)
		return {};
	return std::move(generatedSnapshot);
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

LandscapePickerScreen *CustomGameScreen::chooseLandscape()
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
	auto picker = std::make_unique<LandscapePickerScreen>(
		tr("Landscape"), std::move(shown), selected,
		LandscapePickerScreen::SortOrder(landscapeSortOrder));
	auto *result = picker.get(); // Non-owning; ScreenStack retains ownership through completion.
	screens.push(std::move(picker),
				 [this, entries](GAGGUI::Screen &screen, int result)
				 {
					 auto &picker = static_cast<LandscapePickerScreen &>(screen);
					 // Persisted the next time preferences save, the way any other lobby choice on this screen is.
					 landscapeSortOrder = int(picker.currentSortOrder());
					 // Size and colony count are shared lobby settings the picker can also change (FEEDBACK
					 // 2026-09-17); bring them back whether or not a landscape was actually picked, so backing
					 // out still keeps what was chosen there, the same two-way relationship the sort order has.
					 if (picker.sharedWDec() != setup.generator.wDec ||
						 picker.sharedHDec() != setup.generator.hDec ||
						 picker.sharedTeams() != setup.capacity)
					 {
						 setup.generator.wDec = picker.sharedWDec();
						 setup.generator.hDec = picker.sharedHDec();
						 setup.generator.nbTeams = picker.sharedTeams();
						 setup.setCapacity(picker.sharedTeams());
						 ++setup.mapRevision;
						 invalidatePreview();
					 }
					 if (result == QUIT_APPLICATION)
						 endExecute(QUIT_APPLICATION);
					 else if (result >= 0 && result < int(entries.size()))
					 {
						 const GenerationRequest request = picker.chosenRequest();
						 applyLandscape(entries[result].first, picker.chosenSeed(), &request);
					 }
				 });
	return result;
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
	invalidatePreview();
	// An explicit choice, not an edit in progress: preview it now rather than after the debounce.
	previewDue = SDL_GetTicks();
}

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
	invalidatePreview();
}

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
	invalidatePreview();
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
													: theme().palette.neutral);
	}
	screens.push(std::make_unique<StartQualityScreen>(quality, labels, colors),
				 [this](GAGGUI::Screen &, int result)
				 {
					 if (result == QUIT_APPLICATION)
						 endExecute(QUIT_APPLICATION);
				 });
}

void CustomGameScreen::invalidatePreview()
{
	preview->cancelDrag();
	validMap = false;
	// Keep the scores belonging to the retained generated preview during a reroll.
	// A premade map must not inherit scores from an older generated snapshot.
	if (!setup.random)
		quality = {};
	previewRevision = ~0u;
	previewPending = setup.random;
	previewDue = SDL_GetTicks() + 500;
	message.clear();
	invalidate();
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
	// Callers (the library list, test harnesses, saved preferences) may still pass
	// a bare ".map" name; prefer an existing ".gz" sibling before resolving it.
	const std::string resolvedRequest = glob2PreferGzipReadPath(*Toolkit::getFileManager(), requestedPath);
	std::error_code pathError;
	auto canonical = std::filesystem::canonical(resolvedRequest, pathError);
	const std::string path = (pathError || setup.random) ? resolvedRequest : canonical.string();
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
			BinaryInputStream body(Toolkit::getFileManager()->openInflatingInputStreamBackend(path));
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
		generatedSnapshot.reset();
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
		invalidate();
		return true;
	}
	catch (const std::exception &)
	{
		preview->setState(MapPreview::State::Failed);
		validMap = false;
		message = tr("Could not load this map. Choose another map or retry.");
		invalidate();
		return false;
	}
}

bool CustomGameScreen::generateMap()
{
	// Consume this request even on failure; retry on a new edit or launch, not every frame.
	previewPending = false;
	if (!setup.validation().empty())
	{
		message = tr(setup.validation());
		invalidate();
		return false;
	}
	try
	{
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
		Engine::applyLocalExperiments(initial, game->mapHeader);
		game->setGameHeader(initial);
		// GameLoadScreen reads these bytes directly (Engine::initCustomFromBytesTask): a map
		// this process just generated and is about to load right back gets no benefit from a
		// round trip through disk, so the serialized bytes just move to another in-memory owner.
		auto *memory = new MemoryStreamBackend();
		BinaryOutputStream saveStream(memory);
		game->save(&saveStream, true, "Random map");
		// save() finalizes mapHeader in place (map offset, SHA1) as it writes, so this is
		// already exactly what re-reading a saved copy of it back would have produced.
		mapHeader = game->mapHeader;
		MapThumbnail terrain;
		terrain.loadFromMap(game->map);
		if (!terrain.isLoaded())
			throw std::runtime_error("snapshot preview");
		preview->setMapThumbnail(terrain);
		preview->starts.clear();
		for (int i = 0; i < game->teamsCount(); ++i)
			preview->starts.push_back(
				{game->teams[i]->startPosX, game->teams[i]->startPosY, game->teams[i]->color});
		source.clear();
		generatedSnapshot = std::make_shared<std::string>(memory->takeContents());
		validMap = true;
		previewRevision = setup.mapRevision;
		quality = generationResult.quality;
		randomAttempts = 0;
		message.clear();
		invalidate();
		return true;
	}
	catch (const std::exception &error)
	{
		std::cerr << "Map preview: " << error.what() << std::endl;
		validMap = false;
		message = tr("Generation failed. Adjust settings or press Start to retry.");
		invalidate();
		return false;
	}
}

void CustomGameScreen::onTimer(Uint32 tick)
{
	const bool busyBefore = previewBusy();
	const bool validBefore = validMap;
	const std::string messageBefore = message;
	if (candidates)
		candidates->poll();
	if (!host().interacting() && Sint32(tick - preferencesRetryAt) >= 0)
		savePreferences();
	// Wait until the last edit settles and a dragged control/menu is released.
	if (previewPending && setup.random && Sint32(tick - previewDue) >= 0 && !host().interacting())
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
	if (busyBefore != previewBusy() || validBefore != validMap || messageBefore != message)
		invalidate();
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
				invalidatePreview();
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

void CustomGameScreen::finishPreview()
{
	while (candidates && candidates->busy())
	{
		candidates->poll();
		SDL_Delay(5);
	}
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
		invalidatePreview();
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
			if ((std::filesystem::path(p).filename() == "FourSquares1.map" ||
				std::filesystem::path(p).filename() == "FourSquares1.map.gz"))
			{
				first = p;
				break;
			}
		if (!first.empty())
			loadMap(first);
	}
	invalidate();
}

void CustomGameScreen::showAIProfile(int colony)
{
	std::vector<std::string> labels;
	for (int i : AINames::selectionOrder())
		labels.push_back(AINames::getAISelectorText(i));
	// CustomGameChoiceScreen must be pushed, not blocking-executed: the
	// browser host has no Asyncify and ApplicationHost::wait is a hard
	// error there (docs/browser/adr-003-screen-execution.md).
	screens.push(std::make_unique<CustomGameChoiceScreen>(
					 colonyLabel(colony) + " / " + tr("AI strategy & counterplay"), labels,
					 AINames::selectionIndex(setup.colonies[colony].ai), true, std::vector<bool>{}),
				 [this, colony](GAGGUI::Screen &, int result)
				 {
					 if (result >= 0)
						 setup.colonies[colony].ai =
							 (AI::ImplementationID)AINames::selectionOrder()[result];
				 });
}


Element CustomGameScreen::build(const Presentation &p)
{
	GAGCore::ApplicationHost::customGameReady(validMap && !previewBusy() && setup.validation().empty());
	const bool narrow = p.compact() || p.safe.w < p.pt(720);
	auto speed = globalContainer->settings;
	speed.gameSpeed = setup.speed;
	// Phones keep the former Map / Opponents / Review flow with Back and Next.
	const bool phoneFlow = p.compact();
	const std::vector<std::string> titles = localized(phoneFlow ? std::vector<std::string>{"Map", "Opponents", "Review"}
															   : std::vector<std::string>{"Map", "Players & Teams", "Game Rules"});
	const std::vector<std::string> details = {
		setup.random ? tr(GenerationRequest::methodName(setup.generator.method)) : mapHeader.getMapName(),
		std::to_string(setup.activeColonies()) + " " + tr("colonies") + " / " + tr(setup.format), tr(setup.ruleset)};
	std::vector<Element> tabs;
	for (int i = 0; i < 3; ++i)
	{
		fe::ButtonOptions options;
		options.selected = currentTab == i;
		options.role = narrow ? fe::FontRole::Body : fe::FontRole::Heading;
		auto tab = fe::button("tab/" + std::to_string(i), titles[std::size_t(i)], [this, i] { selectTab(i); }, options);
		// Desktop tabs carry their current choice underneath, as before.
		tabs.push_back(fe::expanded(narrow ? tab : fe::column({tab, fe::caption(details[std::size_t(i)])}, {p.pt(2)})));
	}
	Element body = currentTab == 1 ? playersTab(p, narrow) : currentTab == 2 ? rulesTab(p, narrow) : mapTab(p, narrow);
	std::string error = setup.validation();
	if (!setup.random && !validMap)
		error = tr("Select a valid map.");
	else if (setup.random && previewBusy())
		error = tr("Generating preview...");
	const std::string summary = tr(setup.format) + "  /  " + std::to_string(setup.activeColonies()) + " " + tr("colonies") + "  /  " +
								tr(setup.ruleset) + "  /  " + speed.getGameSpeedText();
	const std::string note = error.empty() ? message : tr(error);
	const bool ready = error.empty() && (!narrow || (validMap && !previewBusy() && (!setup.random || previewRevision == setup.mapRevision)));
	const std::string startLabel = !setup.humanColony() ? tr(setup.random && !validMap ? "Generate & watch" : "Watch game")
													  : tr(setup.random ? (validMap ? "Play this map" : "Generate & play") : "Start game");
	std::vector<fe::MenuAction> footerActions;
	if (phoneFlow)
	{
		const int tab = currentTab;
		footerActions.push_back({"back", tr("Back"), [this, tab] { tab > 0 ? selectTab(tab - 1) : endExecute(CANCEL); }, false, SDLK_ESCAPE});
		if (tab < 2)
			footerActions.push_back({"start", tr("Next"), [this, tab] { selectTab(tab + 1); }, true, SDLK_RETURN});
		else
			footerActions.push_back({"start", startLabel, [this] { launch(); }, true, SDLK_RETURN, ready});
	}
	else
	{
		footerActions.push_back({"back", tr("Back"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE});
		footerActions.push_back({"start", startLabel, [this] { launch(); }, true, SDLK_RETURN, ready});
	}
	auto actionRow = fe::actions(std::move(footerActions), p, fe::ActionStyle::Compact);
	std::vector<Element> summaryParts{fe::caption(summary)};
	if (!note.empty())
		summaryParts.push_back(fe::paragraph(note, {fe::FontRole::Support}));
	// Experiments come from Settings, not the lobby, so say which ones this match will carry.
	if (!globalContainer->settings.experiments.empty())
		summaryParts.push_back(fe::paragraph(tr("Experiments") + ": " + experimentLabelList(globalContainer->settings.experiments), {fe::FontRole::Support}));
	// Desktop: summary at the left, compact Back / Start at the right, as before.
	Element footerColumn = p.touch ? fe::column({fe::column(std::move(summaryParts), {p.pt(4)}), actionRow}, {p.pt(6)})
								   : fe::row({fe::expanded(fe::column(std::move(summaryParts), {p.pt(4)})), actionRow}, {p.pt(8), fe::CrossAlign::Center});
	fe::CardOptions cardOptions;
	cardOptions.padding = p.pt(narrow ? 10 : 16);
	auto panel = fe::card(fe::column({fe::row(std::move(tabs), {p.pt(6)}), fe::expanded(body), fe::divider(), footerColumn}, {p.pt(8)}), cardOptions);
	if (p.touch)
		return fe::center(fe::maxWidth(p.pt(1120), panel));
	// The desktop lobby fills the window inside a margin, as before.
	const int margin = std::clamp(p.safe.w / 24, p.pt(8), p.pt(50));
	return fe::padding(fe::Insets::symmetric(margin, p.pt(8)), panel);
}

Element CustomGameScreen::mapTab(const Presentation &p, bool narrow)
{
	std::vector<Element> left;
	// Content-sized pills at the left on desktop, stretched segments on touch.
	auto pills = [&](const std::string &key, const std::vector<std::string> &labels, int selected, std::function<void(int)> change)
	{
		if (p.touch)
			return fe::segments(key, labels, selected, change);
		std::vector<Element> buttons;
		for (int i = 0; i < int(labels.size()); ++i)
		{
			fe::ButtonOptions options;
			options.selected = selected == i;
			buttons.push_back(fe::constrained({p.pt(120), 0, fe::Constraints::Unbounded, fe::Constraints::Unbounded},
											  fe::padding(fe::Insets::symmetric(p.pt(8), 0),
														  fe::button(key + "/" + std::to_string(i), labels[std::size_t(i)], [change, i] { change(i); }, options))));
		}
		return fe::row(std::move(buttons), {p.pt(8), fe::CrossAlign::Center, fe::MainAlign::Start});
	};
	left.push_back(pills("map/mode", localized({"Premade maps", "Random map"}), setup.random, [this](int value) { setMapMode(value); }));
	if (!setup.random)
	{
		if (separateMapLibraries)
			left.push_back(pills("map/library", localized({"Built-in maps", "Your maps"}), userMaps,
								 [this](int value)
								 {
									 if (userMaps != bool(value))
									 {
										 userMaps = value;
										 listMaps();
										 if (!librarySelection[userMaps].empty())
											 loadMap(librarySelection[userMaps]);
										 invalidate();
									 }
								 }));
		const auto found = std::find(mapPaths.begin(), mapPaths.end(), librarySelection[userMaps]);
		const int selected = found == mapPaths.end() ? -1 : int(found - mapPaths.begin());
		fe::ListOptions listOptions;
		listOptions.visibleRows = narrow ? 8 : 14;
		listOptions.emptyText = tr("No maps in this library.");
		listOptions.activate = [this](int) { launch(); };
		// Each library keeps its own scroll position.
		left.push_back(fe::listView("map/list/" + std::to_string(userMaps ? 1 : 0), mapNames, selected,
									[this](int i)
									{
										if (i >= 0 && i < int(mapPaths.size()))
											loadMap(mapPaths[std::size_t(i)]);
										invalidate();
									},
									listOptions));
	}
	else
	{
		auto &g = setup.generator;
		auto changed = [this]
		{
			chosenSeed.reset();
			++setup.mapRevision;
			invalidatePreview();
		};
		left.push_back(fe::field(tr("Landscape"), fe::chooser("generator/landscape", tr(GenerationRequest::methodName(g.method)), [this] { chooseLandscape(); })));
		GenerationRequest defaults;
		defaults.setMethodDefaults(g.method);
		const bool atDefaults = g.options == defaults.options && g.wDec == defaults.wDec && g.hDec == defaults.hDec && setup.capacity == defaults.nbTeams;
		left.push_back(fe::wrap({fe::button("generator/reset", tr("Reset to defaults"), [this] { resetParameters(); }, {false, false, !atDefaults}),
								 fe::button("generator/random", tr("Random parameters"), [this] { randomizeParameters(); })},
								{-1, p.pt(150)}));
		auto discrete = [&](const GenerationRequest::Control &c, const std::string &id)
		{
			std::vector<std::string> options;
			for (int v : c.values())
				options.push_back(c.isChoice() ? tr(c.valueLabel(v)) : std::to_string(c.displayValue(v)));
			left.push_back(fe::field(tr(c.label), fe::choice(id, options, c.indexOf(c.get(g)),
															 [this, c, changed](int i)
															 {
																 c.set(setup.generator, c.valueAt(i));
																 if (c.id == "teams")
																	 setup.setCapacity(setup.generator.nbTeams);
																 changed();
															 }),
									 {"", 160}));
		};
		for (const auto &c : GenerationRequest::sharedControls())
			if (c.id != "workers")
			{
				if (c.id == "teams")
					g.nbTeams = setup.capacity;
				discrete(c, c.id == "width" ? "generator/width" : c.id == "height" ? "generator/height" : "generator/colonies");
			}
		left.push_back(fe::caption(tr("Starting workers: Game Rules tab.")));
		for (int section = 0; section < 3; ++section)
		{
			const char *sectionName = section == 0 ? "Terrain" : section == 1 ? "Resources" : "Layout";
			fe::ButtonOptions header;
			header.selected = expanded[section];
			header.alignLeft = true;
			left.push_back(fe::button("generator/section/" + std::to_string(section), std::string(expanded[section] ? "-  " : "+  ") + tr(sectionName),
									  [this, section] { expanded[section] = !expanded[section]; }, header));
			if (!expanded[section])
				continue;
			bool any = false;
			for (const auto &c : GenerationRequest::controls(g.method))
			{
				if (static_cast<int>(c.group) != section)
					continue;
				any = true;
				const std::string id = "generator/" + c.id;
				if (c.isToggle())
				{
					left.push_back(fe::toggle(id, tr(c.label), c.get(g) != 0,
											  [this, c, changed](bool on)
											  {
												  c.set(setup.generator, on ? 1 : 0);
												  changed();
											  }));
					continue;
				}
				if (c.powerOfTwo || !c.allowedValues.empty())
				{
					discrete(c, id);
					continue;
				}
				auto apply = [this, c, changed](int v)
				{
					if (c.get(setup.generator) != c.normalize(v))
					{
						c.set(setup.generator, v);
						changed();
					}
				};
				if ((c.maximum - c.minimum) / c.step > 8)
				{
					fe::SliderOptions options;
					options.valueText = std::to_string(c.get(g));
					options.caption = tr(c.label);
					left.push_back(fe::slider(id, (c.get(g) - c.minimum) / c.step, 0, (c.maximum - c.minimum) / c.step,
											  [c, apply](int i) { apply(c.minimum + i * c.step); }, options));
				}
				else
				{
					fe::StepperOptions options;
					options.step = c.step;
					left.push_back(fe::field(tr(c.label), fe::stepper(id, c.get(g), c.minimum, c.maximum, apply, options), {"", 160}));
				}
			}
			if (!any)
				left.push_back(fe::caption(tr(section == 1 ? "This landscape uses fixed resource placement." : "Dimensions and colony count are set above.")));
		}
	}
	auto leftParts = left;
	auto leftColumn = fe::column(std::move(left), {p.pt(8)});

	std::vector<Element> right;
	right.push_back(narrow ? fe::paragraph(setup.random ? tr(GenerationRequest::methodName(setup.generator.method)) : mapHeader.getMapName())
						   : fe::heading(setup.random ? tr(GenerationRequest::methodName(setup.generator.method)) : mapHeader.getMapName()));
	const std::string dimensions = validMap ? std::to_string(preview->getLastWidth()) + " x " + std::to_string(preview->getLastHeight())
							   : setup.random ? std::to_string(1 << setup.generator.wDec) + " x " + std::to_string(1 << setup.generator.hDec)
											  : "";
	std::vector<Element> infoRow{fe::expanded(fe::caption(dimensions + "  /  " + std::to_string(setup.capacity) + " " + tr("colonies")))};
	// The generated map's start quality: the fairness the lobby ranked its candidate rolls by,
	// and a small (i) that opens the breakdown behind it.
	if (setup.random && (validMap || previewBusy()) && quality.measured)
	{
		char summary[96];
		std::snprintf(summary, sizeof summary, "%s %.2f", tr("Fairness").c_str(), quality.fairness);
		infoRow.push_back(fe::caption(summary));
		infoRow.push_back(p.touch ? fe::compactButton(
										"quality/info", tr("[Start quality]"), fe::UIIcon::Info,
										[this] { showStartQuality(); }, p)
								  : fe::button("quality/info", "i", [this] { showStartQuality(); },
											   {false, false, true, false, false, false,
												SDLK_UNKNOWN, fe::FontRole::Support, 24}));
	}
	right.push_back(fe::row(std::move(infoRow), {p.pt(6), fe::CrossAlign::Center}));
	// A reroll invalidates the launch snapshot, not the image being displayed.
	// Keep its geometry, terrain and instructions until the replacement is ready.
	const bool displayPreview = validMap || (previewBusy() && preview->isThumbnailLoaded());
	const int previewSize = narrow ? 240 : 440;
	if (displayPreview)
	{
		right.push_back(fe::center(fe::mapPreview("map/preview", *preview, previewSize)));
		right.push_back(fe::caption(tr("Map preview controls")));
	}
	else
	{
		fe::CardOptions placeholder;
		placeholder.color = theme().palette.placeholder;
		placeholder.shadow = false;
		placeholder.padding = 0;
		right.push_back(fe::center(fe::card(fe::sized({p.pt(previewSize), p.pt(previewSize)}, fe::empty()), placeholder)));
		right.push_back(fe::caption(tr(previewBusy() ? "Map preview controls" : "Preview unavailable. Adjust settings or start to retry.")));
	}
	if (setup.random)
		right.push_back(fe::center(
			fe::compactButton("map/randomize", tr("Randomize"), fe::UIIcon::Refresh,
							  [this]
							  {
								  // Same settings, new seed: generateMap draws a fresh root seed on every run.
								  invalidatePreview();
								  previewDue = SDL_GetTicks();
							  },
							  p, {false, false, setup.validation().empty() && !previewBusy()})));
	auto rightColumn = fe::column(std::move(right), {p.pt(8)});
	if (narrow)
	{
		// Phones: the map mode first, the preview, then the library, as before.
		if (p.compact() && !leftParts.empty())
		{
			Element mode = leftParts.front();
			leftParts.erase(leftParts.begin());
			return fe::scroll("lobby/map", fe::column({mode, rightColumn, fe::column(std::move(leftParts), {p.pt(8)})}, {p.pt(12)}));
		}
		return fe::scroll("lobby/map", fe::column({rightColumn, leftColumn}, {p.pt(12)}));
	}
	return fe::row({fe::expanded(fe::scroll("lobby/map", leftColumn), 11), fe::expanded(fe::scroll("lobby/map/side", rightColumn), 9)},
				   {p.pt(16), fe::CrossAlign::Stretch});
}

Element CustomGameScreen::playersTab(const Presentation &p, bool narrow)
{
	std::vector<Element> parts;
	const int selectedFormat = setup.format == "FFA" ? 0 : setup.format == "2 vs 2" ? 1 : setup.format == "You vs all" ? 2 : -1;
	parts.push_back(fe::segments("format", localized({"FFA", "2 vs 2", "You vs all"}), selectedFormat, [this](int i) { setup.presetTeams(i); },
								 {true, setup.activeColonies() == 4, bool(setup.humanColony()) && setup.activeColonies() > 1}));
	parts.push_back(fe::caption(std::to_string(setup.controllerCount()) + " / 12 " + tr("controllers")));
	const auto controllerNames = localized({"You", "AI", "You + AI", "Closed"});
	for (int i = 0; i < setup.capacity; ++i)
	{
		auto &c = setup.colonies[i];
		const GAGCore::Color color = i < int(preview->starts.size()) ? preview->starts[std::size_t(i)].color : theme().palette.neutral;
		const std::string id = "colony/" + std::to_string(i);
		std::vector<bool> enabled;
		for (int j = 0; j < 4; ++j)
		{
			auto draft = setup;
			enabled.push_back(draft.setController(i, (CustomGameSetup::Controller)j));
		}
		fe::ChoiceOptions controllerOptions;
		controllerOptions.enabled = enabled;
		controllerOptions.help = tr("Shared control needs a free controller slot (maximum 12).");
		auto controller = fe::choice(id + "/controller", controllerNames, c.controller,
									 [this, i](int value) { setup.setController(i, (CustomGameSetup::Controller)value); }, controllerOptions);
		std::vector<std::string> teams;
		for (int j = 0; j < setup.capacity; ++j)
			teams.push_back(tr("Team") + " " + std::to_string(j + 1));
		auto team = fe::choice(id + "/team", teams, c.alliance,
							   [this, i](int value)
							   {
								   setup.colonies[i].alliance = value;
								   setup.format = "Custom teams";
							   });
		const bool hasAI = c.controller == CustomGameSetup::Computer || c.controller == CustomGameSetup::Shared;
		Element aiControls;
		if (hasAI)
		{
			std::vector<std::string> names;
			for (int j : AINames::selectionOrder())
				names.push_back(AINames::getAISelectorText(j));
			aiControls = fe::row({fe::expanded(fe::choice(id + "/ai", names, AINames::selectionIndex(c.ai),
														  [this, i](int value) { setup.colonies[i].ai = (AI::ImplementationID)AINames::selectionOrder()[std::size_t(value)]; })),
								  fe::button(id + "/info", tr("AI strategy"), [this, i] { showAIProfile(i); })},
								 {p.pt(6), fe::CrossAlign::Center});
		}
		else
			aiControls = fe::caption(tr(c.controller == CustomGameSetup::Human ? "You control this colony." : "Closed"));
		auto identity = fe::row({fe::swatch(color, 30), fe::label(std::to_string(i + 1) + "  " + colorName(color))}, {p.pt(8), fe::CrossAlign::Center});
		const std::string summary = c.controller == CustomGameSetup::Human ? username
									: c.controller == CustomGameSetup::Closed ? tr("This colony will not join the match.")
																			  : AINames::getAISummary(c.ai);
		std::vector<Element> body;
		if (narrow)
			body = {identity, controller, aiControls, team};
		else
			body = {fe::row({fe::width(p.pt(150), identity), fe::width(p.pt(150), controller), fe::expanded(aiControls), fe::width(p.pt(120), team)},
							{p.pt(8), fe::CrossAlign::Center})};
		body.push_back(fe::caption(summary));
		if (c.controller == CustomGameSetup::Shared)
			body.push_back(fe::caption(tr("You and the AI both issue orders. Uses two controller slots.")));
		fe::CardOptions cardOptions;
		cardOptions.shadow = false;
		cardOptions.padding = p.pt(10);
		parts.push_back(fe::card(fe::column(std::move(body), {p.pt(6)}), cardOptions));
	}
	return fe::scroll("lobby/players", fe::column(std::move(parts), {p.pt(8)}));
}

Element CustomGameScreen::ruleControl(int index, const Presentation &p, std::string &help)
{
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
		if (index == 10)
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
		setup.ruleset = "Custom";
	};
	const std::string id = "rule/" + std::to_string(index);
	if (index < 3)
	{
		auto options = index == 0 ? localized({"Conquest or prestige", "Conquest only"})
					   : index == 1 ? localized({"Explore as you play", "Terrain revealed"})
									: localized({"Locked teams", "Can change in game"});
		help = tr(index == 0 ? "Conquest only removes prestige victory; map scripts still apply."
				  : index == 1 ? "Revealed terrain does not reveal all enemy activity."
							   : "Choose whether teams can change during the match.");
		return fe::segments(id, options, index == 0 ? !setup.prestige : index == 1 ? setup.revealed : !setup.locked, apply);
	}
	if (index == 3)
	{
		auto settings = globalContainer->settings;
		std::vector<std::string> options;
		for (int i = 0; i <= Settings::GAME_SPEED_MAXIMUM; ++i)
		{
			settings.gameSpeed = i;
			options.push_back(settings.getGameSpeedText());
		}
		help = tr("Changes the pace of the whole simulation.");
		return fe::choice("rule/speed", options, setup.speed, apply);
	}
	if (index == 4)
	{
		help = tr(setup.random ? "More workers jump-start colony growth. Changes the generated map." : "Premade maps retain their authored starting units.");
		if (!setup.random)
			return fe::caption(tr("Map-defined starting units"));
		const auto &control = GenerationRequest::control(setup.generator.method, "workers");
		return fe::stepper("rule/workers", setup.generator.nbWorkers, control.minimum, control.maximum,
						   [this](int v)
						   {
							   setup.generator.nbWorkers = v;
							   ++setup.mapRevision;
							   setup.ruleset = "Custom";
							   invalidatePreview();
						   });
	}
	if (index == 5 || index == 7 || index == 9)
	{
		auto options = index == 5 ? localized({"Grow normally", "No growth"})
					   : index == 7 ? localized({"Normal construction", "Instant"})
									: localized({"Units get hungry", "No hunger"});
		const bool current = index == 5 ? setup.noResourceGrowth : index == 7 ? setup.instantConstruction : setup.noHunger;
		help = tr(index == 5 ? "Resources never grow or spread across the map."
				  : index == 7 ? "Building sites complete immediately, skipping delivery."
							   : "Units never grow hungry and never starve.");
		return fe::segments(id, options, current, apply);
	}
	if (index == 6 || index == 8)
	{
		auto options = index == 6 ? localized({"Off (today's growth)", "Scarce (2x slower)", "Very scarce (4x slower)", "Extremely scarce (8x slower)"})
								  : localized({"None (today's default)", "Small (+50 each)", "Medium (+150 each)", "Large (+300 each)"});
		help = tr(index == 6 ? "Slows how often resources grow or spread across the map."
							 : "Seeds each team's shared market/exchange resource pool at game start.");
		return fe::choice(id, options, index == 6 ? setup.resourceScarcity : setup.stockpileStart, apply);
	}
	if (index == 10 || index == 12 || index == 13 || index == 14)
	{
		auto options = index == 10 ? localized({"Trains normally", "No upgrades"})
					   : index == 12 ? localized({"Retreats when damaged", "Fearless"})
					   : index == 13 ? localized({"Can die permanently", "No permadeath"})
									 : localized({"Normal combat", "Peaceful mode"});
		const bool current = index == 10 ? setup.unitUpgradesDisabled : index == 12 ? setup.unitsFearless : index == 13 ? setup.permadeathDisabled : setup.peacefulMode;
		help = tr(index == 10 ? "Units still visit schools but never gain a level."
				  : index == 12 ? "Units fight to the death instead of retreating to heal."
				  : index == 13 ? "Units are never permanently lost -- HP just stops at 1."
								: "Disables all combat between every team.");
		return fe::segments(id, options, current, apply);
	}
	if (index == 11 || index == 15)
	{
		auto options = index == 11 ? localized({"Off (today's balance)", "Glass cannon x2", "Glass cannon x3"})
								   : localized({"Off (today's HP)", "Fortress x5", "Fortress x10"});
		help = tr(index == 11 ? "Higher tiers deal more damage but have less HP and armor." : "Higher tiers give every building much more HP.");
		return fe::choice(id, options, index == 11 ? setup.glassCannonLevel : setup.buildingHpLevel, apply);
	}
	if (index == 16)
	{
		help = tr(setup.random ? "Starting units spawn already leveled up. Changes the generated map." : "Premade maps retain their authored starting units.");
		if (!setup.random)
			return fe::caption(tr("Map-defined starting units"));
		return fe::choice("rule/startingLevel", localized({"Standard", "Veteran", "Elite", "Legendary"}), setup.startingUnitLevel,
						  [this](int v)
						  {
							  setup.startingUnitLevel = v;
							  ++setup.mapRevision;
							  setup.ruleset = "Custom";
							  invalidatePreview();
						  });
	}
	const auto &minutes = CustomGameSetup::suddenDeathMinuteChoices;
	const int current = int(std::find(minutes.begin(), minutes.end(), setup.suddenDeathMinutes) - minutes.begin());
	help = tr("Match ends at the timer; highest prestige at that instant wins.");
	return fe::choice("rule/suddenDeath", localized({"Off (no timer)", "30 minutes", "45 minutes", "60 minutes", "90 minutes"}),
					  current < int(minutes.size()) ? current : 0, [apply, minutes](int v) { apply(minutes[std::size_t(v)]); });
}

Element CustomGameScreen::rulesTab(const Presentation &p, bool narrow)
{
	std::vector<Element> parts;
	parts.push_back(fe::paragraph(tr("Try a ruleset, then make it your own.")));
	const auto names = localized({"Standard", "Quick clash", "Open book", "Last colony standing"});
	const auto effects = localized({"Classic colony building", setup.random ? "8 workers / 2x speed" : "Premade: only speed changes (2x)",
									"Start with terrain known", "Win through conquest"});
	std::vector<Element> tiles;
	for (int i = 0; i < 4; ++i)
	{
		fe::ButtonOptions options;
		options.selected = tr(setup.ruleset) == names[std::size_t(i)];
		tiles.push_back(fe::button("ruleset/" + std::to_string(i), names[std::size_t(i)] + "\n" + effects[std::size_t(i)],
								   [this, i]
								   {
									   auto rev = setup.mapRevision;
									   setup.presetRules(i);
									   if (setup.random && rev != setup.mapRevision)
										   invalidatePreview();
								   },
								   options));
	}
	parts.push_back(fe::wrap(std::move(tiles), {-1, p.pt(180)}));
	// Rules are numbered in the order they were added, so a later rule can belong to an earlier
	// category: list them grouped by category, in each category's first-appearance order.
	std::vector<int> order;
	for (int first = 0; first < int(CustomGameSetup::ruleDefinitions.size()); ++first)
	{
		const std::string heading = CustomGameSetup::ruleDefinitions[std::size_t(first)].category;
		bool seen = false;
		for (int earlier = 0; earlier < first; ++earlier)
			seen = seen || heading == CustomGameSetup::ruleDefinitions[std::size_t(earlier)].category;
		if (!seen)
			for (int index = first; index < int(CustomGameSetup::ruleDefinitions.size()); ++index)
				if (heading == CustomGameSetup::ruleDefinitions[std::size_t(index)].category)
					order.push_back(index);
	}
	std::string category;
	for (int index : order)
	{
		const auto definition = CustomGameSetup::ruleDefinitions[std::size_t(index)];
		if (category != definition.category)
		{
			category = definition.category;
			if (category != definition.label)
				parts.push_back(fe::padding({0, p.pt(8), 0, 0}, fe::label(tr(category))));
		}
		std::string help;
		auto control = ruleControl(index, p, help);
		const std::string label = tr(definition.label) + (setup.ruleChanged(index) ? " *" : "");
		fe::FieldOptions fieldOptions;
		fieldOptions.help = help;
		fieldOptions.controlWidth = 360;
		fe::CardOptions cardOptions;
		cardOptions.shadow = false;
		cardOptions.padding = p.pt(10);
		parts.push_back(fe::card(fe::field(label, control, fieldOptions), cardOptions));
	}
	parts.push_back(fe::row({fe::button("rules/restore", tr("Restore standard rules"),
										[this]
										{
											auto rev = setup.mapRevision;
											setup.presetRules(0);
											if (setup.random && rev != setup.mapRevision)
												invalidatePreview();
										}),
							 fe::expanded(fe::caption(tr("* Changed from standard.")))},
							{p.pt(12), fe::CrossAlign::Center}));
	return fe::scroll("lobby/rules", fe::column(std::move(parts), {p.pt(8)}));
}
