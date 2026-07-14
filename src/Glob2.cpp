// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2.h"
#include "GlobalContainer.h"
#include "YOGServer.h"

#ifndef YOG_SERVER_ONLY

#include "CampaignMenuScreen.h"
#include "CampaignMainMenu.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "Engine.h"
#include "Game.h"
#include "LANMenuScreen.h"
#include "MainMenuScreen.h"
#include "MapGenerator.h"
#include "SettingsScreen.h"
#include <StringTable.h>
#include "Utilities.h"
#include "YOGClient.h"
#include "YOGLoginScreen.h"
#include "YOGServerRouter.h"
#include "YOGClientRouterAdministrator.h"


#include <Stream.h>
#include <BinaryStream.h>
#include <Toolkit.h>
#include <FileManager.h>
#include "map/Map.h"
#include "team/Team.h"
#include "building/Building.h"
#include "BuildingType.h"
#include "ai/cortex/CortexWheat.h"

#include <stdio.h>
#include <stdlib.h>

#endif  // !YOG_SERVER_ONLY

#ifndef WIN32
#	include <unistd.h>
#else
#	include <time.h>
#endif

#ifdef __APPLE__
#	include <Carbon/Carbon.h>
#	include <sys/param.h>
#endif

using std::shared_ptr;

/*!	\mainpage Globulation 2 Reference documentation

	\section intro Introduction
	This is the documentation of Globulation 2, a free
	software game. It covers Glob2 itself and
	libgag (graphic and widget).
	\section feedback Feedback
	This documentation is not yet complete, but should help to have an
	overview of Globulation's 2 code. If you have any comments or suggestions,
	do not hesitate to contact the development team at
	http://www.globulation2.org
*/

GlobalContainer *globalContainer=NULL;


#ifndef YOG_SERVER_ONLY

void Glob2::drawYOGSplashScreen(void)
{
	int w, h;
	w=globalContainer->gfx->getW();
	h=globalContainer->gfx->getH();
	globalContainer->gfx->drawFilledRect(0, 0, w, h, 0, 0, 0);
	std::string text[3];
	text[0]=Toolkit::getStringTable()->getString("[connecting to]");
	text[1]=Toolkit::getStringTable()->getString("[yog]");
	text[2]=Toolkit::getStringTable()->getString("[please wait]");
	for (int i=0; i<3; ++i)
	{
		int size=globalContainer->menuFont->getStringWidth(text[i]);
		int dec=(w-size)>>1;
		globalContainer->gfx->drawString(dec, 150+i*50, globalContainer->menuFont, text[i]);
	}
	globalContainer->gfx->nextFrame();
}

void Glob2::mutiplayerYOG(void)
{
	if (verbose)
		printf("Glob2:: starting YOGLoginScreen...\n");
	shared_ptr<YOGClient> client(new YOGClient);
	YOGLoginScreen yogLoginScreen(client);
	int yogReturnCode=yogLoginScreen.execute(globalContainer->gfx, 40);
	if (yogReturnCode==YOGLoginScreen::Cancelled)
		return;
	if (yogReturnCode==-1)
	{
		isRunning=false;
		return;
	}
	if (verbose)
		printf("Glob2::YOGLoginScreen has ended ...\n");
}

int Glob2::runNoX()
{
	printf("nox::running %d times %d steps:\n", globalContainer->runNoXCountRuns, globalContainer->automaticEndingSteps);
	for (int runNoXCount = 0; runNoXCount < globalContainer->runNoXCountRuns; runNoXCount++)
	{
		Engine engine;
		if (engine.initCustom(globalContainer->runNoXGameName) != Engine::EE_NO_ERROR)
			return 1;
		engine.run();
	}
	return 0;
}



int Glob2::runTestGames()
{
	globalContainer->automaticEndingSteps=90000;
	int maxRuns = globalContainer->runTestGamesCount;
	int run = 0;
	while(maxRuns == 0 || run < maxRuns)
	{
		// GLOB2_TEST_SEED overrides the wall-clock seed for deterministic
		// regression testing. With a fixed seed (and unchanged maps/), two
		// runs produce byte-identical replays — the basis for the
		// behavior-preservation harness used by C++ cleanup work.
		const char* envSeed = getenv("GLOB2_TEST_SEED");
		long t = envSeed ? atol(envSeed) : time(NULL);
		setSyncRandSeed(t);
		// Capture the seed so createRandomGame can mirror it into
		// GameHeader::seed — otherwise a saved .game file (from
		// --save-game-as or GLOB2_DUMP_GAME) would carry the wall-clock
		// time(NULL) that GameHeader's default ctor wrote, not the seed
		// that actually drove this run, and reloading via --nox would
		// diverge from the original.
		globalContainer->testGamesSeed = (Uint32)t;
		globalContainer->testGamesSeedSet = true;
		std::cout<<"Random Seed initial: "<<t<<std::endl;
		Engine engine;
		engine.createRandomGame();
		engine.run();
		run++;
	}
	return 0;
}



