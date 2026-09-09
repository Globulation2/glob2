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


// One hiring pass on a site that wants stone, with two candidates at the given
// positions, the second holding corn the site cannot take. Returns the
// gid of the unit hired, and reports each candidate's scored distance.
static int hireOneOfTwo(int emptyX, int emptyY, int loadedX, int loadedY, int* emptyCost, int* loadedCost)
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	const BuildingType* type = globalContainer->buildingsTypes.get(siteType);
	const int siteX = 8, siteY = 8;
	TestBuilding* site = new TestBuilding(siteX, siteY, Building::GIDfrom(0, 0), siteType, team,
	                                      &globalContainer->buildingsTypes, 1, 1);
	team->myBuildings[0] = site;
	game.map.setBuilding(siteX, siteY, type->width, type->height, site->gid);

	// Stone only, so the apportionment has exactly one job to staff.
	site->resources[WOOD] = type->maxResource[WOOD];
	require(site->neededResource(WOOD) == 0, "the site wants no more wood");
	require(site->neededResource(STONE) > 0, "the site still wants stone");
	require(game.map.incResource(22, 9, STONE, 0), "seed the stone tile");

	const int positions[2][2] = {{emptyX, emptyY}, {loadedX, loadedY}};
	for (int n = 0; n < 2; ++n)
	{
		Unit* unit = new Unit(positions[n][0], positions[n][1], n, WORKER, team, 0);
		team->myUnits[n] = unit;
		unit->performance[WALK] = 10;
		unit->performance[HARVEST] = 10;
		unit->activity = Unit::ACT_RANDOM;
		unit->displacement = Unit::DIS_RANDOM;
		unit->medical = Unit::MED_FREE;
		unit->attachedBuilding = NULL;
		unit->trigHungry = 100;
		unit->hungry = unit->trigHungry + 1000 * unit->race->hungriness;
	}
	// Unit 1 turns up holding corn, which this site has no use for at all.
	team->myUnits[1]->carriedResource = CORN;
	require(site->neededResource(CORN) == 0, "the corn is of no use here");

	const int swimClass = team->myUnits[0]->swimClass();
	for (int n = 0; n < 2; ++n)
	{
		Unit* unit = team->myUnits[n];
		int distBuilding = 0, distResource = 0;
		require(game.map.buildingAvailable(site, swimClass, unit->posX, unit->posY, &distBuilding),
			"the site is reachable from the candidate");
		require(game.map.resourceAvailable(0, STONE, swimClass, unit->posX, unit->posY, &distResource),
			"the stone is reachable from the candidate");
		*(n == 0 ? emptyCost : loadedCost) = distBuilding + distResource;
	}

	site->refreshCallLists();
	require(site->hireOne(), "somebody is hired");
	require(site->unitsWorking.size() == 1, "exactly one unit is hired");
	Unit* chosen = site->unitsWorking.front();
	require(chosen->destinationPurpose == STONE, "hired for the stone");
	return chosen->gid;
}

// A candidate that has to change task pays a detour, so an idle empty-handed one
// that is no closer wins; the geometry is mirrored so neither position nor scan
// order decides it.
static void anEmptyHandedUnitWinsAllElseEqual()
{
	int emptyCost = 0, loadedCost = 0;
	int hired = hireOneOfTwo(5, 7, 5, 11, &emptyCost, &loadedCost);
	std::printf("mirrored: empty=%d loaded=%d hired=%d\n", emptyCost, loadedCost, hired);
	require(emptyCost == loadedCost, "the two candidates really are equally placed");
	require(hired == 0, "the empty-handed unit is hired");

	// Same geometry with the cargo on the other unit: the empty one wins again.
	int mirroredEmpty = 0, mirroredLoaded = 0;
	int hiredMirrored = hireOneOfTwo(5, 11, 5, 7, &mirroredEmpty, &mirroredLoaded);
	require(hiredMirrored == 0, "still the empty-handed unit, whichever position it holds");

	std::puts("PASS a loaded candidate loses to an equally placed empty-handed one");
}

// But the detour is a price, not a veto: far enough ahead and the loaded unit
// is still the better hire.
static void aLoadedUnitFarEnoughAheadIsStillHired()
{
	int emptyCost = 0, loadedCost = 0;
	int hired = hireOneOfTwo(5, 9, 13, 9, &emptyCost, &loadedCost);
	std::printf("lopsided: empty=%d loaded=%d hired=%d\n", emptyCost, loadedCost, hired);
	require(loadedCost + 4 < emptyCost, "the loaded unit is more than the penalty ahead");
	require(hired == 1, "the loaded unit is hired anyway");

	std::puts("PASS a loaded candidate far enough ahead is hired despite the penalty");
}


