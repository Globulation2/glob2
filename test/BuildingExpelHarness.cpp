// SPDX-License-Identifier: GPL-3.0-or-later
// Units survive their building being destroyed (issue #185).
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct World
{
	GameGUI gui;
	Game& game = gui.game;
	Team* team = nullptr;
	int parked = 0;
	static const int bx = 8, by = 8;  // where addInn() puts the inn

	World()
	{
		game.map.setSize(5, 5, GRASS);
		game.map.setGame(&game);
		game.addTeam(0);
		team = game.teams[0];
	}

	// Game::addBuilding creates the building; the map footprint is registered separately.
	Building* addBuilding(const char* name, int x, int y)
	{
		const int typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, false);
		require(typeNum >= 0, "building type exists");
		Building* b = game.addBuilding(x, y, typeNum, 0);
		require(b != nullptr, "building placed");
		game.map.setBuilding(x, y, b->type->width, b->type->height, b->gid);
		return b;
	}

	Building* addInn() { return addBuilding("inn", bx, by); }

	// A unit created on a far-away tile, so scenarios can move it wherever they need.
	Unit* addUnit(int typeNum, int x = -1, int y = -1)
	{
		if (x < 0) { x = 20 + parked % 10; y = 20 + parked / 10; ++parked; }
		Unit* u = game.addUnit(x, y, 0, typeNum, 0, 0, 0, 0);
		require(u != nullptr, "unit placed");
		return u;
	}

	// The state Unit::handleActivity leaves after a unit subscribes to go inside.
	void subscribe(Unit* u, Building* b, int purpose, Unit::Displacement displacement)
	{
		u->attachedBuilding = b;
		u->setTargetBuilding(b);
		u->activity = Unit::ACT_UPGRADING;
		u->displacement = displacement;
		u->destinationPurpose = purpose;
		u->needToRecheckMedical = false;
		b->unitsInside.push_back(u);
	}

	// Mirrors the state Unit::handleDisplacement leaves after DIS_ENTERING_BUILDING -> DIS_INSIDE.
	Unit* addInside(Building* b, int typeNum, int purpose, int elapsed, int total)
	{
		Unit* u = addUnit(typeNum);
		if (u->performance[FLY])
			game.map.setAirUnit(u->posX, u->posY, NOGUID);
		else
			game.map.setGroundUnit(u->posX, u->posY, NOGUID);
		u->posX = b->getMidX();
		u->posY = b->getMidY();
		subscribe(u, b, purpose, Unit::DIS_INSIDE);
		u->movement = Unit::MOV_INSIDE;
		u->insideTimeout = -(total - elapsed);
		u->speed = b->type->insideSpeed;
		return u;
	}

	Unit* addEntering(Building* b, int typeNum, int fromX, int fromY, int dx, int dy)
	{
		Unit* u = addUnit(typeNum, fromX, fromY);
		u->posX = (fromX + dx) & game.map.getMaskW();
		u->posY = (fromY + dy) & game.map.getMaskH();
		u->dx = dx;
		u->dy = dy;
		subscribe(u, b, FEED, Unit::DIS_ENTERING_BUILDING);
		u->movement = Unit::MOV_ENTERING_BUILDING;
		return u;
	}

	bool alive(const Unit* u) const
	{
		return u && !u->isDead && team->myUnits[Unit::GIDtoID(u->gid)] == u;
	}

	bool registered(const Unit* u) const
	{
		if (u->performance[FLY])
			return game.map.getAirUnit(u->posX, u->posY) == u->gid;
		return game.map.getGroundUnit(u->posX, u->posY) == u->gid;
	}

	bool inFootprintOrRing(const Unit* u, int bx, int by, int w, int h) const
	{
		const int ox = (u->posX - bx + 1) & game.map.getMaskW();
		const int oy = (u->posY - by + 1) & game.map.getMaskH();
		return ox <= w + 1 && oy <= h + 1;
	}

	void step(int steps)
	{
		for (int i = 0; i < steps; ++i)
			game.syncStep(0);
		require(game.integrity(), "integrity after simulation steps");
	}
};

