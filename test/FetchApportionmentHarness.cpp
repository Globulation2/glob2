// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a building that wants several resources at once must
// spread its fetchers across them, instead of sending everyone to whichever
// resource happens to be nearest and leaving the others with nobody.
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL.h>
#include <string>
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "BuildingType.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <cstdio>
#include <cstdlib>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// subscribeToBringResourcesStep() is the hiring pass Team::updateAllBuildingTasks
// runs; a subclass reaches it the same way test/README.md's Map subclass pattern
// reaches Map's predicates.
struct TestBuilding : Building
{
	TestBuilding(int x, int y, Uint16 gid, Sint32 typeNum, Team* team, BuildingsTypes* types,
	             Sint32 unitWorking, Sint32 unitWorkingFuture)
		: Building(x, y, gid, typeNum, team, types, unitWorking, unitWorkingFuture) {}
	bool hireOne() { return subscribeToBringResourcesStep(); }
	void refreshCallLists() { updateCallLists(); }
};

// A market construction site: 4 wood and 4 stone, the case from the bug report.
static void siteSpreadsItsFetchersAcrossBothResources()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	require(siteType >= 0, "the market building site type exists");
	const BuildingType* type = globalContainer->buildingsTypes.get(siteType);
	require(type->maxResource[WOOD] == 4 && type->maxResource[STONE] == 4,
		"the market site wants 4 wood and 4 stone");

	const int siteX = 16, siteY = 16;
	TestBuilding* site = new TestBuilding(siteX, siteY, Building::GIDfrom(0, 0), siteType, team,
	                                      &globalContainer->buildingsTypes, 8, 8);
	team->myBuildings[0] = site;
	game.map.setBuilding(siteX, siteY, type->width, type->height, site->gid);

	// Wood right beside the site, stone across the map: the layout that made the
	// site hire eight wood fetchers and no stone fetcher at all.
	for (int i = 0; i < 6; ++i)
		require(game.map.incResource(siteX + 3, siteY - 2 + i, WOOD, 0), "seed a wood tile by the site");
	for (int i = 0; i < 6; ++i)
		require(game.map.incResource(siteX - 2 + 16, siteY - 4 + 16 + i, STONE, 0), "seed a distant stone tile");

	// Eight idle workers standing on the site's doorstep, so distance never
	// stops anybody from being hired.
	const int workers = 8;
	for (int n = 0; n < workers; ++n)
	{
		Unit* unit = new Unit(siteX - 2, siteY - 2 + (n % 5), n, WORKER, team, 0);
		team->myUnits[n] = unit;
		unit->performance[WALK] = 10;
		unit->performance[HARVEST] = 10;
		unit->activity = Unit::ACT_RANDOM;
		unit->displacement = Unit::DIS_RANDOM;
		unit->medical = Unit::MED_FREE;
		unit->attachedBuilding = NULL;
	}

	// Confirm the layout really is lopsided, so a pass here means the
	// apportionment did the work and not an accidentally even map.
	int woodDistance = 0, stoneDistance = 0;
	Unit* probe = team->myUnits[0];
	require(game.map.resourceAvailable(0, WOOD, probe->swimClass(), probe->posX, probe->posY, &woodDistance),
		"the wood is reachable");
	require(game.map.resourceAvailable(0, STONE, probe->swimClass(), probe->posX, probe->posY, &stoneDistance),
		"the stone is reachable");
	std::printf("probe distances: wood=%d stone=%d\n", woodDistance, stoneDistance);
	require(stoneDistance > 3 * woodDistance, "the stone is far enough to tempt the old scorer");

	site->refreshCallLists();
	int hires = 0;
	while (site->hireOne())
		require(++hires <= workers, "hiring stops once the site has its deliveries");

	int subscribed[MAX_NB_RESOURCES] = {0};
	for (std::list<Unit*>::iterator ui = site->unitsWorking.begin(); ui != site->unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0 && (*ui)->destinationPurpose < MAX_NB_RESOURCES)
			subscribed[(*ui)->destinationPurpose]++;

	std::printf("hired %d: wood=%d stone=%d (wood %d tiles away, stone %d)\n",
		hires, subscribed[WOOD], subscribed[STONE], woodDistance, stoneDistance);
	require(subscribed[WOOD] == 4, "four fetchers went for wood, not every idle worker");
	require(subscribed[STONE] == 4, "four fetchers went for the distant stone");
	require((int)site->unitsWorking.size() == 8, "no worker was hired beyond the eight deliveries wanted");

	std::puts("PASS a market site spreads its fetchers over wood and stone");
}

int main(int argc, char** argv)
{
	SDL_SetMainReady();
	require(argc == 3, "usage: harness PROFILE ROOT");
	require(std::string(argv[1]).find("glob2-save-test-") == 0, "disposable profile required");
	GlobalContainer globals(argv[1]);
	globals.fileManager->addDir(argv[2]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	siteSpreadsItsFetchersAcrossBothResources();
	std::puts("Fetch apportionment regressions passed");
	return 0;
}