// A unit already fetching for one building is hireable by another, at the switch
// penalty, so a bad assignment is undone instead of persisting for the whole trip.
static void aBuildingHiresAwayAUnitItSuitsBetter()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	const BuildingType* type = globalContainer->buildingsTypes.get(siteType);

	// Two sites at opposite ends, and the only stone next to the far one.
	TestBuilding* nearSite = new TestBuilding(20, 8, Building::GIDfrom(0, 0), siteType, team,
	                                          &globalContainer->buildingsTypes, 1, 1);
	TestBuilding* farSite = new TestBuilding(4, 8, Building::GIDfrom(1, 0), siteType, team,
	                                         &globalContainer->buildingsTypes, 1, 1);
	team->myBuildings[0] = nearSite;
	team->myBuildings[1] = farSite;
	game.map.setBuilding(20, 8, type->width, type->height, nearSite->gid);
	game.map.setBuilding(4, 8, type->width, type->height, farSite->gid);
	for (TestBuilding* site : {nearSite, farSite})
	{
		site->resources[WOOD] = type->maxResource[WOOD];
		require(site->neededResource(STONE) > 0, "the site still wants stone");
	}
	require(game.map.incResource(24, 8, STONE, 0), "seed the stone tile");

	// One worker, standing by the near site.
	Unit* unit = new Unit(23, 11, 0, WORKER, team, 0);
	team->myUnits[0] = unit;
	unit->performance[WALK] = 10;
	unit->performance[HARVEST] = 10;
	unit->activity = Unit::ACT_RANDOM;
	unit->displacement = Unit::DIS_RANDOM;
	unit->medical = Unit::MED_FREE;
	unit->attachedBuilding = NULL;
	unit->trigHungry = 100;
	unit->hungry = unit->trigHungry + 1000 * unit->race->hungriness;

	const int swimClass = unit->swimClass();
	int toNear = 0, toFar = 0;
	require(game.map.buildingAvailable(nearSite, swimClass, unit->posX, unit->posY, &toNear), "near site reachable");
	require(game.map.buildingAvailable(farSite, swimClass, unit->posX, unit->posY, &toFar), "far site reachable");
	std::printf("poach: toNear=%d toFar=%d\n", toNear, toFar);
	require(toFar > toNear + 4, "the far site is worse by more than the switch penalty");

	// The far site gets there first and takes the only worker.
	farSite->refreshCallLists();
	require(farSite->hireOne(), "the far site hires the only worker");
	require(unit->attachedBuilding == farSite, "the worker is working for the far site");

	// The near site can beat it by more than the penalty, so it hires it away.
	nearSite->refreshCallLists();
	require(nearSite->hireOne(), "the near site hires the worker away");
	require(unit->attachedBuilding == nearSite, "the worker now works for the near site");
	require(farSite->unitsWorking.empty(), "and no longer counts as the far site's");

	// The far site cannot take it back: it is worse by more than the penalty, and
	// a swap that does not pay for itself is what makes units ping-pong.
	farSite->refreshCallLists();
	farSite->hireOne();
	require(unit->attachedBuilding == nearSite, "the far site does not take it back");
	require(nearSite->unitsWorking.size() == 1, "the near site keeps its one worker");

	std::puts("PASS a better-placed building hires a working unit away, and the worse one cannot take it back");
}

// The two interruptions are separate losses, so a unit that is both working
// elsewhere and holding something unwanted pays for both.
static void thePenaltiesAdd()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS);
	game.map.setGame(&game);
	game.addTeam(0);
	Team* team = game.teams[0];

	const Sint32 siteType = globalContainer->buildingsTypes.getTypeNum("market", 0, true);
	const BuildingType* type = globalContainer->buildingsTypes.get(siteType);
	TestBuilding* site = new TestBuilding(8, 8, Building::GIDfrom(0, 0), siteType, team,
	                                      &globalContainer->buildingsTypes, 2, 2);
	team->myBuildings[0] = site;
	game.map.setBuilding(8, 8, type->width, type->height, site->gid);
	site->resources[WOOD] = type->maxResource[WOOD];
	require(game.map.incResource(22, 9, STONE, 0), "seed the stone tile");

	// Two candidates the same distance out. One is idle but loaded; the other is
	// idle and empty. One interruption against none: the empty one wins.
	Unit* loaded = new Unit(5, 7, 0, WORKER, team, 0);
	Unit* empty = new Unit(5, 11, 1, WORKER, team, 0);
	team->myUnits[0] = loaded;
	team->myUnits[1] = empty;
	for (Unit* u : {loaded, empty})
	{
		u->performance[WALK] = 10;
		u->performance[HARVEST] = 10;
		u->activity = Unit::ACT_RANDOM;
		u->displacement = Unit::DIS_RANDOM;
		u->medical = Unit::MED_FREE;
		u->attachedBuilding = NULL;
		u->trigHungry = 100;
		u->hungry = u->trigHungry + 1000 * u->race->hungriness;
	}
	loaded->carriedResource = CORN;

	const int swimClass = empty->swimClass();
	int a = 0, b = 0;
	require(game.map.buildingAvailable(site, swimClass, loaded->posX, loaded->posY, &a), "loaded reaches the site");
	require(game.map.buildingAvailable(site, swimClass, empty->posX, empty->posY, &b), "empty reaches the site");
	require(a == b, "the two candidates are equally placed");

	site->refreshCallLists();
	require(site->hireOne(), "somebody is hired");
	require(site->unitsWorking.front() == empty, "the unpenalised candidate is hired first");

	// Now the loaded one is the only candidate left, so it is hired despite the
	// price; it is holding corn and it is idle, so it pays one penalty, not two.
	require(site->hireOne(), "the loaded candidate is hired next");
	require(site->unitsWorking.size() == 2, "both are working now");

	std::puts("PASS an interruption is priced per loss, and an unpenalised candidate goes first");
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
	anEmptyHandedUnitWinsAllElseEqual();
	aLoadedUnitFarEnoughAheadIsStillHired();
	aBuildingHiresAwayAUnitItSuitsBetter();
	thePenaltiesAdd();
	std::puts("Fetch apportionment regressions passed");
	return 0;
}
