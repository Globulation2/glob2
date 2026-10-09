// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine and render regression: a selected building remembers, by gid,
// the units it could not hire under each reason, and the map view draws the
// reason's shape over exactly those units. Needs a display; writes the scene to
// scene.png in the case's artifact directory for reviewers.
#include "scene/SceneMap.h"
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Ressource.h"
#include "GraphicContext.h"
#include "MapInternal.h"
#include <SDL3_image/SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>


namespace
{
	const int SCREEN_W = 1024;
	const int SCREEN_H = 768;
	const int DRAW_W = 512;
	const int DRAW_H = 512;
	std::string outputDirectory;
	const char* outputDir = nullptr;

	void require(bool ok, const char* message)
	{
		GLOB2_REQUIRE(ok, message);
	}

	struct Frame
	{
		std::vector<unsigned char> data;
		int w = 0, h = 0;
	};

	Frame grab()
	{
		Sprite::flushBatches(globalContainer->gfx);
		glFinish();
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		Frame frame;
		frame.w = viewport[2];
		frame.h = viewport[3];
		frame.data.resize(frame.w * frame.h * 4);
		glReadPixels(viewport[0], viewport[1], frame.w, frame.h, GL_RGBA, GL_UNSIGNED_BYTE, frame.data.data());
		require(glGetError() == GL_NO_ERROR, "read back the framebuffer");
		return frame;
	}

	void save(const Frame& frame, const std::string& name)
	{
		std::vector<unsigned char> flipped(frame.data.size());
		for (int y = 0; y < frame.h; ++y)
			std::copy_n(frame.data.data() + y * frame.w * 4, frame.w * 4,
				flipped.data() + (frame.h - 1 - y) * frame.w * 4);
		auto* surface = SDL_CreateSurfaceFrom(frame.w, frame.h, SDL_PIXELFORMAT_RGBA32, flipped.data(), frame.w * 4);
		require(surface != nullptr, "wrap the framebuffer for PNG output");
		require(IMG_SavePNG(surface, (std::string(outputDir) + "/" + name + ".png").c_str()), "write the PNG");
		SDL_DestroySurface(surface);
	}

	// Red marker pixels inside the 32x32 screen tile of map tile (tx, ty).
	int redPixelsInTile(const Frame& frame, int tx, int ty)
	{
		int count = 0;
		for (int sy = ty * 32; sy < ty * 32 + 32; ++sy)
			for (int sx = tx * 32; sx < tx * 32 + 32; ++sx)
			{
				const int fx = sx * frame.w / SCREEN_W;
				const int fy = (SCREEN_H - 1 - sy) * frame.h / SCREEN_H;
				const unsigned char* p = &frame.data[(fy * frame.w + fx) * 4];
				if (p[0] > 180 && p[1] < 90 && p[2] < 90)
					++count;
			}
		return count;
	}

	Unit* worker(Game& game, Team* team, int x, int y, int level)
	{
		Unit* u = game.addUnit(x, y, 0, WORKER, level, 0, 0, 0);
		require(u != nullptr, "place a worker");
		u->activity = Unit::ACT_RANDOM;
		u->medical = Unit::MED_FREE;
		return u;
	}
}