static void destroyedInnExpelsEveryone()
{
	World world;
	const int bx = World::bx, by = World::by;
	Building* inn = world.addInn();
	const int w = inn->type->width, h = inn->type->height;
	const int total = inn->type->timeToFeedUnit;
	require(total > 0, "inn feeds units");
	Unit* units[5];
	units[0] = world.addInside(inn, WORKER, FEED, total / 2, total);
	units[1] = world.addInside(inn, WARRIOR, FEED, total / 2, total);
	units[2] = world.addInside(inn, EXPLORER, FEED, total / 2, total);
	units[3] = world.addInside(inn, WORKER, FEED, 0, total);
	units[4] = world.addEntering(inn, WORKER, bx - 1, by, 1, 0);
	for (Unit* u : units)
		u->hungry = 0;
	units[3]->displacement = Unit::DIS_EXITING_BUILDING;  // waiting for a free exit
	inn->resources[CORN] = 10;
	require(world.game.integrity(), "scenario setup is consistent");

	const Uint16 innGid = inn->gid;
	inn->kill();
	require(inn->buildingState == Building::DEAD && inn->unitsInside.empty(), "inn is dead and empty");
	for (Unit* u : units)
	{
		require(world.alive(u), "unit survives the destruction");
		require(u->attachedBuilding == nullptr && u->targetBuilding == nullptr, "unit is detached");
		require(u->activity == Unit::ACT_RANDOM && u->displacement == Unit::DIS_RANDOM, "unit is free");
		require(u->movement == Unit::MOV_EXITING_BUILDING && u->delta == 0, "unit walks out");
		require(u->insideTimeout == 0 && u->needToRecheckMedical, "unit is out and re-evaluates its needs");
		require(u->speed > 0, "unit has a speed");
		require(world.registered(u), "unit is registered on its tile");
		require(world.inFootprintOrRing(u, bx, by, w, h), "unit lands on the footprint or the ring");
	}
	for (int i = 0; i < 3; ++i)
		require(units[i]->hungry == (Unit::HUNGRY_MAX * (total / 2)) / total, "half a meal is kept");
	require(inn->resources[CORN] == 7, "each started meal cost one wheat, nothing else did");
	require(units[3]->hungry == 0, "a unit already on its way out gets nothing more");
	require(units[4]->posX == bx - 1 && units[4]->posY == by && units[4]->dx == -1 && units[4]->dy == 0,
	        "entering unit steps back onto the tile it came from");
	require(units[4]->hungry == 0, "entering unit had not started eating");
	require(world.game.integrity(), "integrity right after the destruction");
	world.step(40);
	for (Unit* u : units)
		require(world.alive(u) && world.registered(u), "expelled unit keeps living");
	require(world.team->myBuildings[Building::GIDtoID(innGid)] == nullptr, "dead inn was collected");  // inn is freed by now
	std::puts("PASS destroyed inn expels inside, exiting and entering units alive");
}

static void noRoomKillsTheSurplus()
{
	World world;
	const int bx = World::bx, by = World::by;
	Building* inn = world.addInn();
	const int w = inn->type->width, h = inn->type->height;
	const int total = inn->type->timeToFeedUnit;
	for (int y = by - 1; y <= by + h; ++y)
		for (int x = bx - 1; x <= bx + w; ++x)
			if (y == by - 1 || y == by + h || x == bx - 1 || x == bx + w)
				world.addUnit(WORKER, x, y);
	const int footprint = w * h;
	std::vector<Unit*> inside;
	for (int i = 0; i < footprint + 1; ++i)
		inside.push_back(world.addInside(inn, WORKER, FEED, 0, total));
	require(world.game.integrity(), "scenario setup is consistent");

	inn->kill();
	int survivors = 0, dead = 0;
	for (Unit* u : inside)
	{
		if (u->isDead) { ++dead; continue; }
		++survivors;
		require(world.registered(u), "survivor is registered");
		require(((u->posX - bx) & world.game.map.getMaskW()) < w && ((u->posY - by) & world.game.map.getMaskH()) < h,
		        "survivor stands on the footprint");
	}
	require(survivors == footprint && dead == 1, "exactly the units without a free tile die");
	require(world.game.integrity(), "integrity right after the destruction");
	world.step(5);
	std::puts("PASS a unit with no free tile still dies");
}

static void healingIsKeptProRata()
{
	World world;
	Building* hospital = world.addBuilding("hospital", 8, 8);
	const int total = hospital->type->timeToHealUnit;
	require(total > 0, "hospital heals units");
	Unit* u = world.addInside(hospital, WARRIOR, HEAL, (3 * total) / 4, total);
	const int maxHp = u->performance[HP];
	u->hp = maxHp / 10;
	const int expected = u->hp + ((maxHp - u->hp) * ((3 * total) / 4)) / total;
	hospital->kill();
	require(world.alive(u) && u->hp == expected, "three quarters of the healing is kept");
	world.step(10);
	std::puts("PASS healing is kept pro rata");
}

// Gradients are allocated lazily, so a fresh world has none: the per-step
// round robin must still return instead of spinning until one appears.
static void stepsWithoutAnyGradient()
{
	World world;
	world.step(3);
	world.game.map.getResourceGradient(0, WOOD, 0);
	world.step(3);
	std::puts("PASS a world without gradients keeps stepping");
}

int main()
{
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	stepsWithoutAnyGradient();
	destroyedInnExpelsEveryone();
	noRoomKillsTheSurplus();
	healingIsKeptProRata();
	std::puts("Building expulsion regressions passed");
	return 0;
}