int Glob2::runTestMapGeneration()
{
	long t = time(NULL);
	setSyncRandSeed(t);
	while(true)
	{
		MapGenerationDescriptor descriptor;
		
		int type = (syncRand() % 7) + 1;
		int wDec = (syncRand() % 4) + 6;
		int hDec = (syncRand() % 4) + 6;
		int teams = (syncRand() % 12) + 1;
		int workers = (syncRand() % 8) + 1;
		int repeat = (syncRand() % 5);
		int smooth = (syncRand() % 8) + 1;
		
		int oldBeach = (syncRand() % 4);
		
		descriptor.methode = static_cast<MapGenerationDescriptor::Methode>(type);
		descriptor.nbTeams = teams;
		descriptor.wDec=wDec;
		descriptor.hDec=hDec;
		descriptor.smooth = smooth;
		descriptor.oldBeach=oldBeach;
		descriptor.nbWorkers=workers;
		descriptor.logRepeatAreaTimes = repeat;
		
		descriptor.waterRatio=syncRand() % 100;
		descriptor.sandRatio=syncRand() % 100;
		descriptor.grassRatio=syncRand() % 100;
		descriptor.desertRatio=syncRand() % 100;
		descriptor.wheatRatio=syncRand() % 100;
		descriptor.woodRatio=syncRand() % 100;
		descriptor.algaeRatio=syncRand() % 100;
		descriptor.stoneRatio=syncRand() % 100;
		descriptor.fruitRatio=syncRand() % 100;
		descriptor.riverDiameter=syncRand() % 100;
		descriptor.craterDensity=syncRand() % 100;
		descriptor.extraIslands=syncRand() % 9;
		//eISLANDS
		descriptor.oldIslandSize=syncRand() % 74;
		

		std::cout<<"Generating Map"<<std::endl;		
		MapGenerator generator;
		Game game(NULL);
		generator.generateMap(game, descriptor);
	}
	return 0;
}
#endif  // !YOG_SERVER_ONLY


#ifndef YOG_SERVER_ONLY
// Headless tooling: dump a map's CORN (wheat) layout and team start positions as
// ASCII, to sanity-check AI wheat-protection field geometry. Reuses the real
// Game::load path so the data matches what the engine sees. Not a gameplay feature.
static int dumpResources(const std::string& mapName)
{
	using namespace GAGCore;
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "dump-resources: cannot open " << mapName << std::endl;
		delete stream;
		return 1;
	}
	Game game(NULL);
	bool ok = game.load(stream);
	delete stream;
	if (!ok)
	{
		std::cerr << "dump-resources: failed to load " << mapName << std::endl;
		return 1;
	}

	Map& map = game.map;
	const int w = map.getW();
	const int h = map.getH();
	int cornCount = 0;
	int minX = w, minY = h, maxX = -1, maxY = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (map.getRessource(x, y).type == CORN)
			{
				cornCount++;
				if (x < minX) minX = x; if (x > maxX) maxX = x;
				if (y < minY) minY = y; if (y > maxY) maxY = y;
			}

	const int teamCount = game.mapHeader.getNumberOfTeams();
	std::cout << "Map " << mapName << " : " << w << "x" << h
	          << ", teams=" << teamCount << ", CORN tiles=" << cornCount;
	if (cornCount > 0)
		std::cout << ", CORN bbox=(" << minX << "," << minY << ")-(" << maxX << "," << maxY << ")";
	std::cout << std::endl;
	for (int t = 0; t < teamCount; t++)
		if (game.teams[t])
			std::cout << "  team " << t << " start=(" << game.teams[t]->startPosX
			          << "," << game.teams[t]->startPosY << ")" << std::endl;
	std::cout << "  legend: C=corn ~=water #=non-walkable .=land  digit=team start" << std::endl;

	for (int y = 0; y < h; y++)
	{
		std::string row;
		for (int x = 0; x < w; x++)
		{
			char c;
			if (map.getRessource(x, y).type == CORN)      c = 'C';
			else if (map.isWater(x, y))                    c = '~';
			else if (!map.isFreeForGroundUnitNoForbidden(x, y, false)) c = '#';
			else                                           c = '.';
			for (int t = 0; t < teamCount; t++)
				if (game.teams[t] && game.teams[t]->startPosX == x && game.teams[t]->startPosY == y)
					c = (char)('0' + t);
			row += c;
		}
		std::cout << row << std::endl;
	}
	return 0;
}

