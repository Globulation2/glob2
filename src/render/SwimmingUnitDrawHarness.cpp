// SPDX-License-Identifier: GPL-3.0-or-later
// Renders swimming workers and warriors in the water and requires their limbs
// to fade into it while the torso stays as drawn.
//
// The swim artwork is seen from above with the body lying flat, so a swimmer
// sits in the water by fading with distance from its centre (RadialFade) and
// by a ring that each stroke sends across the surface (SwimmingPresentation.h).
// The scene captures, through one stroke, are for a human to look at; the
// comparison draws one swim frame with and without the fade on a black
// background.
#include "scene/SceneMap.h"
#include "HeadlessGlobals.h"
#include "DrawFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "MapDrawer.h"
#include "Unit.h"
#include "GraphicContext.h"
#include <RenderStateScope.h>
#include "unit/render/SwimmingPresentation.h"
#include "unit/render/UnitAnimation.h"
#include "unit/render/UnitHarvestAnimation.h"
#include "unit/render/ColonySkinPreview.h"
#include <cstdlib>
#include <SDL3_image/SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	const int SCREEN_W = 1024;
	const int SCREEN_H = 768;
	const int DRAW_W = 512;
	const int DRAW_H = 512;
	std::string outputDirectory;

	struct Frame
	{
		std::vector<unsigned char> data;
		int w = 0, h = 0;
		//! Pixel at gfx coordinates (x, y), in the top-down screen orientation.
		const unsigned char *at(int x, int y) const
		{
			const int fx = x * w / SCREEN_W, fy = h - 1 - y * h / SCREEN_H;
			return &data[(fy * w + fx) * 4];
		}
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
		GLOB2_REQUIRE(glGetError() == GL_NO_ERROR, "read back the framebuffer");
		return frame;
	}

	void save(const Frame &frame, const std::string &name)
	{
		std::vector<unsigned char> flipped(frame.data.size());
		for (int y = 0; y < frame.h; ++y)
			std::copy_n(frame.data.data() + y * frame.w * 4, frame.w * 4,
				flipped.data() + (frame.h - 1 - y) * frame.w * 4);
		auto *surface = SDL_CreateSurfaceFrom(frame.w, frame.h, SDL_PIXELFORMAT_RGBA32, flipped.data(), frame.w * 4);
		GLOB2_REQUIRE(surface != nullptr, "wrap the framebuffer for PNG output");
		GLOB2_REQUIRE(IMG_SavePNG(surface, (outputDirectory + "/" + name + ".png").c_str()), "write the PNG");
		SDL_DestroySurface(surface);
	}

	int brightness(const unsigned char *pixel)
	{
		return pixel[0] + pixel[1] + pixel[2];
	}
}

class SwimmingUnitDrawHarness
{
	//! Water, then swimmers, as the map renderer layers them.
	static void capture(Game &game, ViewState &view, const std::string &name)
	{
		auto *gfx = globalContainer->gfx;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		{
			SceneMap layers;
			glob2test::observeMap(game.map, layers);
			MapDrawer::drawMapTerrain(0, 0, DRAW_W >> 5, DRAW_H >> 5, 0, 0, 0, Game::DRAW_WHOLE_MAP, layers,
				view.render.animationTime);
		}
		MapDrawer::drawMapGroundUnits(0, 0, DRAW_W >> 5, DRAW_H >> 5, DRAW_W, DRAW_H,
			0, 0, 0, Game::DRAW_WHOLE_MAP, view, glob2test::sceneOf(game, view));
		save(grab(), name);
	}

