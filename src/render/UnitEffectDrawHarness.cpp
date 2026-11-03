// SPDX-License-Identifier: GPL-3.0-or-later
// Draws a swarm and the worker it has just produced, and workers leaving a
// hospital and a school, through the whole map pass at several ages of their
// effects (UnitEffects.h), saving each frame for review.
// The effect must change the picture while it plays and leave exactly the
// ordinary picture once its record has expired.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "GraphicContext.h"
#include "render/GameAnimations.h"
#include "render/UnitEffects.h"
#include <SDL3_image/SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <string>
#include <tuple>
#include <vector>

namespace
{
	const int SCREEN_W = 640, SCREEN_H = 480;
	//! Where the swarm is placed; its worker leaves on the tile below its footprint.
	const int SWARM_X = 6, SWARM_Y = 4;
	int exitX = 0, exitY = 0;

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
		GLOB2_REQUIRE(glGetError() == GL_NO_ERROR, "read back the framebuffer");
		return frame;
	}

	void save(const Frame& frame, const std::string& name)
	{
		std::vector<unsigned char> flipped(frame.data.size());
		for (int y = 0; y < frame.h; ++y)
			std::copy_n(frame.data.data() + y * frame.w * 4, frame.w * 4,
				flipped.data() + (frame.h - 1 - y) * frame.w * 4);
		auto* surface = SDL_CreateSurfaceFrom(frame.w, frame.h, SDL_PIXELFORMAT_RGBA32, flipped.data(), frame.w * 4);
		GLOB2_REQUIRE(surface != nullptr, "wrap the framebuffer for PNG output");
		const std::string path = (glob2test::artifactDir() / (name + ".png")).string();
		GLOB2_REQUIRE(IMG_SavePNG(surface, path.c_str()), "write the PNG");
		SDL_DestroySurface(surface);
	}

	//! Pixels differing between two frames of the same size.
	int differing(const Frame& a, const Frame& b)
	{
		int count = 0;
		for (size_t i = 0; i + 3 < a.data.size(); i += 4)
			if (a.data[i] != b.data[i] || a.data[i + 1] != b.data[i + 1] || a.data[i + 2] != b.data[i + 2])
				++count;
		return count;
	}

	Frame draw(Game& game)
	{
		auto* gfx = globalContainer->gfx;
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), 0, 0, 0);
		Game::ViewState view;
		glob2test::drawMap(game, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0, 0, view,
			Game::DRAW_WHOLE_MAP | Game::DRAW_NO_CLOUD_LAYER, nullptr, nullptr, true);
		return grab();
	}

	//! Restart the effect and age it to `age` ticks, as Map::syncStep would.
	void spawnAged(Game& game, Unit& unit, Building& swarm, int age)
	{
		game.animations->clear();
		game.animations->onUnitSpawn(game.map, exitX, exitY, game.teams[0], unit.gid, swarm.gid, 0, 1);
		for (int tick = 0; tick <= age; ++tick)
			game.animations->step();
	}
}

TEST_SUITE("UnitEffectDraw")
{
TEST_CASE("the spawn effect plays over a new worker and then leaves the ordinary picture [display:640x480][artifacts]")
{
	glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true,
		.width = SCREEN_W, .height = SCREEN_H, .screenFlags = GraphicContext::USEGPU});
	globalContainer->settings.clouds = false;
	globalContainer->settings.cloudShadows = false;
	REQUIRE(globalContainer->gfx->rendererCapabilities().accelerated);

	Game game(nullptr);
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam(0);
	Building* swarm = game.addBuilding(SWARM_X, SWARM_Y, globalContainer->buildingsTypes.getFinishedTypeNum("swarm"), 0);
	REQUIRE(swarm != nullptr);
	exitX = swarm->posX + swarm->type->width / 2;
	exitY = swarm->posY + swarm->type->height;
	Unit* unit = game.addUnit(exitX, exitY, 0, WORKER, 0, 0, 0, 1);
	REQUIRE(unit != nullptr);

	game.animations->clear();
	const Frame ordinary = draw(game);
	save(ordinary, "spawn-none");

	for (int age : {0, 2, 5, 10, 15, 22, 30})
	{
		spawnAged(game, *unit, *swarm, age);
		const Frame frame = draw(game);
		save(frame, "spawn-age" + std::to_string(age));
		CHECK_MESSAGE(differing(frame, ordinary) > 0, "the effect shows at age ", age);
	}

	// Gone at its lifetime: the record is dropped and the frame is the ordinary one.
	spawnAged(game, *unit, *swarm, UnitEffect::LifetimeTicks);
	CHECK(game.animations->getUnitEffects(game.map.getSectorIndex(exitX, exitY)).empty());
	const Frame after = draw(game);
	save(after, "spawn-after");
	CHECK(differing(after, ordinary) == 0);
}

TEST_CASE("service effects play over a unit leaving a hospital and a school [display:640x480][artifacts]")
{
	glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true,
		.width = SCREEN_W, .height = SCREEN_H, .screenFlags = GraphicContext::USEGPU});
	globalContainer->settings.clouds = false;
	globalContainer->settings.cloudShadows = false;
	globalContainer->settings.buildingParticles = true;

	for (const auto &[typeName, kind, level] : {std::tuple{"hospital", UnitEffect::Kind::Heal, 0},
		std::tuple{"school", UnitEffect::Kind::Train, 2}})
	{
		Game game(nullptr);
		game.map.setSize(5, 5, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		Building* building = game.addBuilding(SWARM_X, SWARM_Y, globalContainer->buildingsTypes.getFinishedTypeNum(typeName), 0);
		REQUIRE(building != nullptr);
		exitX = building->posX + building->type->width / 2;
		exitY = building->posY + building->type->height;
		Unit* unit = game.addUnit(exitX, exitY, 0, WORKER, 0, 0, 0, 1);
		REQUIRE(unit != nullptr);

		game.animations->clear();
		const Frame ordinary = draw(game);
		const std::string name = std::string("service-") + typeName;
		save(ordinary, name + "-none");
		const auto serve = [&](int age) {
			game.animations->clear();
			game.animations->onUnitServiced(unit->gid, Uint8(kind), building->gid, building->typeNum, Uint8(level));
			game.animations->onUnitLeftBuilding(game.map, unit->gid, exitX, exitY, 0, 1, game.teams[0], 0);
			for (int tick = 0; tick <= age; ++tick)
				game.animations->step();
		};
		for (int age : {0, 3, 8, 15, 22})
		{
			serve(age);
			const Frame frame = draw(game);
			save(frame, name + "-age" + std::to_string(age));
			CHECK_MESSAGE(differing(frame, ordinary) > 0, typeName, " effect shows at age ", age);
		}
		// Detail effects reduced: nothing plays.
		globalContainer->settings.buildingParticles = false;
		serve(3);
		CHECK(differing(draw(game), ordinary) == 0);
		globalContainer->settings.buildingParticles = true;
		serve(UnitEffect::LifetimeTicks);
		CHECK(differing(draw(game), ordinary) == 0);
	}
}
}