// Headless tooling (AI wheat-protection eyeball): run the Cortex wheat scan over
// one team's territory on a freshly-loaded map and print the checkerboard it
// WOULD paint, swept over the open-margin range N=0..2. No Orders are emitted —
// this is the isolated geometry/reconcile core (ai/cortex/CortexWheat.*).
//
// A loaded .map has no colony and is fully fogged, so this differs from the live
// path in two debug-only ways, both documented inline: fog is bypassed
// (ignoreFOW), and the territory region is faked as the start/colony bounding box
// padded generously (the live path uses the real colony bbox + margin).
static int dumpWheatPlan(const std::string& mapName, int team)
{
	using namespace GAGCore;
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "dump-wheat: cannot open " << mapName << std::endl;
		delete stream;
		return 1;
	}
	Game game(NULL);
	bool ok = game.load(stream);
	delete stream;
	if (!ok)
	{
		std::cerr << "dump-wheat: failed to load " << mapName << std::endl;
		return 1;
	}

	Map& map = game.map;
	const int w = map.getW();
	const int h = map.getH();
	const int teamCount = game.mapHeader.getNumberOfTeams();
	if (team < 0 || team >= teamCount || game.teams[team] == NULL)
	{
		std::cerr << "dump-wheat: team " << team << " out of range (teams=" << teamCount << ")" << std::endl;
		return 1;
	}

	Team* tm = game.teams[team];
	const Uint32 teamMask = Team::teamNumberToMask(team);

	// Consumer seeds = feeding-building (inn) tiles; the colony bounding box grows
	// over every real building. A freshly-loaded .map usually has no buildings, so
	// fall back to the team start position as the single consumer seed.
	std::vector<int> seeds;
	std::vector<bool> seedBit(static_cast<size_t>(w) * h, false);
	int bbMinX = w, bbMinY = h, bbMaxX = -1, bbMaxY = -1;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building* b = tm->myBuildings[i];
		if (b == NULL || b->buildingState == Building::DEAD)
			continue;
		if (b->posX < bbMinX) bbMinX = b->posX;
		if (b->posX > bbMaxX) bbMaxX = b->posX;
		if (b->posY < bbMinY) bbMinY = b->posY;
		if (b->posY > bbMaxY) bbMaxY = b->posY;
		if (b->type && b->type->canFeedUnit)
		{
			const int idx = static_cast<int>(map.coordToIndex(b->posX, b->posY));
			seeds.push_back(idx);
			seedBit[idx] = true;
		}
	}
	const int startX = tm->startPosX;
	const int startY = tm->startPosY;
	if (startX < bbMinX) bbMinX = startX;
	if (startX > bbMaxX) bbMaxX = startX;
	if (startY < bbMinY) bbMinY = startY;
	if (startY > bbMaxY) bbMaxY = startY;
	if (seeds.empty())
	{
		const int idx = static_cast<int>(map.coordToIndex(startX, startY));
		seeds.push_back(idx);
		seedBit[idx] = true;
	}

	// Fake territory region: the start/colony bbox padded enough to reach a
	// starter field across its land gap (live path uses the colony bbox + a
	// smaller WHEAT_REGION_MARGIN instead).
	const int DEBUG_REGION_HALF = 18;
	int boxMinX = bbMinX - DEBUG_REGION_HALF;
	int boxMinY = bbMinY - DEBUG_REGION_HALF;
	int boxMaxX = bbMaxX + DEBUG_REGION_HALF;
	int boxMaxY = bbMaxY + DEBUG_REGION_HALF;
	if (boxMinX < 0) boxMinX = 0;
	if (boxMinY < 0) boxMinY = 0;
	if (boxMaxX > w - 1) boxMaxX = w - 1;
	if (boxMaxY > h - 1) boxMaxY = h - 1;

	std::cout << "Wheat-plan dump " << mapName << " : " << w << "x" << h
	          << ", team " << team << " start=(" << startX << "," << startY << ")"
	          << ", consumer seeds=" << seeds.size()
	          << ", region=(" << boxMinX << "," << boxMinY << ")-(" << boxMaxX << "," << boxMaxY << ")"
	          << " [fog bypassed]" << std::endl;
	std::cout << "  legend: ~=water #=blocked .=land c=corn(unreached) o=open-margin"
	             " +=harvest-half X=forbidden S=seed " << team << "=start" << std::endl;

	for (int N = 0; N <= 2; N++)
	{
		Cortex::WheatScanResult r = Cortex::scanWheatForbidden(
			map, teamMask, team, seeds,
			boxMinX, boxMinY, boxMaxX, boxMaxY,
			/*openMargin=*/N, /*ignoreFOW=*/true, /*wantDebug=*/true);

		std::cout << "=== team " << team << ", N=" << N << " ===  field=" << r.fieldTileCount
		          << " components=" << r.componentCount << " open=" << r.openCount
		          << " forbidden=" << r.forbiddenCount
		          << " add=" << r.addCount << " del=" << r.delCount << std::endl;

		for (int y = boxMinY; y <= boxMaxY; y++)
		{
			std::string row;
			for (int x = boxMinX; x <= boxMaxX; x++)
			{
				const int idx = static_cast<int>(map.coordToIndex(x, y));
				const Uint8 cls = r.classOf.empty() ? (Uint8)Cortex::WC_NONE : r.classOf[idx];
				char c;
				if (x == startX && y == startY)            c = (char)('0' + team);
				else if (seedBit[idx])                     c = 'S';
				else if (cls == Cortex::WC_OPEN_MARGIN)    c = 'o';
				else if (cls == Cortex::WC_FORBIDDEN)      c = 'X';
				else if (cls == Cortex::WC_CHECKER_OPEN)   c = '+';
				else if (map.getRessource(x, y).type == CORN) c = 'c';
				else if (map.isWater(x, y))                c = '~';
				else if (!map.isFreeForGroundUnitNoForbidden(x, y, false)) c = '#';
				else                                       c = '.';
				row += c;
			}
			std::cout << row << std::endl;
		}
		std::cout << std::endl;
	}
	return 0;
}
#endif  // !YOG_SERVER_ONLY

