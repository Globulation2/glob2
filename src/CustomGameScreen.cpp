// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameScreen.h"
#include "CustomGamePreferences.h"
#include "AINames.h"
#include "GUIMapPreview.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "LobbyMapCatalog.h"
#include "MapGenerator.h"
#include "Player.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
std::string tr(const std::string &s) { return Toolkit::getStringTable()->getString("[" + s + "]"); }
const Uint32 A = ALIGN_SCREEN_CENTERED;
const std::vector<std::string> methods = []
{
	std::vector<std::string> result;
	for (int m = MapGenerationDescriptor::eSWAMP; m <= MapGenerationDescriptor::eOLDISLANDS; ++m)
		result.emplace_back(
			MapGenerationDescriptor::methodName(static_cast<MapGenerationDescriptor::Method>(m)));
	return result;
}();
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
		std::string content = this->profiles ? AINames::getAIProfile(AINames::selectionOrder()[this->selected]) : "";
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

class LobbyMapPreview : public MapPreview
{
  public:
	struct Start
	{
		int x, y;
		Color color;
	};
	std::vector<Start> starts;
	LobbyMapPreview() : MapPreview(430, 115, A, A) { w = h = 180; }
	void paint() override
	{
		if (!surface)
			return;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		auto target = parent->getSurface();
		target->drawSurface(x, y, w, h, surface);
		auto font = Toolkit::getFont("standard");
		for (size_t i = 0; i < starts.size(); ++i)
		{
			int px = x + std::clamp(starts[i].x * w / std::max(1, getLastWidth()), 10, w - 18);
			int py = y + std::clamp(starts[i].y * h / std::max(1, getLastHeight()), 10, h - 18);
			target->drawFilledRect(px - 2, py - 2, 20, 20, 20, 30, 20);
			target->drawFilledRect(px, py, 16, 16, starts[i].color);
			font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(0, 0, 0)));
			target->drawString(px + 3, py, font, std::to_string(i + 1));
			font->popStyle();
		}
	}
};
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
		{"Red", 255, 0, 0},		 {"Yellow", 255, 255, 0}, {"Lime", 128, 255, 0},
		{"Teal", 0, 255, 128},	 {"Azure", 0, 128, 255},  {"Green", 0, 255, 0},
		{"Cyan", 0, 255, 255},	 {"Blue", 0, 0, 255},	  {"Magenta", 255, 0, 255},
		{"Orange", 255, 128, 0}, {"Purple", 128, 0, 255}, {"White", 255, 255, 255},
		{"Gray", 128, 128, 128}, {"Brown", 128, 64, 0},	  {"Pink", 255, 128, 192}};
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
		std::copy(std::begin(preferences.expanded), std::end(preferences.expanded), expanded);
		listMaps();
		if (setup.random)
			invalidate();
		else
			loadMap(setup.premadeMap);
		// The visible library may differ from the selected map (e.g. an empty library).
		std::copy(std::begin(preferences.librarySelection), std::end(preferences.librarySelection), librarySelection);
	}
	else
	{
		listMaps();
		for (const auto &p : mapPaths)
			if (std::filesystem::path(p).filename() == "FourSquares1.map")
			{
				loadMap(p);
				break;
			}
		if (!validMap || setup.capacity != 4)
		{
			setup.random = true;
			setup.setCapacity(4);
			validMap = false;
			source.clear();
			invalidate();
		}
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
	std::copy(std::begin(librarySelection), std::end(librarySelection), preferences.librarySelection);
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
void CustomGameScreen::onGroupActivated(int group) { currentTab = group; }
void CustomGameScreen::invalidate()
{
	validMap = false;
	previewRevision = ~0u;
	previewPending = setup.random;
	previewDue = SDL_GetTicks() + 500;
	message = tr("Updating map preview...");
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
		BinaryInputStream stream(Toolkit::getFileManager()->openInputStreamBackend(path));
		MapHeader header;
		if (!stream.isValid() || !header.load(&stream) ||
			(header.getNumberOfTeams() < 1 || header.getNumberOfTeams() > Team::MAX_COUNT))
			throw std::runtime_error("map header");
		int old = setup.capacity;
		auto world = std::make_unique<Game>(nullptr);
		BinaryInputStream body(Toolkit::getFileManager()->openInputStreamBackend(path));
		if (!world->load(&body))
			throw std::runtime_error("map body");
		mapHeader = header;
		setup.setCapacity(header.getNumberOfTeams());
		source = path;
		validMap = true;
		preview->setMapThumbnail(path);
		preview->starts.clear();
		for (int i = 0; i < world->teamsCount(); ++i)
			preview->starts.push_back(
				{world->teams[i]->startPosX, world->teams[i]->startPosY, world->teams[i]->color});
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
		MapGenerator generator;
		setup.generator.nbTeams = setup.capacity;
		// Some rolls cannot fit every starting colony. Retry those rolls before
		// reporting failure; only the successful world becomes the preview.
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			game = std::make_unique<Game>(nullptr);
			if (generator.generateMap(*game, setup.generator) &&
				game->teamsCount() == setup.capacity)
				break;
			game.reset();
		}
		if (!game)
			throw std::runtime_error("generation");
		GameHeader initial;
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
		if (!loadMap(candidate))
			throw std::runtime_error("snapshot load");
		if (!snapshot.empty())
			std::filesystem::remove_all(std::filesystem::path(snapshot).parent_path());
		snapshot = candidate;
		previewRevision = setup.mapRevision;
		message = tr("Preview ready. Start plays this exact map.");
		return true;
	}
	catch (const std::exception &)
	{
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
	if (controls->pressed.empty() && !controls->popup.open && Sint32(tick - preferencesRetryAt) >= 0)
		savePreferences();
	// Wait until the last edit settles and a dragged control/menu is released.
	if (previewPending && setup.random && Sint32(tick - previewDue) >= 0 &&
		controls->pressed.empty() && !controls->popup.open)
		generateMap();
}
void CustomGameScreen::setMapMode(bool random)
{
	if (setup.random == random)
		return;
	setup.random = random;
	previewPending = false;
	validMap = false;
	if (random)
	{
		setup.setCapacity(setup.generator.nbTeams);
		invalidate();
	}
	else if (!setup.premadeMap.empty())
		loadMap(setup.premadeMap);
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
		setup.random ? tr(methods[setup.generator.method - 1]) : mapHeader.getMapName(),
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
	int selected = setup.format == "FFA"		  ? 0
				   : setup.format == "2 vs 2"	  ? 1
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
			ui.dropdown(id + "/ai", {ax, ry + 8, aw, 30}, names, AINames::selectionIndex(c.ai), [this, i](int value)
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
	std::string category;
	for (int index = 0; index < 6; ++index)
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
				setup.suddenDeathMinutes = value;
			setup.ruleset = "Custom";
		};
		std::string help;
		if (index < 3)
		{
			auto options = index == 0	? localized({"Conquest or prestige", "Conquest only"})
						   : index == 1 ? localized({"Explore as you play", "Terrain revealed"})
										: localized({"Locked teams", "Can change in game"});
			ui.segments("rule/" + std::to_string(index), {fieldX, yy + 5, fieldW, 29}, options,
						index == 0	 ? !setup.prestige
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
				ui.stepper(
					"rule/workers", {fieldX, yy + 5, 130, 29}, setup.generator.nbWorkers,
					MapGenerationDescriptor::control(setup.generator.method, "Starting workers")
						.minimum,
					MapGenerationDescriptor::control(setup.generator.method, "Starting workers")
						.maximum,
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
		else
		{
			std::vector<std::string> options =
				localized({"Off (no timer)", "15 minutes", "30 minutes", "45 minutes", "60 minutes"});
			ui.dropdown("rule/suddenDeath", {fieldX, yy + 5, fieldW, 29}, options,
						setup.suddenDeathMinutes / 15, [apply](int v) { apply(v * 15); });
			help = tr("Match ends at the timer; highest prestige at that instant wins.");
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
			++setup.mapRevision;
			invalidate();
		};
		ui.text(x + 4, yy, tr("Landscape"), "little", leftW - 20, true);
		yy += 18;
		ui.dropdown(
			"generator/landscape", {x, yy, leftW - 16, 30}, localized(methods), g.method - 1,
			[this, changed](int value)
			{
				setup.generatorHistory.select(
					setup.generator, static_cast<MapGenerationDescriptor::Method>(value + 1));
				changed();
			});
		yy += 40;
		auto discrete = [&](const MapGenerationDescriptor::Control &c, const std::string &id)
		{
			ui.text(x + 4, yy + 7, tr(c.label), "little", 110, true);
			std::vector<std::string> options;
			for (int v = c.minimum; v <= c.maximum; v += c.step)
				options.push_back(std::to_string(c.powerOfTwo ? (1 << v) : v));
			ui.dropdown(id, {x + 112, yy, leftW - 128, 28}, options,
						(c.get(g) - c.minimum) / c.step,
						[this, c, changed](int i)
						{
							c.set(setup.generator, c.minimum + i * c.step);
							if (c.field == &MapGenerationDescriptor::nbTeams)
								setup.setCapacity(setup.generator.nbTeams);
							changed();
						});
			yy += 36;
		};
		for (const auto &c : MapGenerationDescriptor::sharedControls())
			if (c.field != &MapGenerationDescriptor::nbWorkers)
			{
				if (c.field == &MapGenerationDescriptor::nbTeams)
					g.nbTeams = setup.capacity;
				const char *id = c.field == &MapGenerationDescriptor::wDec	 ? "generator/width"
								 : c.field == &MapGenerationDescriptor::hDec ? "generator/height"
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
			for (const auto &c : MapGenerationDescriptor::controls(g.method))
			{
				if (static_cast<int>(c.group) != section)
					continue;
				any = true;
				std::string id = "generator/" + std::string(c.label);
				if (c.powerOfTwo)
				{
					discrete(c, "generator/repeat");
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
			setup.random ? tr(methods[setup.generator.method - 1]) : mapHeader.getMapName(),
			w < 800 ? "standard" : "menu", rightW);
	std::string dimensions = validMap ? std::to_string(preview->getLastWidth()) + " x " +
											std::to_string(preview->getLastHeight())
							 : setup.random ? std::to_string(1 << setup.generator.wDec) + " x " +
												  std::to_string(1 << setup.generator.hDec)
											: "";
	ui.text(rightX, top + 29,
			dimensions + "  /  " + std::to_string(setup.capacity) + " " + tr("colonies"), "little",
			rightW, true);
	int size = std::min(rightW, h - 126);
	int px = rightX + (rightW - size) / 2, py = top + 51;
	ui.box({px - 3, py - 3, size + 6, size + 6}, ui.line);
	if (validMap)
	{
		preview->setScreenPosition(px - (gfx->getW() - 640) / 2, py - (gfx->getH() - 480) / 2);
		preview->setDimensions(size, size);
		preview->paint();
	}
	else
	{
		ui.box({px, py, size, size}, Color(211, 223, 197));
		ui.paragraph(px + 16, py + size / 2 - 20, size - 32,
					 tr(previewPending
							? "Updating map preview..."
							: "Preview unavailable. Adjust settings or start to retry."));
	}
}
