// SPDX-License-Identifier: GPL-3.0-or-later
// Renders a worker on its final step into a building and requires the sprite to
// land exactly where a worker walking onto the same tile lands.
//
// A unit entering a building keeps its map slot on the tile it is leaving
// (Unit::handleActionEnteringBuilding) while posX/posY already name the
// building tile, so the draw loop visits it one square behind itself. Nothing
// in Game::drawUnit reads `displacement`: at equal delta the two states are the
// same picture, and any difference means the sprite is anchored on the wrong
// tile — the "entering a building jumps back one square" regression.
#include "GlobalContainer.h"
#include "Game.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "GraphicContext.h"
#include <SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <set>
#include <vector>

GlobalContainer* globalContainer = nullptr;

namespace
{
	// Screen and map geometry. The map is deliberately larger than the drawn
	// region so nothing wraps into it; the seam cases are covered by the pure
	// UnitDrawGeometryTest.
	const int SCREEN_W = 1024;
	const int SCREEN_H = 768;
	const int DRAW_W = 512;
	const int DRAW_H = 512;
	//! Tile the unit is walking from, and the building tile it walks into.
	const int FROM_X = 8, FROM_Y = 8;
	const int INTO_X = 9, INTO_Y = 8;

	const char* outputDir = ".cache/entering-unit-draw-check";
	//! Sampled points of one step: arrival, three intermediates, departure.
	const int DELTAS[] = {0, 63, 128, 191, 255};

	void require(bool ok, const char* message)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
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
		auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), frame.w, frame.h, 32,
			frame.w * 4, SDL_PIXELFORMAT_RGBA32);
		require(surface != nullptr, "wrap the framebuffer for PNG output");
		require(IMG_SavePNG(surface, (std::string(outputDir) + "/" + name + ".png").c_str()) == 0,
			"write the PNG");
		SDL_FreeSurface(surface);
	}

	//! Horizontal extent of everything drawn on the cleared background, in gfx
	//! coordinates. With only one unit on screen this is the glob's sprite.
	void spriteSpan(const Frame& frame, int* left, int* right)
	{
		*left = SCREEN_W;
		*right = -1;
		for (int y = 0; y < frame.h; ++y)
			for (int x = 0; x < frame.w; ++x)
			{
				const unsigned char* pixel = &frame.data[(y * frame.w + x) * 4];
				if (!pixel[0] && !pixel[1] && !pixel[2])
					continue;
				const int gfxX = x * SCREEN_W / frame.w;
				*left = std::min(*left, gfxX);
				*right = std::max(*right, gfxX);
			}
	}
}

class EnteringUnitDrawHarness
{
	//! Game::drawMapGroundUnits is private; this class is a friend of Game.
	static Frame render(Game& game, Game::ViewState& view)
	{
		auto* gfx = globalContainer->gfx;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		game.drawMapGroundUnits(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H,
			0, 0, 0, Game::DRAW_WHOLE_MAP, view);
		return grab();
	}

	//! Terrain, unit and building together, purely so a human can look at the
	//! animation. The comparison below stays on the unit-only render, which has
	//! no animated water or clouds to make frames differ by themselves.
	static void capturePresentation(Game& game, Unit* unit, Game::ViewState& view)
	{
		auto* gfx = globalContainer->gfx;
		std::set<Building*> visible;
		for (int delta : DELTAS)
		{
			unit->delta = delta;
			gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
			game.drawMapTerrain(0, 0, DRAW_W >> 5, DRAW_H >> 5, 0, 0, 0, Game::DRAW_WHOLE_MAP);
			game.drawMapGroundUnits(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H,
				0, 0, 0, Game::DRAW_WHOLE_MAP, view);
			game.drawMapGroundBuildings(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H,
				0, 0, 0, Game::DRAW_WHOLE_MAP, &visible, nullptr);
			save(grab(), "scene-delta" + std::to_string(delta));
		}
	}

public:
	static void run()
	{
		Game game(nullptr);
		game.map.setSize(5, 5, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		require(game.addBuilding(INTO_X, INTO_Y,
			globalContainer->buildingsTypes.getFinishedTypeNum("inn"), 0) != nullptr,
			"place the inn the worker walks into");

		Game::ViewState view;
		Unit* unit = game.addUnit(FROM_X, FROM_Y, 0, WORKER, 0, 0, 1, 0);
		require(unit != nullptr, "create the worker");
		unit->action = WALK;
		unit->directionFromDxDy();
		require(game.map.getGroundUnit(FROM_X, FROM_Y) == unit->gid,
			"the worker starts registered on the tile it is leaving");
		// Entering a building: posX/posY moved onto the building tile, the map slot
		// deliberately left behind so the unit stays drawable.
		unit->posX = INTO_X;
		unit->posY = INTO_Y;

		capturePresentation(game, unit, view);

		int checked = 0;
		for (int delta : DELTAS)
		{
			unit->delta = delta;
			const Frame entering = render(game, view);

			// The same instant of the same step, expressed the way every other
			// action leaves it: slot and position both on the destination tile.
			game.map.setGroundUnit(FROM_X, FROM_Y, NOGUID);
			game.map.setGroundUnit(INTO_X, INTO_Y, unit->gid);
			const Frame walking = render(game, view);
			game.map.setGroundUnit(INTO_X, INTO_Y, NOGUID);
			game.map.setGroundUnit(FROM_X, FROM_Y, unit->gid);

			int enteringLeft, enteringRight, walkingLeft, walkingRight;
			spriteSpan(entering, &enteringLeft, &enteringRight);
			spriteSpan(walking, &walkingLeft, &walkingRight);
			// A drawn glob is the whole point; two empty frames would compare equal.
			require(enteringRight >= enteringLeft, "the entering worker is drawn at all");
			std::printf("delta %3d: entering sprite x %d..%d, walking x %d..%d\n",
				delta, enteringLeft, enteringRight, walkingLeft, walkingRight);
			save(entering, "entering-delta" + std::to_string(delta));
			if (entering.data != walking.data)
			{
				save(walking, "walking-delta" + std::to_string(delta));
				std::fprintf(stderr,
					"FAIL: at delta %d the entering worker is drawn %d px from where the same "
					"step drawn as a walk puts it (one tile is 32 px); see %s/\n",
					delta, enteringLeft - walkingLeft, outputDir);
				std::exit(1);
			}
			++checked;
		}
		std::printf("Entering-unit draw regression passed: %d deltas render identically to the "
			"equivalent walk\n", checked);
	}
};

int main()
{
	std::filesystem::create_directories(outputDir);
	GlobalContainer globals("glob2-entering-unit-draw-test");
	globalContainer = &globals;
	globals.settings.screenWidth = SCREEN_W;
	globals.settings.screenHeight = SCREEN_H;
	globals.settings.screenFlags = GraphicContext::USEGPU;
	globals.settings.rememberUnit = false;
	globals.settings.mute = 1;
	globals.load();
	IntBuildingType::init();
	EnteringUnitDrawHarness::run();
	return 0;
}