int Glob2::run(int argc, char *argv[])
{
	srand(time(NULL));

	globalContainer=new GlobalContainer();
	globalContainer->parseArgs(argc, argv);
	globalContainer->load();

#ifndef YOG_SERVER_ONLY
	// Headless tooling hook (AI wheat-protection sanity check): -dump-resources <map>
	for (int ai = 1; ai + 1 < argc; ai++)
		if (strcmp(argv[ai], "-dump-resources") == 0)
		{
			int ret = dumpResources(argv[ai + 1]);
			delete globalContainer;
			return ret;
		}
		else if (strcmp(argv[ai], "-dump-wheat") == 0)
		{
			// -dump-wheat <map> [team]; team defaults to 0.
			int team = 0;
			if (ai + 2 < argc && argv[ai + 2][0] >= '0' && argv[ai + 2][0] <= '9')
				team = atoi(argv[ai + 2]);
			int ret = dumpWheatPlan(argv[ai + 1], team);
			delete globalContainer;
			return ret;
		}
#endif  // !YOG_SERVER_ONLY

	if ( SDLNet_Init() < 0 )
	{
		fprintf(stderr, "Couldn't initialize net: %s\n", SDLNet_GetError());
		exit(1);
	}
	atexit(SDLNet_Quit);

	if (globalContainer->hostServer)
	{
		YOGServer server(YOGRequirePassword, YOGMultipleGames);
		int rc = server.run();
		delete globalContainer;
		return rc;
	}

// Glob2::run ends here for server.
#ifndef YOG_SERVER_ONLY

	if (globalContainer->hostRouter)
	{
		YOGServerRouter router;
		int rc = router.run();
		return rc;	
	}
	if(globalContainer->adminRouter)
	{
		YOGClientRouterAdministrator admin;
		return admin.execute();
	}
	
	if (globalContainer->runTestGames)
	{
		int ret=runTestGames();
		delete globalContainer;
		return ret;
	}
	
	if(globalContainer->runTestMapGeneration)
	{
		runTestMapGeneration();
	}
	
	if (globalContainer->runNoX)
	{
		int ret=runNoX();
		delete globalContainer;
		return ret;
	}

	isRunning=true;

	// Replay the game specified by the command line
	if (globalContainer->replaying)
	{
		Engine engine;
		int rc_e = engine.loadReplay(globalContainer->replayFileName);
		if (rc_e == Engine::EE_NO_ERROR)
			isRunning = (engine.run() != -1);
		else if(rc_e == -1)
			isRunning = false;
	}
 
	while (isRunning)
	{
		switch (MainMenuScreen::menu())
		{
			case -1:
			{
				isRunning = false;
			}
			break;
			case MainMenuScreen::CAMPAIGN:
			{
				CampaignMainMenu ccs;
				int rccs=ccs.execute(globalContainer->gfx, 40);
				if(rccs == -1)
				{
					isRunning = false;
				}
			}
			break;
			case MainMenuScreen::TUTORIAL:
			{
				Campaign campaign;
				if(campaign.load("games/Tutorial_Campaign.txt"))
				{
					CampaignMenuScreen cms("games/Tutorial_Campaign.txt");
					int rc_cms=cms.execute(globalContainer->gfx, 40);
					if(rc_cms == -1)
					{
						isRunning = false;
					}
				}
				else
				{
					CampaignMenuScreen cms("campaigns/Tutorial_Campaign.txt");
					cms.setNewCampaign();
					int rc_cms=cms.execute(globalContainer->gfx, 40);
					if(rc_cms == -1)
					{
						isRunning = false;
					}
				}
			}
			break;
			case MainMenuScreen::LOAD_GAME:
			{
				Engine engine;
				int rc_e = engine.initLoadGame();
				if (rc_e == Engine::EE_NO_ERROR)
					isRunning = (engine.run() != -1);
				else if(rc_e == -1)
					isRunning = false;
			}
			break;
			case MainMenuScreen::CUSTOM:
			{
				bool cont=true;
				while(cont && isRunning)
				{
					Engine engine;
					int rc_e = engine.initCustom();
					if (rc_e ==  Engine::EE_NO_ERROR)
					{
						isRunning = (engine.run() != -1);
					}
					else if(rc_e == -1)
					{
						isRunning = false;
					}
					else
					{
						cont=false;	
					}
				}
			}
			break;
			case MainMenuScreen::MULTIPLAYERS_YOG:
			{
				mutiplayerYOG();
			}
			break;
			case MainMenuScreen::MULTIPLAYERS_LAN:
			{
				LANMenuScreen lanms;
				int rc_lms = lanms.execute(globalContainer->gfx, 40);
				if(rc_lms == -1)
					isRunning=false;
			}
			break;
			case MainMenuScreen::GAME_SETUP:
			{
				SettingsScreen settingsScreen;
				int rc_ss = settingsScreen.execute(globalContainer->gfx, 40);
				if( rc_ss == -1)
				{
					isRunning=false;
				}
			}
			break;
			case MainMenuScreen::EDITOR:
			{
				EditorMainMenu editorMainMenu;
				int rc=editorMainMenu.execute(globalContainer->gfx, 40);
				if (rc==-1)
				{
					isRunning=false;
				}
			}
			break;
			case MainMenuScreen::CREDITS:
			{
				CreditScreen creditScreen;
				if (creditScreen.execute(globalContainer->gfx, 40)==-1)
					isRunning=false;
			}
			break;
			case MainMenuScreen::QUIT:
			{
				isRunning=false;
			}
			break;
			default:
			break;
		}
	}

	// This is for the textshot code
	GAGCore::DrawableSurface::printFinishingText();
	delete globalContainer;

#endif  // !YOG_SERVER_ONLY

	return 0;
}