	//! One swim frame drawn plainly and faded, both on black.
	static void compareFade()
	{
		auto *gfx = globalContainer->gfx;
		Sprite *sprite = globalContainer->units;
		const int frame = unitAnimationFrame(128, 2, 0); // worker swim, facing east
		const int px = 64, py = 64;
		const int x = px - ((sprite->getW(frame) - 32) >> 1), y = py - ((sprite->getH(frame) - 32) >> 1);
		sprite->setBaseColor(Color(0, 160, 255));

		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		gfx->drawSprite(x, y, sprite, frame);
		const Frame plain = grab();
		save(plain, "swim-frame-plain");

		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		{
			const GAGCore::RadialFadeScope fade(*gfx, swimmerFade(px, py, SWIM_REACH_WORKER));
			gfx->drawSprite(x, y, sprite, frame);
		}
		const Frame faded = grab();
		save(faded, "swim-frame-faded");

		// Sample the sprite's own box by gfx pixel; the framebuffer may be HiDPI.
		// The grid interpolates the fade, so compare the regions in aggregate.
		const float cx = px + 16, cy = py + 16, reach = SWIM_REACH_WORKER;
		long plainTorso = 0, fadedTorso = 0, plainLimbs = 0, fadedLimbs = 0;
		for (int gy = y; gy < y + sprite->getH(frame); ++gy)
			for (int gx = x; gx < x + sprite->getW(frame); ++gx)
			{
				const float d = std::hypot(gx + 0.5f - cx, gy + 0.5f - cy);
				const int before = brightness(plain.at(gx, gy)), after = brightness(faded.at(gx, gy));
				if (d < reach * SWIM_FADE_INNER * 0.75f)
				{
					plainTorso += before;
					fadedTorso += after;
				}
				else if (d > reach)
				{
					plainLimbs += before;
					fadedLimbs += after;
				}
			}
		REQUIRE(plainTorso > 0);
		REQUIRE(plainLimbs > 0);
		const double torso = double(fadedTorso) / plainTorso, limbs = double(fadedLimbs) / plainLimbs;
		std::printf("swim frame: torso at %.2f and limbs at %.2f of their unfaded brightness\n", torso, limbs);
		CHECK(torso > 0.95);
		CHECK(limbs < SWIM_FADE_FLOOR + 0.1);
	}

public:
	static void harvest()
	{
		Game game(nullptr);
		game.map.setSize(4, 4, WATER);
		game.map.setGame(&game);
		game.addTeam(0);
		for (int direction = 0; direction < 8; ++direction)
		{
			auto *u = game.addUnit(3 + 3 * (direction % 4), 3 + 4 * (direction / 4), 0, WORKER,
								 NB_UNIT_LEVELS - 1, 0, 0, 0);
			REQUIRE(u);
			u->action = HARVEST;
			u->direction = direction;
		}
		ViewState view;
		if (std::getenv("GLOB2_SKIN_PREVIEW_DIR"))
		{
			REQUIRE(view.render.skinPreview().ready);
			for (const auto &mesh : view.render.skinPreview().proneHarvest)
				REQUIRE(mesh.identity != 0);
		}
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			// Drive only the presentation clock, never advance game orders.
			game.stepCounter = unsigned(frame * 9 / 10);
			view.render.unitMotion = float(frame) * .9f - float(game.stepCounter);
			const auto before = game.checkSum();
			const auto &scene = glob2test::sceneOf(game, view);
			REQUIRE_FALSE(scene.entities.units.empty());
			const auto &worker = scene.entities.units.front();
			CHECK(unitProneHarvestPhase(scene, worker, WORKER, view.render.unitMotion) >= 0);
			CHECK(unitProneHarvestPhase(scene, worker, WARRIOR, view.render.unitMotion) == -1);
			auto changed = worker;
			changed.action = BUILD;
			CHECK(unitProneHarvestPhase(scene, changed, WORKER, view.render.unitMotion) == -1);
			changed = worker;
			changed.performance[FLY] = 1;
			CHECK(unitProneHarvestPhase(scene, changed, WORKER, view.render.unitMotion) == -1);
			view.render.skinPreview().prepare(*globalContainer->gfx, scene, 0, 0, 16, 16, 0, 0, 0, 0,
											true, view.render.unitMotion);
			capture(game, view, "harvest-" + std::to_string(frame));
			CHECK(game.checkSum() == before);
		}
		// The same action on land, including shores, keeps its normal artwork.
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				game.map.setVertexTerrain(x, y, GRASS);
		const auto &scene = glob2test::sceneOf(game, view);
		auto worker = scene.entities.units.front();
		CHECK(unitProneHarvestPhase(scene, worker, WORKER, 0) == -1);
	}
	static void run()
	{
		Game game(nullptr);
		game.map.setSize(4, 4, WATER);
		game.map.setGame(&game);
		game.addTeam(0);

		// Swimmers of both kinds, moving in several directions and treading water.
		struct Placement { int x, y, type, dx, dy; bool moving; };
		const Placement placements[] = {
			{3, 3, WORKER, 1, 0, true}, {6, 3, WORKER, 1, 1, true}, {9, 3, WORKER, 0, -1, true},
			{3, 7, WARRIOR, -1, 0, true}, {6, 7, WARRIOR, 0, 1, true}, {9, 7, WARRIOR, 0, 0, false},
			{12, 3, WORKER, 0, 0, false}, {12, 7, WARRIOR, 1, -1, true},
		};
		std::vector<Unit *> swimmers;
		for (const auto &p : placements)
		{
			Unit *unit = game.addUnit(p.x, p.y, 0, p.type, NB_UNIT_LEVELS - 1, 0, p.dx, p.dy);
			GLOB2_REQUIRE(unit != nullptr, "place a swimmer");
			unit->action = p.moving ? SWIM : STOP_SWIM;
			unit->directionFromDxDy();
			swimmers.push_back(unit);
		}

		ViewState view;
		// Through one stroke: arms forward, a new ring as they sweep back, the ring
		// spreading through the pull and fading out before the kick.
		for (int delta : {200, 232, 16, 80})
		{
			for (Unit *unit : swimmers)
				unit->delta = delta;
			capture(game, view, "swimmers-delta" + std::to_string(delta));
		}
		compareFade();
	}
};

TEST_SUITE("SwimmingUnitDraw")
{
	TEST_CASE("harvesting afloat uses the approved stroke without mutating simulation "
			  "[display:1024x768][artifacts]")
	{
		outputDirectory = glob2test::artifactDir().string();
		glob2test::HeadlessGlobals globals(
			glob2test::GlobalsOptions{.display = true,
									  .loadStrings = true,
									  .width = SCREEN_W,
									  .height = SCREEN_H,
									  .screenFlags = GraphicContext::USEGPU});
		SwimmingUnitDrawHarness::harvest();
	}

TEST_CASE("swimmers fade into the water around a solid torso [display:1024x768][artifacts]")
{
	outputDirectory = glob2test::artifactDir().string();
	glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = SCREEN_W, .height = SCREEN_H, .screenFlags = GraphicContext::USEGPU});
	SwimmingUnitDrawHarness::run();
}
}