class FailingUnitMarkersHarness
{
public:
	// Unschooled workers: a four-strong huddle, two pairs and two strays, so
	// the saved scene shows what a screenful of badges looks like at real size
	// and whether badges on neighbouring tiles stay apart.
	static const int LOW_COUNT = 10;
	struct PresentationFrame
	{
		Team* team; Building* inn; Unit* low[LOW_COUNT]; Unit* schooled;
		int lowX[LOW_COUNT] = {4, 5, 4,  5, 12, 13,  6,  7, 2, 14};
		int lowY[LOW_COUNT] = {4, 4, 5,  5,  5,  5, 12, 12, 3, 12};
	};
	// A level-2 inn wanting wheat: only schooled workers may stock it. Ten
	// unschooled workers spread around it, one schooled next to a wheat tile.
	static PresentationFrame buildScene(Game& game)
	{
		PresentationFrame scene;
		game.map.setSize(5, 5, GRASS); // 32x32
		game.map.setGame(&game);
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				game.map.clearImmobileUnit(x, y);
		game.addTeam(0);
		scene.team = game.teams[0];
		scene.team->race.loadDefault();
		const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 1, false);
		require(innType >= 0, "level-2 inn type exists");
		scene.inn = game.addBuilding(9, 8, innType, 0);
		require(scene.inn != nullptr, "place the inn");
		game.map.setBuilding(9, 8, scene.inn->type->width, scene.inn->type->height, scene.inn->gid);
		scene.inn->maxUnitWorking = 2;
		scene.inn->materials[WHEAT] = 0;
		scene.inn->updateCallLists();
		require(game.map.incResourceByIndex(2, 10, WHEAT, 0), "seed a wheat tile");
		for (int i = 0; i < LOW_COUNT; ++i)
			scene.low[i] = worker(game, scene.team, scene.lowX[i], scene.lowY[i], 0);
		scene.schooled = worker(game, scene.team, 3, 10, 1);
		return scene;
	}

	// The building panel with the inn selected: the tally rows carry the shapes.
	static void capturePanel()
	{
		GameGUI gui;
		gui.init();
		PresentationFrame scene = buildScene(gui.game);
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.setSelection(GameGUI::BUILDING_SELECTION, static_cast<void*>(scene.inn));
		// The GUI publishes the observed building; the simulation switches
		// recording on at the next tick boundary.
		require(!scene.inn->recordFailingUnits, "selection alone does not touch the building");
		gui.game.applyClientRequests();
		require(scene.inn->recordFailingUnits, "the tick boundary switches recording on");
		scene.team->updateAllBuildingTasks();
		// Only the right-hand panel: drawAll also wants players, a minimap and
		// a torus view this bare game does not have.
		gui.localTeam = gui.game.teams[0];
		gui.teamStats = &gui.localTeam->stats;
		gui.game.snapshots().invalidateBoundary();
		gui.prepareLocalPresentation();
		gui.updateCamera();
		auto* gfx = globalContainer->gfx;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 32);
		gui.drawPanel();
		Frame frame = grab();
		save(frame, "panel-inn-selected");
		// The rows sit in the right-hand menu; the shapes are the red pixels there.
		int red = 0;
		for (int sy = 0; sy < SCREEN_H; ++sy)
			for (int sx = SCREEN_W - 160; sx < SCREEN_W; ++sx)
			{
				const int fx = sx * frame.w / SCREEN_W;
				const int fy = (SCREEN_H - 1 - sy) * frame.h / SCREEN_H;
				const unsigned char* p = &frame.data[(fy * frame.w + fx) * 4];
				if (p[0] > 180 && p[1] < 90 && p[2] < 90)
					++red;
			}
		std::printf("markers: building panel holds %d red pixels\n", red);
		require(red > 0, "the panel rows carry the shapes");
		gui.setSelection(GameGUI::NO_SELECTION, static_cast<void*>(nullptr));
	}

	// A building only refreshes its failure tallies while it is asking for
	// units. Drag the "Working" ratio down to what is already hired and it
	// stops asking, keeping the gids its last scan recorded -- and the panel
	// stops drawing the rows that explain them. The badges have to go with the
	// rows, or the player is left with marks and nothing to read them against.
	static void checkStoppedAskingDropsBadges()
	{
		GameGUI gui;
		gui.init();
		PresentationFrame scene = buildScene(gui.game);
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.localTeam = gui.game.teams[0];
		gui.teamStats = &gui.localTeam->stats;
		gui.setSelection(GameGUI::BUILDING_SELECTION, static_cast<void*>(scene.inn));
		gui.game.applyClientRequests(); // the tick boundary turns recording on
		scene.team->updateAllBuildingTasks();
		require(scene.inn->unitsFailingByReason[Building::UnitTooLowLevel].size() == LOW_COUNT,
			"the scan with an open slot remembers every unschooled worker");

		// The player drags the ratio down to the one worker already hired.
		scene.inn->maxUnitWorking = 1;
		scene.inn->updateCallLists();
		scene.team->updateAllBuildingTasks();
		require((int)scene.inn->unitsWorking.size() == scene.inn->desiredMaxUnitWorking,
			"the inn is now staffed to its reduced ratio");
		require(scene.inn->unitsFailingByReason[Building::UnitTooLowLevel].size() == LOW_COUNT,
			"and it still holds the gids of the scan that ran while it was asking");

		gui.game.snapshots().invalidateBoundary();
		gui.prepareLocalPresentation();
		gui.updateCamera();
		auto* gfx = globalContainer->gfx;
		std::set<Uint16> visible;
		Game::ViewState view;
		view.selectedBuilding = scene.inn;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		{ SceneMap layers; glob2test::observeMap(gui.game.map,layers); gui.game.drawMapTerrain(0, 0, DRAW_W >> 5, DRAW_H >> 5, 0, 0, 0, Game::DRAW_WHOLE_MAP, layers); }
		gui.game.drawMapGroundBuildings(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H, 0, 0, 0, Game::DRAW_WHOLE_MAP, &visible, nullptr, glob2test::sceneOf(gui.game), nullptr);
		gui.game.drawMapGroundUnits(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H, 0, 0, 0, Game::DRAW_WHOLE_MAP, view, glob2test::sceneOf(gui.game, view));
		Frame stopped = grab();
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		{ SceneMap layers; glob2test::observeMap(gui.game.map,layers); gui.game.drawMapTerrain(0, 0, DRAW_W >> 5, DRAW_H >> 5, 0, 0, 0, Game::DRAW_WHOLE_MAP, layers); }
		gui.game.drawMapGroundBuildings(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H, 0, 0, 0, Game::DRAW_WHOLE_MAP, &visible, nullptr, glob2test::sceneOf(gui.game), nullptr);
		Game::ViewState none;
		gui.game.drawMapGroundUnits(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H, 0, 0, 0, Game::DRAW_WHOLE_MAP, none, glob2test::sceneOf(gui.game, none));
		Frame baseline = grab();
		for (int i = 0; i < LOW_COUNT; ++i)
		{
			const int added = redPixelsInTile(stopped, scene.lowX[i], scene.lowY[i])
				- redPixelsInTile(baseline, scene.lowX[i], scene.lowY[i]);
			require(added == 0, "a building that stopped asking for units marks nobody");
		}
		std::puts("markers: a building that stopped asking for units drops its badges with its panel rows");
		gui.setSelection(GameGUI::NO_SELECTION, static_cast<void*>(nullptr));
	}

	static void run()
	{

	Game game(nullptr);
	PresentationFrame scene = buildScene(game);
	Team* team = scene.team; Building* inn = scene.inn; Unit* schooled = scene.schooled;
	Unit** low = scene.low; const int* lowX = scene.lowX; const int* lowY = scene.lowY;

	// Not recording: nothing is remembered.
	team->updateAllBuildingTasks();
	require(inn->unitsFailingRequirements[Building::UnitTooLowLevel] == LOW_COUNT, "every unschooled worker is counted too low");
	require(inn->unitsFailingByReason[Building::UnitTooLowLevel].empty(), "nothing remembered while not recording");
	require(inn->unitsWorking.size() == 1 && inn->unitsWorking.front() == schooled, "the schooled worker is hired");

	// Selected in the GUI: the gids behind the tally are kept.
	inn->setRecordFailingUnits(true);
	team->updateAllBuildingTasks();
	const std::vector<Uint16>& tooLow = inn->unitsFailingByReason[Building::UnitTooLowLevel];
	require(tooLow.size() == LOW_COUNT, "every unschooled worker's gid is remembered under too-low-level");
	for (int i = 0; i < LOW_COUNT; ++i)
		require(std::find(tooLow.begin(), tooLow.end(), low[i]->gid) != tooLow.end(), "each unschooled worker is remembered");
	require(std::find(tooLow.begin(), tooLow.end(), schooled->gid) == tooLow.end(), "the schooled worker (already hired) is not");
	std::puts("markers: the selected building remembers the units behind each tally");

	// Render once without a selection (team colour is red too, so the sprites
	// hold red pixels of their own), then with the inn selected: the markers
	// are the red pixels the selection adds.
	auto* gfx = globalContainer->gfx;
	auto render = [&](Game::ViewState& view) {
		std::set<Uint16> visible;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		{ SceneMap layers; glob2test::observeMap(game.map,layers); game.drawMapTerrain(0, 0, DRAW_W >> 5, DRAW_H >> 5, 0, 0, 0, Game::DRAW_WHOLE_MAP, layers); }
		game.drawMapGroundBuildings(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H, 0, 0, 0, Game::DRAW_WHOLE_MAP, &visible, nullptr, glob2test::sceneOf(game), nullptr);
		game.drawMapGroundUnits(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H, 0, 0, 0, Game::DRAW_WHOLE_MAP, view, glob2test::sceneOf(game, view));
		return grab();
	};
	Game::ViewState none;
	Frame baseline = render(none);
	save(baseline, "scene-unselected");
	Game::ViewState view;
	view.selectedBuilding = inn;
	Frame frame = render(view);
	save(frame, "scene-inn-selected");
	for (int i = 0; i < LOW_COUNT; ++i)
	{
		const int added = redPixelsInTile(frame, lowX[i], lowY[i]) - redPixelsInTile(baseline, lowX[i], lowY[i]);
		std::printf("markers: unschooled worker at (%d,%d): %d red marker pixels\n", lowX[i], lowY[i], added);
		require(added > 0, "an unschooled worker wears the too-low-level shape");
	}
	{
		const int added = redPixelsInTile(frame, 3, 10) - redPixelsInTile(baseline, 3, 10);
		std::printf("markers: schooled worker at (3,10): %d red marker pixels\n", added);
		require(added == 0, "the schooled worker, hired and merely busy, wears no shape");
	}
	// Deselected: the markers go away.
	inn->setRecordFailingUnits(false);
	require(tooLow.empty(), "deselecting drops the remembered units");
	capturePanel();
	checkStoppedAskingDropsBadges();
	std::puts("PASS units a selected building could not hire wear the reason's shape");
}
};

TEST_SUITE("FailingUnitMarkers")
{
TEST_CASE("units a building could not hire wear the reason marker [display:1024x768][artifacts]")
{
	outputDirectory = glob2test::artifactDir().string();
	outputDir = outputDirectory.c_str();
	glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = SCREEN_W, .height = SCREEN_H, .screenFlags = GraphicContext::USEGPU});
	FailingUnitMarkersHarness::run();
}
}