int main(int argc, char *argv[])
{
	// Line-buffer stderr/stdout so abort() and assert failures don't swallow
	// the last log line. macOS block-buffers redirected stdio, and abort()
	// is not required to flush — without this, "fprintf(stderr, ...) ; abort()"
	// loses the message whenever stderr is a redirected file.
	setvbuf(stderr, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);

#if defined(__APPLE__) && !defined(YOG_SERVER_ONLY)
	/* SDL has this annoying "feature" of setting working directory to parent
	   of bundle during static initialization.  We want to set it back to the
	   main bundle directory so we can find our Resources directory. */
	CFBundleRef mainBundle = CFBundleGetMainBundle();
	assert(mainBundle);
	CFURLRef mainBundleURL = CFBundleCopyBundleURL(mainBundle);
	assert(mainBundleURL);
	CFStringRef cfStringRef = CFURLCopyFileSystemPath(mainBundleURL, kCFURLPOSIXPathStyle);
	assert(cfStringRef);

	char path[MAXPATHLEN];
	CFStringGetCString(cfStringRef, path, MAXPATHLEN, kCFStringEncodingASCII);
	chdir(path);

	CFRelease(mainBundleURL);
	CFRelease(cfStringRef);
#endif

	Glob2 glob2;
	return glob2.run(argc, argv);
}
