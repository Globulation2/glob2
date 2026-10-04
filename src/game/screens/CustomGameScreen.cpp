// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameScreen.h"
#include "ChooseMapScreen.h"
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
#include "RulesetCatalog.h"
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
	const std::string text = Toolkit::getStringTable()->getString("[" + s + "]");
	if (s == "Shared control needs a free controller slot (maximum %0).")
		return FormattableString(text).arg(Team::MAX_COUNT);
	return text;
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
					fe::actions({{"profile/back", tr("Back"), [this] { endExecute(-2); }, false, SDLK_ESCAPE, true, fe::uiIcon(fe::UIIcon::Back)},
								 {"profile/use", useLabel, [this] { use(); }, true, SDLK_RETURN, !choices.empty(), fe::uiIcon(fe::UIIcon::Check)}},
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

void CustomGameScreen::useForRoom(const CustomGameSetup &draft, int tab)
{
	forRoom = true;
	setup = draft;
	for (auto &colony : setup.colonies)
		if (colony.ai == AI::JAVASCRIPT)
		{
			colony.ai = AI::NUMBI;
			colony.aiLibraryId.clear();
		}
	// The editor opens on the room's generated map. A premade or own map chosen here
	// is uploaded by the room (PlatformRoom::usePremadeMap); a random one is generated
	// by the platform for everyone.
	setup.random = true;
	invalidatePreview();
	selectTab(tab);
}

void CustomGameScreen::launch()
{
	if (!setup.validation().empty())
		return;
	if (forRoom)
	{
		if (!setup.random && !validMap)
			return;
		endExecute(OK);
		return;
	}
	// A click during generation waits for the current preview before launching it.
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
		try
		{
			freezeAIs();
		}
		catch (const std::exception &e)
		{
			message = e.what();
			invalidate();
			return;
		}
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
	for (int i = 0; i < gameHeader.getNumberOfPlayers(); ++i)
	{
		gameHeader.setAIConfig(i, "");
		auto &player = gameHeader.getBasePlayer(i);
		if (player.type != BasePlayer::P_LOCAL &&
			BasePlayer::implementationIdFromPlayerType(player.type) == AI::JAVASCRIPT)
		{
			const auto &id = setup.colonies[player.teamNumber].aiLibraryId;
			auto source = frozenAIs.find(id);
			if (source == frozenAIs.end())
				throw std::runtime_error("Custom AI source was not frozen before launch");
			gameHeader.setAIConfig(i, source->second);
		}
	}
	gameHeader.getExperiments().clear();
	Engine::applyLocalExperiments(gameHeader, mapHeader);
	for (int i = 0; i < gameHeader.getNumberOfPlayers(); ++i)
	{
		auto &player = gameHeader.getBasePlayer(i);
		if (player.type != BasePlayer::P_LOCAL)
			player.name = aiLabel(player.teamNumber) + " / " + colonyLabel(player.teamNumber);
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

void CustomGameScreen::repeatCurrentMap()
{
	if (!validMap || previewBusy())
		return;
	std::string path = source;
	if (generatedSnapshot)
	{
		// Freeze the previewed roll before opening the advanced dialog. The
		// repeated result becomes a premade map: rerolling it would lose the
		// source the player just configured. Use portable profile storage.
		auto &files = *Toolkit::getFileManager();
		files.addWriteSubdir("generated");
		path = files.getDir(0) + "/generated/source-" + std::to_string(SDL_GetPerformanceCounter()) + ".map";
		if (!files.writeFileAtomic(path, *generatedSnapshot))
		{
			message = tr("Could not load this map. Choose another map or retry.");
			invalidate();
			return;
		}
	}
	auto dialog = std::make_unique<ChooseMapScreen>("maps", "map", false);
	dialog->editMapParameters(path);
	const bool temporarySource = generatedSnapshot != nullptr;
	screens.push(std::move(dialog), [this, path, temporarySource](GAGGUI::Screen &screen, int result)
	{
		const auto repeated = result == ChooseMapScreen::OK
			? static_cast<ChooseMapScreen &>(screen).getMapHeader().getFileName() : std::string();
		// Cancel or accepting unchanged settings leaves the original draft and
		// preview intact. Only the accepted repeated file needs to outlive us.
		if (temporarySource && repeated != path)
			Toolkit::getFileManager()->remove(path);
		if (result != ChooseMapScreen::OK || repeated == path)
		{
			if (temporarySource && repeated == path)
				Toolkit::getFileManager()->remove(path);
			return;
		}
		setup.random = false;
		quality = {};
		++setup.mapRevision;
		loadMap(repeated);
		invalidate();
	});
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
	if (setup.colonies[colony].ai == AI::JAVASCRIPT)
	{
		message = aiLabel(colony);
		if (aiLibrary)
		{
			try
			{
				message +=
					"\n" + aiLibrary->get(setup.colonies[colony].aiLibraryId).metadata.description;
			}
			catch (const std::exception &e)
			{
				message = e.what();
			}
		}
		invalidate();
		return;
	}
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
	if (!forRoom && !aiLibrary)
	{
		try
		{
			aiStorage = Online::makeUserDirectoryStorage();
			aiLibrary = std::make_unique<Script::Library>(*aiStorage);
		}
		catch (const std::exception &e)
		{
			message = e.what();
		}
	}
	GAGCore::ApplicationHost::customGameReady(validMap && !previewBusy() && setup.validation().empty());
	const bool narrow = p.compact() || p.safe.w < p.pt(720);
	auto speed = globalContainer->settings;
	speed.gameSpeed = setup.speed;
	// Phones keep the Map / Players / Rules flow with Back and Next.
	const bool phoneFlow = p.compact();
	// Short landscape phones: Back and Start join the tab row, leaving the height to the tab.
	const bool topActions = p.shortLandscape() && !phoneFlow;
	const std::vector<std::string> titles = localized(phoneFlow || topActions ? std::vector<std::string>{"Map", "Players", "Rules"}
																			 : std::vector<std::string>{"Map", "Players & Teams", "Game Rules"});
	const std::string rulesetTitle = setup.rulesetTitle(forRoom);
	const std::vector<std::string> details = {
		setup.random ? tr(GenerationRequest::methodName(setup.generator.method)) : mapHeader.getMapName(),
		std::to_string(setup.activeColonies()) + " " + tr("colonies") + " / " + tr(setup.format), rulesetTitle};
	std::vector<Element> tabs;
	for (int i = 0; i < 3; ++i)
	{
		fe::ButtonOptions options;
		options.selected = currentTab == i;
		// Tabs use the body font; large text on a narrow screen would break the titles mid-word.
		if (narrow && p.textGrowth > 1.2)
			options.role = fe::FontRole::Support;
		static constexpr fe::UIIcon tabIcons[] = {fe::UIIcon::Map, fe::UIIcon::Users, fe::UIIcon::Rules};
		options.icon = fe::uiIcon(tabIcons[i]);
		options.iconSize = narrow || topActions ? 20 : 24;
		auto tab = fe::button("tab/" + std::to_string(i), titles[std::size_t(i)], [this, i] { selectTab(i); }, options);
		// Desktop tabs carry their current choice underneath, as before.
		tabs.push_back(fe::expanded(narrow || topActions ? tab : fe::column({tab, fe::caption(details[std::size_t(i)])}, {p.pt(2)})));
	}
	Element body = currentTab == 1 ? playersTab(p, narrow) : currentTab == 2 ? rulesTab(p, narrow) : mapTab(p, narrow);
	std::string error = setup.validation();
	if (!setup.random && !validMap)
		error = tr("Select a valid map.");
	const std::string summary = tr(setup.format) + "  /  " + std::to_string(setup.activeColonies()) + " " + tr("colonies") + "  /  " +
								rulesetTitle + "  /  " + speed.getGameSpeedText();
	const std::string note = error.empty() ? (setup.random && previewBusy() ? tr("Generating preview...") : message) : tr(error);
	bool ready = error.empty();
	if (forRoom)
		ready = setup.validation().empty() && (setup.random || validMap);
	std::string startLabel = !setup.humanColony() ? tr("Watch game")
												: tr(setup.random ? "Play this map" : "Start game");
	fe::UIIcon startIcon = setup.humanColony() ? fe::UIIcon::Start : fe::UIIcon::Watch;
	if (forRoom)
	{
		startLabel = tr("Use in room");
		startIcon = fe::UIIcon::Check;
	}
	const auto backIcon = fe::uiIcon(fe::UIIcon::Back);
	std::vector<fe::MenuAction> footerActions;
	if (phoneFlow)
	{
		const int tab = currentTab;
		footerActions.push_back({"back", tr("Back"), [this, tab] { tab > 0 ? selectTab(tab - 1) : endExecute(CANCEL); }, false, SDLK_ESCAPE, true, backIcon});
		if (tab < 2)
			footerActions.push_back({"start", tr("Next"), [this, tab] { selectTab(tab + 1); }, true, SDLK_RETURN, true, fe::uiIcon(fe::UIIcon::ChevronRight)});
		else
			footerActions.push_back({"start", startLabel, [this] { launch(); }, true, SDLK_RETURN, ready, fe::uiIcon(startIcon)});
	}
	else
	{
		footerActions.push_back({"back", tr("Back"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE, true, backIcon});
		footerActions.push_back({"start", startLabel, [this] { launch(); }, true, SDLK_RETURN, ready, fe::uiIcon(startIcon)});
	}
	auto actionRow = fe::actions(std::move(footerActions), p);
	// Short landscape phones keep their few lines for the tab: the tabs already say it all.
	std::vector<Element> summaryParts{p.shortLandscape() ? nullptr : fe::caption(summary)};
	if (!note.empty())
		summaryParts.push_back(fe::hint(note));
	// Experiments come from Settings, not the lobby, so say which ones this match will carry.
	if (!globalContainer->settings.experiments.empty())
		summaryParts.push_back(fe::hint(tr("Experiments") + ": " + experimentLabelList(globalContainer->settings.experiments)));
	// Desktop: summary at the left, compact Back / Start at the right, as before.
	Element footerColumn = p.touch || narrow ? fe::column({fe::column(std::move(summaryParts), {p.pt(4)}), actionRow}, {p.pt(6)})
								   : fe::row({fe::expanded(fe::column(std::move(summaryParts), {p.pt(4)})), actionRow}, {p.pt(8), fe::CrossAlign::Center});
	fe::CardOptions cardOptions;
	cardOptions.padding = p.pt(narrow ? 10 : 16);
	Element panel;
	if (topActions)
	{
		tabs.push_back(actionRow);
		panel = fe::card(fe::column({fe::row(std::move(tabs), {p.pt(6), fe::CrossAlign::Center}), fe::expanded(body),
									 note.empty() ? nullptr : fe::hint(note)},
									{p.pt(6)}),
						 cardOptions);
	}
	else
		panel = fe::card(fe::column({fe::row(std::move(tabs), {p.pt(6)}), fe::expanded(body), fe::divider(), footerColumn}, {p.pt(8)}), cardOptions);
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
	auto pills = [&](const std::string &key, const std::vector<std::string> &labels, const std::vector<fe::UIIcon> &icons,
					 int selected, std::function<void(int)> change)
	{
		std::vector<fe::IconRef> iconRefs;
		for (auto icon : icons)
			iconRefs.push_back(fe::uiIcon(icon));
		if (p.touch)
			return fe::segments(key, labels, selected, change, {}, iconRefs);
		std::vector<Element> buttons;
		for (int i = 0; i < int(labels.size()); ++i)
		{
			fe::ButtonOptions options;
			options.selected = selected == i;
			options.icon = iconRefs[std::size_t(i)];
			buttons.push_back(fe::constrained({p.pt(120), 0, fe::Constraints::Unbounded, fe::Constraints::Unbounded},
											  fe::padding(fe::Insets::symmetric(p.pt(8), 0),
														  fe::button(key + "/" + std::to_string(i), labels[std::size_t(i)], [change, i] { change(i); }, options))));
		}
		return fe::row(std::move(buttons), {p.pt(8), fe::CrossAlign::Center, fe::MainAlign::Start});
	};
	left.push_back(pills("map/mode", localized({"Premade maps", "Random map"}), {fe::UIIcon::Map, fe::UIIcon::Dice}, setup.random, [this](int value) { setMapMode(value); }));
	if (!setup.random)
	{
		if (separateMapLibraries)
			left.push_back(pills("map/library", localized({"Built-in maps", "Your maps"}), {fe::UIIcon::Tutorial, fe::UIIcon::Player}, userMaps,
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
		if (validMap)
			left.push_back(fe::button("map/parameters", tr("Size and parameters"), [this] { repeatCurrentMap(); },
				{.enabled = !previewBusy(), .flat = true, .alignLeft = true, .icon = fe::uiIcon(fe::UIIcon::Settings)}));
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
		left.push_back(fe::wrap({fe::button("generator/reset", tr("Reset to defaults"), [this] { resetParameters(); },
											{.enabled = !atDefaults, .icon = fe::uiIcon(fe::UIIcon::Reset)}),
								 fe::button("generator/random", tr("Random parameters"), [this] { randomizeParameters(); },
											{.icon = fe::uiIcon(fe::UIIcon::Dice)})},
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
			header.icon = fe::uiIcon(section == 0 ? fe::UIIcon::Terrain : section == 1 ? fe::UIIcon::Resources : fe::UIIcon::Layout);
			// The chevron at the right edge shows whether the section is open.
			auto chevron = fe::uiIcon(expanded[section] ? fe::UIIcon::ChevronDown : fe::UIIcon::ChevronRight);
			left.push_back(fe::stack({fe::button("generator/section/" + std::to_string(section), tr(sectionName),
												 [this, section] { expanded[section] = !expanded[section]; }, header),
									  fe::padding(fe::Insets::symmetric(p.pt(10), 0),
												  fe::row({fe::expanded(fe::spacer()), fe::icon(chevron)}, {0, fe::CrossAlign::Center}))}));
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
			// Repetition transforms the completed roll, not the landscape's
			// construction settings. Keep it after the advanced layout fields.
			if (section == 2 && !forRoom)
				left.push_back(fe::button("generator/repeat", tr("Size and parameters"), [this] { repeatCurrentMap(); },
					{.enabled = validMap && !previewBusy(), .flat = true, .alignLeft = true, .icon = fe::uiIcon(fe::UIIcon::Settings)}));
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
										"quality/info", tr("Start quality"), fe::UIIcon::Info,
										[this] { showStartQuality(); }, p)
								  : fe::button("quality/info", "", [this] { showStartQuality(); },
											   {.flat = true, .role = fe::FontRole::Support, .minHeight = 24,
												.tooltip = tr("Start quality"), .accessibleLabel = tr("Start quality"),
												.icon = fe::uiIcon(fe::UIIcon::Info)}));
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
							  p, {.enabled = setup.validation().empty() && !previewBusy(), .icon = fe::uiIcon(fe::UIIcon::Refresh)})));
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

CustomGameScreen::ColonyFields CustomGameScreen::colonyFields(int i, const Presentation &p)
{
	auto &c = setup.colonies[i];
	const auto controllerNames = localized({"You", "AI", "You + AI", "Closed"});
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
	controllerOptions.help = tr("Shared control needs a free controller slot (maximum %0).");
	for (auto icon : {fe::UIIcon::Player, fe::UIIcon::Robot, fe::UIIcon::Users, fe::UIIcon::Lock})
		controllerOptions.icons.push_back(fe::uiIcon(icon));
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
		const auto names = aiChoices();
		const int selectedAI = aiSelection(i);
		// The choice and its strategy button share a line only when both fit.
		aiControls = fe::adaptive(
			[this, i, id, names, selectedAI, p](const fe::LayoutContext &,
												fe::Size available) -> Element
			{
				auto ai = fe::choice(id + "/ai", names, selectedAI,
									 [this, i](int value) { selectAI(i, value); });
				// Touch: an icon-only button, leaving the AI name room on one line.
				auto info = fe::compactButton(id + "/info", tr("AI strategy"), fe::UIIcon::Info,
											  [this, i] { showAIProfile(i); }, p, {.icon = fe::uiIcon(fe::UIIcon::Info)});
				if (available.w < p.textPt(300))
					return fe::column({ai, info}, {p.pt(6)});
				return fe::row({fe::expanded(ai), info}, {p.pt(6), fe::CrossAlign::Center});
			});
	}
	else
		aiControls = fe::caption(tr(c.controller == CustomGameSetup::Human ? "You control this colony." : "Closed"));
	auto identity = fe::row({fe::swatch(color, 30), fe::expanded(fe::label(std::to_string(i + 1) + "  " + colorName(color)))},
							{p.pt(8), fe::CrossAlign::Center});
	return {identity, controller, aiControls, team};
}

Element CustomGameScreen::playersTab(const Presentation &p, bool narrow)
{
	std::vector<Element> parts;
	const int selectedFormat = setup.format == "FFA" ? 0 : setup.format == "2 vs 2" ? 1 : setup.format == "You vs all" ? 2 : -1;
	parts.push_back(fe::segments("format", localized({"FFA", "2 vs 2", "You vs all"}), selectedFormat, [this](int i) { setup.presetTeams(i); },
								 {true, setup.activeColonies() == 4, bool(setup.humanColony()) && setup.activeColonies() > 1},
								 {fe::uiIcon(fe::UIIcon::FreeForAll), fe::uiIcon(fe::UIIcon::Users), fe::uiIcon(fe::UIIcon::Crown)}));
	parts.push_back(fe::caption(std::to_string(setup.controllerCount()) + " / " + std::to_string(Team::MAX_COUNT) + " " + tr("controllers")));
	for (int i = 0; i < setup.capacity; ++i)
	{
		auto &c = setup.colonies[i];
		const std::string summary =
			c.controller == CustomGameSetup::Human ? username
			: c.controller == CustomGameSetup::Closed
				? tr("This colony will not join the match.")
				: (c.ai == AI::JAVASCRIPT ? aiLabel(i) : AINames::getAISummary(c.ai));
		// Side by side when the offered width holds the columns at the current
		// text size (the desktop page is narrower than the window), else stacked.
		auto layout = [this, i, p](bool stacked) -> Element
		{
			auto f = colonyFields(i, p);
			if (stacked)
				return fe::column({f.identity, f.controller, f.ai, f.team}, {p.pt(6)});
			return fe::row({fe::width(p.textPt(150), f.identity), fe::width(p.textPt(150), f.controller), fe::expanded(f.ai),
							fe::width(p.textPt(120), f.team)},
						   {p.pt(8), fe::CrossAlign::Center});
		};
		std::vector<Element> body{narrow ? layout(true)
										 : fe::adaptive([layout, p](const fe::LayoutContext &, fe::Size available)
														{ return layout(available.w < p.textPt(560)); })};
		body.push_back(fe::caption(summary));
		if (c.controller == CustomGameSetup::Shared)
			body.push_back(fe::caption(tr("You and the AI both issue orders. Uses two controller slots.")));
		fe::CardOptions cardOptions;
		cardOptions.shadow = false;
		cardOptions.padding = p.pt(10);
		parts.push_back(fe::card(fe::column(std::move(body), {p.pt(6)}), cardOptions));
	}
	if (!forRoom)
		parts.push_back(fe::caption(tr("Add JavaScript AIs in Settings → Custom AIs. They appear in each colony’s AI selector.")));
	return fe::scroll("lobby/players", fe::column(std::move(parts), {p.pt(8)}));
}

// Game Rules tab ----------------------------------------------------------------------

namespace
{
using CustomGameRules::Group;
using CustomGameRules::Kind;
using CustomGameRules::Rule;

// A ruleset as a tappable card: its name over its one-line description. As the narrow
// layouts' chooser it gains a disclosure mark, since it opens the list of rulesets.
Element rulesetCard(const std::string &key, const Ruleset &ruleset, bool selected, bool chooser,
					std::function<void()> action, const Presentation &p)
{
	const std::string name = fe::tr(ruleset.name), description = fe::tr(ruleset.description);
	fe::ButtonOptions options;
	options.selected = selected;
	options.alignLeft = true;
	options.minHeight = 1;
	options.accessibleLabel = chooser ? tr("Ruleset") + ": " + name + ", " + description : name + ", " + description;
	Element text = fe::column({fe::label(name), fe::caption(description)}, {p.pt(2)});
	// The selection is marked by more than its fill colour.
	Element mark = chooser ? fe::label("›") : selected ? fe::icon(fe::uiIcon(fe::UIIcon::Check), {16}) : nullptr;
	auto content = fe::padding(fe::Insets::symmetric(p.pt(10), p.pt(5)),
							   fe::row({fe::expanded(text), mark}, {p.pt(8), fe::CrossAlign::Center}));
	return fe::stack({fe::button(key, "", std::move(action), options), content});
}

std::vector<std::string> optionTexts(const Rule &rule)
{
	std::vector<std::string> texts;
	for (int i = 0; i < int(rule.optionIds.size()); ++i)
		texts.push_back(CustomGameRules::optionText(rule, i));
	return texts;
}

fe::UIIcon groupIcon(Group group)
{
	switch (group)
	{
	case Group::Match:
		return fe::UIIcon::Trophy;
	case Group::Start:
		return fe::UIIcon::Campaign;
	case Group::Economy:
		return fe::UIIcon::Economy;
	case Group::Combat:
		break;
	}
	return fe::UIIcon::Combat;
}

// A paragraph led by the warning icon, in the warning colour.
Element warning(const std::string &text, const Presentation &p, const fe::Theme &theme)
{
	fe::TextOptions options{fe::FontRole::Support};
	options.color = theme.palette.warning;
	return fe::row({fe::icon(fe::uiIcon(fe::UIIcon::Warning)), fe::expanded(fe::paragraph(text, options))},
				   {p.pt(8), fe::CrossAlign::Center});
}
} // namespace

Element RulesetChoiceScreen::build(const Presentation &p)
{
	const auto &catalog = RulesetCatalog::shipped();
	std::vector<Element> cards;
	for (int i = 0; i < int(catalog.rulesets.size()); ++i)
	{
		const auto &ruleset = catalog.rulesets[std::size_t(i)];
		cards.push_back(rulesetCard("ruleset/" + ruleset.id, ruleset, ruleset.id == selected, false,
									[this, i] { endExecute(i); }, p));
	}
	return fe::page(tr("Choose a ruleset"), fe::scroll("rulesets/list", fe::column(std::move(cards), {p.pt(6)})),
					fe::actions({{"rulesets/back", tr("Back"), [this] { endExecute(-2); }, false, SDLK_ESCAPE, true, fe::uiIcon(fe::UIIcon::Back)}}, p), p, 720);
}

void RulesetChoiceScreen::onTimer(Uint32)
{
	// Once laid out, open on the current ruleset: in view and focused, so Return keeps it.
	if (!revealed)
	{
		revealed = true;
		host().layoutIfNeeded();
		host().scrollIntoView("ruleset/" + selected);
		host().focus("ruleset/" + selected, true);
	}
}

void CustomGameScreen::selectRuleset(const std::string &id)
{
	if (setup.applyRuleset(id) && setup.random)
		invalidatePreview();
	invalidate();
}

void CustomGameScreen::setRulesView(RulesView view)
{
	rulesView = view;
	invalidate();
}

void CustomGameScreen::chooseRuleset()
{
	// Pushed, not blocking-executed, like the AI profile chooser (browser host).
	screens.push(std::make_unique<RulesetChoiceScreen>(setup.rulesetId),
				 [this](GAGGUI::Screen &, int result)
				 {
					 const auto &rulesets = RulesetCatalog::shipped().rulesets;
					 if (result >= 0 && result < int(rulesets.size()))
						 selectRuleset(rulesets[std::size_t(result)].id);
				 });
}

void CustomGameScreen::setRuleValue(const Rule &rule, int value)
{
	if (setup.setRule(rule, value) && setup.random)
		invalidatePreview();
}

Element CustomGameScreen::ruleRow(const Rule &rule, const Presentation &p, bool help, bool resetColumn)
{
	const std::string key = std::string("rule/") + rule.id;
	const std::string label = tr(rule.label);
	const int value = setup.ruleValue(rule);
	const bool applies = CustomGameRules::appliesTo(rule, setup);
	// Combat off makes the combat tuning moot; the Combat row says so once, the rest dim.
	const bool enabled = !rule.needsCombat || !setup.peacefulMode;
	const bool combatOff = std::string_view(rule.id) == "combat" && setup.peacefulMode;
	std::string note = combatOff ? tr("Combat is off, so the rules below have no effect.") : help ? tr(rule.help) : std::string();
	if (!applies && help)
		note = tr("Premade maps retain their authored starting units.");

	auto change = [this, &rule](int v) { setRuleValue(rule, v); };
	Element control, narrowControl;
	if (!applies)
		control = fe::caption(tr("Map-defined starting units"));
	else if (rule.kind == Kind::Toggle)
		control = fe::toggle(key, label, value != 0, [this, &rule](bool on) { setRuleValue(rule, on); }, enabled);
	else if (rule.kind == Kind::Stepper)
	{
		fe::StepperOptions options;
		options.enabled = enabled;
		control = fe::stepper(key, value, CustomGameRules::minimum(rule, setup), CustomGameRules::maximum(rule, setup), change,
							  options);
	}
	else
	{
		fe::ChoiceOptions options;
		options.controlEnabled = enabled;
		auto choice = fe::choice(key, optionTexts(rule), value, change, options);
		// Segments read best when every option fits on a line; a narrow slot takes the menu.
		control = rule.kind == Kind::Segments
					  ? fe::segments(key, optionTexts(rule), value, change, std::vector<bool>(rule.optionIds.size(), enabled))
					  : choice;
		narrowControl = choice;
	}

	// Changed from the chosen ruleset: say so in words (not only colour), and offer a reset.
	Element changedNote, reset;
	if (setup.ruleChanged(rule, forRoom))
	{
		const auto &base = setup.baseRuleset();
		const std::string original = CustomGameRules::optionText(rule, base.value(rule, setup));
		fe::TextOptions marked{fe::FontRole::Support};
		marked.color = theme().palette.warning;
		changedNote = fe::paragraph(FormattableString(tr("Changed from %0: %1")).arg(fe::tr(base.name)).arg(original), marked);
		fe::ButtonOptions options;
		options.flat = !p.touch;
		options.tooltip = FormattableString(tr("Reset to %0")).arg(original);
		options.accessibleLabel = label + ": " + options.tooltip;
		auto action = [this, &rule] { setRuleValue(rule, setup.baseRuleset().value(rule, setup)); };
		options.icon = fe::uiIcon(fe::UIIcon::Reset);
		// Touch: a 48-point icon; pointer hosts name the action beside it.
		if (p.touch)
		{
			options.minHeight = 48;
			reset = fe::button(key + "/reset", "", action, options);
		}
		else
			reset = fe::button(key + "/reset", tr("Reset"), action, options);
	}
	const int resetWidth = p.touch ? p.pt(48) : p.textPt(70) + p.pt(26);
	Element noteText = note.empty() ? nullptr : fe::hint(note);
	const bool toggle = rule.kind == Kind::Toggle && applies;

	return fe::adaptive(
		[=](const fe::LayoutContext &, fe::Size available) -> Element
		{
			// Every row keeps the reset column while any rule is changed, so controls align.
			Element resetSlot = resetColumn ? fe::width(resetWidth, reset ? reset : fe::spacer()) : nullptr;
			if (toggle)
			{
				// Notes line up with the toggle's text, past its box.
				auto notes = fe::padding({p.textPt(30), 0, 0, 0}, fe::column({noteText, changedNote}, {p.pt(2)}));
				return fe::row({fe::expanded(fe::column({control, noteText || changedNote ? notes : nullptr}, {p.pt(2)})), resetSlot},
							   {p.pt(8), fe::CrossAlign::Center});
			}
			Element labelBlock = fe::column({fe::paragraph(label), noteText, changedNote}, {p.pt(2)});
			// Label and control side by side when both fit; otherwise the control goes under
			// its label, and segments give way to a menu if they would wrap.
			const int controlWidth = std::min(p.textPt(270), available.w / 2);
			if (available.w < p.textPt(460))
			{
				Element wide = narrowControl && available.w < p.textPt(320) ? narrowControl : control;
				return fe::column({labelBlock, fe::row({fe::expanded(wide), resetSlot}, {p.pt(8), fe::CrossAlign::Center})}, {p.pt(4)});
			}
			Element fitted = narrowControl && controlWidth < p.textPt(260) ? narrowControl : control;
			// The margin keeps wrapped help clear of the control beside it.
			return fe::row({fe::expanded(fe::padding({0, 0, p.pt(8), 0}, labelBlock)), fe::width(controlWidth, fitted), resetSlot},
						   {p.pt(8), fe::CrossAlign::Center});
		});
}

Element CustomGameScreen::rulesTab(const Presentation &p, bool narrow)
{
	const auto &catalog = RulesetCatalog::shipped();
	const auto &base = setup.baseRuleset();
	const bool changed = !setup.rulesetDiff(forRoom).empty();
	// Wide layouts list every ruleset beside the rules; narrower ones open the list as a screen.
	const bool rail = !narrow && !p.shortLandscape() && p.safe.w >= p.pt(900);
	const bool sidePanel = p.shortLandscape();
	const bool all = rulesView == RulesView::All;
	// Narrow All rules: one group at a time, so a phone never scrolls through every rule.
	const bool groupFilter = all && (narrow || sidePanel);

	// The rules, grouped. Summary shows the Match rules and any rule set away from Standard or
	// from the chosen ruleset; All rules shows every rule.
	std::vector<Element> groups;
	int hidden = 0;
	for (Group group : CustomGameRules::groups)
	{
		if (groupFilter && group != rulesGroup)
			continue;
		// A filtered layout's group menu already names the group.
		std::vector<Element> rows{groupFilter ? nullptr
											  : fe::row({fe::icon(fe::uiIcon(groupIcon(group)), {24}), fe::heading(tr(CustomGameRules::groupLabel(group)))},
														{p.pt(8), fe::CrossAlign::Center})};
		for (const auto &rule : CustomGameRules::rules())
		{
			if (rule.group != group || (forRoom && rule.inRooms == CustomGameRules::InRooms::Hidden))
				continue;
			if (!all && group != Group::Match && !setup.ruleNonStandard(rule, forRoom) && !setup.ruleChanged(rule, forRoom))
			{
				++hidden;
				continue;
			}
			rows.push_back(ruleRow(rule, p, all && !narrow, changed));
		}
		if (rows.size() > 1)
			groups.push_back(fe::column(std::move(rows), {p.pt(6)}));
	}
	// Groups stack with more space between them than between rows.
	auto stackGroups = [p](std::vector<Element> blocks) { return fe::column(std::move(blocks), {p.pt(18)}); };
	std::vector<Element> body;
	if (!setup.prestige && setup.peacefulMode && setup.suddenDeathMinutes == 0 && setup.winProbabilityPermille == 0)
		body.push_back(warning(tr("This match cannot end. Turn on prestige victory or set a time limit."), p, theme()));
	else if (setup.suddenDeathMinutes == 30)
		body.push_back(warning(tr("Prestige usually appears after 13 to 28 minutes, so a 30-minute limit often ends in a tie."), p, theme()));
	if (!setup.random && std::any_of(base.values.begin(), base.values.end(), [](const auto &v) { return v.first->affectsMap; }))
		body.push_back(fe::hint(tr("Premade maps retain their authored starting units.")));
	if (all && !groupFilter)
		// Match and Economy at the left, Start and Combat at the right, when two columns fit.
		body.push_back(fe::adaptive(
			[groups, stackGroups, p](const fe::LayoutContext &, fe::Size available) -> Element
			{
				if (available.w < 2 * p.textPt(460) || groups.size() < 2)
					return stackGroups(groups);
				std::vector<Element> left, right;
				for (std::size_t i = 0; i < groups.size(); ++i)
					(i % 2 ? right : left).push_back(groups[i]);
				return fe::row({fe::expanded(stackGroups(left)), fe::expanded(stackGroups(right))}, {p.pt(24), fe::CrossAlign::Start});
			}));
	else
		body.push_back(stackGroups(std::move(groups)));
	if (!all && hidden > 0)
		body.push_back(fe::row({fe::expanded(fe::hint(FormattableString(tr("%0 more rules are at their Standard values.")).arg(hidden))),
								fe::button("rules/all", tr("Show all rules"), [this] { setRulesView(RulesView::All); },
										   {.icon = fe::uiIcon(fe::UIIcon::AllRules)})},
							   {p.pt(8), fe::CrossAlign::Center}));
	auto rulesList = [&body, &p] { return fe::scroll("lobby/rules", fe::column(body, {p.pt(8)})); };

	auto resetAll = [&]
	{
		fe::ButtonOptions options;
		options.flat = !p.touch;
		options.icon = fe::uiIcon(fe::UIIcon::Reset);
		return fe::button("rules/reset", FormattableString(tr("Reset to %0")).arg(fe::tr(base.name)),
						  [this] { selectRuleset(setup.rulesetId); }, options);
	};
	auto setView = [this](int v) { setRulesView(v == 0 ? RulesView::Summary : RulesView::All); };
	const auto views = localized({"Summary", "All rules"});
	const std::vector<fe::IconRef> viewIcons{fe::uiIcon(fe::UIIcon::Summary), fe::uiIcon(fe::UIIcon::AllRules)};

	if (rail)
	{
		std::vector<Element> cards{fe::caption(tr("Ruleset"))};
		for (const auto &ruleset : catalog.rulesets)
			cards.push_back(rulesetCard("ruleset/" + ruleset.id, ruleset, ruleset.id == base.id, false,
										[this, id = ruleset.id] { selectRuleset(id); }, p));
		auto header = fe::row({fe::expanded(fe::heading(setup.rulesetTitle(forRoom))), changed ? resetAll() : nullptr,
							   fe::width(p.textPt(240) + p.pt(52), fe::segments("rules/view", views, int(rulesView), setView, {}, viewIcons))},
							  {p.pt(10), fe::CrossAlign::Center});
		// Summary rows stay near their labels instead of spanning the whole window.
		Element list = rulesList();
		Element rules = all ? list
							: fe::adaptive([list, p](const fe::LayoutContext &, fe::Size available)
										   { return fe::row({fe::width(std::min(available.w, p.textPt(760)), list), fe::expandedSpacer()},
															{0, fe::CrossAlign::Stretch}); });
		return fe::row({fe::width(p.textPt(250), fe::scroll("rules/rulesets", fe::column(std::move(cards), {p.pt(6)}))),
						fe::expanded(fe::column({header, fe::expanded(rules)}, {p.pt(8)}))},
					   {p.pt(16), fe::CrossAlign::Stretch});
	}

	std::vector<Element> controls{fe::caption(tr("Ruleset")),
								  rulesetCard("rules/ruleset", base, false, true, [this] { chooseRuleset(); }, p)};
	if (changed)
		controls.push_back(fe::row({fe::expanded(fe::caption(setup.rulesetTitle(forRoom))), resetAll()}, {p.pt(8), fe::CrossAlign::Center}));
	Element groupChoice;
	if (groupFilter)
	{
		std::vector<std::string> names;
		fe::ChoiceOptions groupOptions;
		for (Group group : CustomGameRules::groups)
		{
			names.push_back(tr(CustomGameRules::groupLabel(group)));
			groupOptions.icons.push_back(fe::uiIcon(groupIcon(group)));
		}
		groupChoice = fe::choice("rules/group", names, int(rulesGroup),
								 [this](int g)
								 {
									 rulesGroup = CustomGameRules::groups[std::size_t(g)];
									 invalidate();
								 },
								 groupOptions);
	}
	if (sidePanel)
	{
		// Short landscape phones: the ruleset and view at the left, the rules beside them. The
		// view is a menu here; two segments would wrap in the narrow pane.
		fe::ChoiceOptions viewOptions;
		viewOptions.icons = viewIcons;
		controls.push_back(fe::choice("rules/view", views, int(rulesView), setView, viewOptions));
		controls.push_back(groupChoice);
		const int side = std::min(p.textPt(230), p.safe.w * 2 / 5);
		return fe::row({fe::width(side, fe::scroll("rules/controls", fe::column(std::move(controls), {p.pt(6)}))), fe::expanded(rulesList())},
					   {p.pt(12), fe::CrossAlign::Stretch});
	}
	auto viewSegments = fe::segments("rules/view", views, int(rulesView), setView, {}, viewIcons);
	// The group menu shares the view's row when both fit, else goes under it.
	if (groupChoice && p.safe.w < p.textPt(480))
	{
		controls.push_back(viewSegments);
		controls.push_back(groupChoice);
	}
	else
		controls.push_back(fe::row({fe::expanded(viewSegments), groupChoice ? fe::width(p.textPt(140), groupChoice) : nullptr},
								   {p.pt(8), fe::CrossAlign::Center}));
	// A short window cannot spare a fixed header: the ruleset scrolls away with the rules.
	if (p.safe.h < p.pt(640))
	{
		for (auto &part : body)
			controls.push_back(std::move(part));
		return fe::scroll("lobby/rules", fe::column(std::move(controls), {p.pt(8)}));
	}
	controls.push_back(fe::expanded(rulesList()));
	return fe::column(std::move(controls), {p.pt(6)});
}

std::vector<std::string> CustomGameScreen::aiChoices() const
{
	std::vector<std::string> names;
	for (int id : AINames::selectionOrder())
		names.push_back(AINames::getAISelectorText(id));
	if (!forRoom && aiLibrary)
		for (const auto &entry : aiLibrary->entries())
			names.push_back(entry.metadata.name + " · JavaScript #" + entry.id);
	return names;
}
int CustomGameScreen::aiSelection(int colony) const
{
	const auto &c = setup.colonies[colony];
	if (c.ai != AI::JAVASCRIPT)
		return AINames::selectionIndex(c.ai);
	if (!forRoom && aiLibrary)
		for (std::size_t i = 0; i < aiLibrary->entries().size(); ++i)
			if (aiLibrary->entries()[i].id == c.aiLibraryId)
				return int(AINames::selectionOrder().size() + i);
	return -1;
}
void CustomGameScreen::selectAI(int colony, int selection)
{
	if (selection < 0)
		return;
	auto &c = setup.colonies[colony];
	const auto builtin = AINames::selectionOrder().size();
	if (std::size_t(selection) < builtin)
	{
		c.ai = AI::ImplementationID(AINames::selectionOrder()[selection]);
		c.aiLibraryId.clear();
	}
	else if (!forRoom && aiLibrary &&
			 std::size_t(selection) - builtin < aiLibrary->entries().size())
	{
		c.ai = AI::JAVASCRIPT;
		c.aiLibraryId = aiLibrary->entries()[selection - builtin].id;
	}
	frozenAIs.clear();
}
std::string CustomGameScreen::aiLabel(int colony) const
{
	const auto &c = setup.colonies[colony];
	if (c.ai != AI::JAVASCRIPT)
		return AINames::getAIText(c.ai);
	try
	{
		if (aiLibrary)
			return aiLibrary->get(c.aiLibraryId).metadata.name;
	}
	catch (...)
	{
	}
	return tr("Missing custom AI");
}
void CustomGameScreen::freezeAIs()
{
	bool selected = false;
	for (int i = 0; i < setup.capacity; ++i)
		selected |= (setup.colonies[i].controller == CustomGameSetup::Computer ||
			setup.colonies[i].controller == CustomGameSetup::Shared) &&
			setup.colonies[i].ai == AI::JAVASCRIPT;
	if (!selected)
	{
		frozenAIs.clear();
		return;
	}
	if (!aiLibrary)
	{
		aiStorage = Online::makeUserDirectoryStorage();
		aiLibrary = std::make_unique<Script::Library>(*aiStorage);
	}
	std::map<std::string, std::string> sources;
	for (int i = 0; i < setup.capacity; ++i)
	{
		const auto &c = setup.colonies[i];
		if ((c.controller == CustomGameSetup::Computer ||
			 c.controller == CustomGameSetup::Shared) &&
			c.ai == AI::JAVASCRIPT && !sources.contains(c.aiLibraryId))
			sources.emplace(c.aiLibraryId, aiLibrary->configuration(c.aiLibraryId));
	}
	frozenAIs = std::move(sources);
}
