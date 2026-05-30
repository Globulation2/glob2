// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière

// Command-line argument parsing for GlobalContainer. Split out of
// GlobalContainer.cpp because parseArgs and its helpers are nearly
// 400 lines on their own and have no overlap with the asset/init code.

#include <sstream>

#include <Toolkit.h>
#include <GAG.h>

#include "AINames.h"
#include "FileManager.h"
#include "GlobalContainer.h"

// version related stuff
#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif
#ifndef PACKAGE_VERSION
	#define PACKAGE_VERSION "System Specific - not using autoconf"
#endif
#include "Version.h"
#include "NetConsts.h"

#include "GraphicContext.h"

namespace
{
	// Parse a comma-separated AI name list into AI::ImplementitionID values.
	// Used by --ai-types (which warns and skips on unknown) and --matchup
	// (which warns and exits(1) on unknown). The flag name is included in the
	// stderr message; valid AI names are kept canonical here so both flags
	// stay in sync.
	void parseAIList(const char* flagName, const std::string& list,
	                 std::vector<int>& out, bool exitOnUnknown)
	{
		std::stringstream ss(list);
		std::string item;
		while (std::getline(ss, item, ','))
		{
			int matched = AINames::parseAIName(item);
			if (matched > 0)
			{
				out.push_back(matched);
			}
			else
			{
				std::cerr << flagName << ": unknown AI '" << item
					<< "' (valid: numbi, castor, warrush, reachtoinfinity, nicowar)" << std::endl;
				if (exitOnUnknown)
					exit(1);
			}
		}
	}
}

/**
 * parses all command line arguments
 * @param argc number of arguments
 * @param argv the arguments themselves
 * @see Glob2::main()
 */
void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
#ifndef YOG_SERVER_ONLY
		if (strcmp(argv[i], "-nox")==0 || strcmp(argv[i], "--nox")==0)
		{
			bool good=true;
			if (i + 3 < argc)
			{
				runNoXGameName = argv[i + 1];
				runNoX = true;
				automaticEndingGame = true;
				good &= (sscanf(argv[i + 2], "%d", &automaticEndingSteps) == 1);
				good &= (sscanf(argv[i + 3], "%d", &runNoXCountRuns) == 1);
				i += 3;
			}
			else
			{
				good=false;
			}
			if(!good)
			{
				printf("usage:\n");
				printf("--nox <game file name> <number of steps> <number of runs>\n");
				printf("zero steps will make the game run until the end.\n");
				printf("\n");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "-daemon")==0)
		{
			runNoX=true;
			hostServer=true;
		}
		else if (strcmp(argv[i], "-router")==0)
		{
			runNoX=true;
			hostRouter=true;
		}
		else if (strcmp(argv[i], "-admin-router")==0)
		{
			runNoX=true;
			adminRouter=true;
		}
		else if (strcmp(argv[i], "-test-games")==0 || strcmp(argv[i], "-test-games-nox")==0)
		{
			runTestGames=true;
			automaticEndingGame = true;
			automaticGameGlobalEndConditions=true;
			if (strcmp(argv[i], "-test-games-nox")==0)
				runNoX=true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				runTestGamesCount = atoi(argv[i + 1]);
				i++;
			}
		}
		else if (strcmp(argv[i], "-test-map-gen")==0)
		{
			runTestMapGeneration = true;
			runNoX=true;
		}
		else if (strcmp(argv[i], "--ai-types")==0)
		{
			// Constrain the random AI pool used by createRandomGame for
			// -test-games / -test-games-nox. Comma-separated AI names,
			// case-insensitive (see AINames::parseAIName). Unknown names
			// are reported on stderr and skipped. Empty pool (default)
			// means "use all main AIs uniformly".
			if (i + 1 < argc)
			{
				parseAIList("--ai-types", argv[i + 1], testGamesAIPool, false);
				i++;
			}
			else
			{
				printf("--ai-types <comma-separated-list> requires an argument\n");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "--map")==0)
		{
			// Pin the random-game map. Bare name, no .map extension —
			// resolved as maps/<name>.map by Engine::chooseRandomMap.
			if (i + 1 < argc)
			{
				testGamesMap = argv[i + 1];
				i++;
			}
			else
			{
				printf("--map <name> requires an argument\n");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "--matchup")==0)
		{
			// Per-team AI assignment for the random-game flow. Comma-
			// separated AI names; matchup[k] is the AI for team k.
			// Validated against the loaded map's getNumberOfTeams() in
			// Engine::createRandomGame() before the game starts.
			if (i + 1 < argc)
			{
				parseAIList("--matchup", argv[i + 1], testGamesMatchup, true);
				i++;
			}
			else
			{
				printf("--matchup <comma-separated-list> requires an argument\n");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "--save-game-as")==0)
		{
			// Write the fully-initialised tick-0 game state to a .game file
			// before running. Lets a -test-games-nox scenario be replayed
			// deterministically via --nox <path>. Pair with GLOB2_TEST_SEED
			// for a fully reproducible scenario: the same env var seeds
			// syncRand AND gets mirrored into GameHeader::seed at save time
			// (see Engine::createRandomGame in engine_init.cpp).
			if (i + 1 < argc)
			{
				testGamesSaveGameAs = argv[i + 1];
				i++;
			}
			else
			{
				printf("--save-game-as <path> requires an argument\n");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "-vs")==0)
		{
			if (i+1 < argc)
			{
				videoshotName = argv[i+1];
				i++;
			}
			else
			{
				printf("usage:\n");
				printf("-vs <videoshot name>");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "-textshot")==0)
		{
			if(i+1 < argc)
			{
				GAGCore::DrawableSurface::translationPicturesDirectory = argv[i+1];
				i++;
			}
			else
			{
				GAGCore::DrawableSurface::translationPicturesDirectory = ".";
			}
		}
		else if (strcmp(argv[i], "-f")==0)
		{
			settings.screenFlags |= GraphicContext::FULLSCREEN;
		}
		else if (strcmp(argv[i], "-F")==0)
		{
			settings.screenFlags &= ~GraphicContext::FULLSCREEN;
		}

		else if (strcmp(argv[i], "-c")==0)
		{
			settings.screenFlags |= GraphicContext::CUSTOMCURSOR;
		}
		else if (strcmp(argv[i], "-C")==0)
		{
			settings.screenFlags &= ~GraphicContext::CUSTOMCURSOR;
		}

		else if (strcmp(argv[i], "-r")==0)
		{
			settings.screenFlags |= GraphicContext::RESIZABLE;
		}
		else if (strcmp(argv[i], "-R")==0)
		{
			settings.screenFlags &= ~GraphicContext::RESIZABLE;
		}

		else if  (strcmp(argv[i], "-sgsl")==0)
		{
			settings.optionFlags &= ~OPTION_MAP_EDIT_USE_USL;
		}
		else if (strcmp(argv[i], "-usl")==0)
		{
			settings.optionFlags |= OPTION_MAP_EDIT_USE_USL;
		}

		else if (strcmp(argv[i], "-g")==0)
		{
			settings.screenFlags |= GraphicContext::USEGPU;
		}
		else if (strcmp(argv[i], "-G")==0)
		{
			settings.screenFlags &= ~GraphicContext::USEGPU;
		}

		else if (strcmp(argv[i], "-l")==0)
		{
			settings.optionFlags |= OPTION_LOW_SPEED_GFX;
		}
		else if (strcmp(argv[i], "-h")==0)
		{
			settings.optionFlags &= ~OPTION_LOW_SPEED_GFX;
		}
		else if (strcmp(argv[i], "-m")==0)
		{
			settings.mute = 1;
		}
		else if (strcmp(argv[i], "-M")==0)
		{
			settings.mute = 0;
		}
		else if (strcmp(argv[i], "-replay")==0)
		{
			if (i+1 < argc)
			{
				replaying=true;
				replayFileName=argv[i+1];
				i++;
			}
			else
			{
				printf("usage:\n");
				printf("-replay <replay file name>\n");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "-y")==0)
		{
			if(i+1 < argc)
			{
				// TODO: Let this option really change hostname.
				yogHostName = argv[i+1];
				i++;
			}
			else
			{
				printf("usage:\n");
				printf("-y <hostname>");
				exit(0);
			}
		}
		else if (strcmp(argv[i],"-s")==0)
		{
			if (i+1 < argc)
			{
				i++;
				const char *resStr=&(argv[i][0]);
				int ix, iy;
				int nscaned = sscanf(resStr, "%dx%dx", &ix, &iy);
				if (nscaned == 2)
				{
					if (ix!=0 && iy!=0)
					{
						if (ix<640)
							ix=640;
						settings.screenWidth = ix;
						if (iy<480)
							iy=480;
						settings.screenHeight = iy;
					}
				}
			}
		}
		else if (strcmp(argv[i], "-d")==0)
		{
			if(i+1 < argc)
			{
				fileManager->addDir(argv[i+1]);
				i++;
			}
			else
			{
				printf("usage:\n");
				printf("-d <directory>");
				exit(0);
			}
		}
		else if (strcmp(argv[i], "-dl")==0)
		{
			std::cout << "Glob2 will fuse the following directories into its virtual filesystem:\n";
			const unsigned dirCount(fileManager->getDirCount());
			for (unsigned d = 0; d < dirCount; ++d)
			{
				std::cout << d << "\t" << fileManager->getDir(d) << std::endl;
			}
			exit(0);
		}
		else if (strcmp(argv[i], "-u")==0)
		{
			if(i+1 < argc)
			{
				settings.setUsername(argv[i+1]);
				i++;
			}
			else
			{
				printf("usage:\n");
				printf("-u <username>");
				exit(0);
			}
		}
		else
#endif  // !YOG_SERVER_ONLY
			if (strcmp(argv[i], "-version")==0 || strcmp(argv[i], "--version")==0)
		{
			printf("\nGlobulation 2 - %s\n\n", PACKAGE_VERSION);
			printf("Compiled on %s at %s\n\n", __DATE__, __TIME__);
			SDL_version v;
			SDL_VERSION(&v);
			printf("Compiled with SDL version %d.%d.%d\n", v.major, v.minor, v.patch);
			SDL_GetVersion(&v);
			printf("Linked with SDL version %d.%d.%d\n\n", v.major, v.minor, v.patch);
			printf("Featuring :\n");
			printf("* Map version %d\n", VERSION_MINOR);
			printf("* Maps up to version %d can still be loaded\n", MINIMUM_VERSION_MINOR);
			printf("* Network Protocol version %d\n", NET_PROTOCOL_VERSION);
			printf("This program and all related materials are GPL, see COPYING for details.\n");
			printf("(C) 2001-2007 Stephane Magnenat, Luc-Olivier de Charriere and other contributors.\n");
			printf("See data/authors.txt for a full list.\n\n");
			printf("Type %s --help for a list of command line options.\n\n", argv[0]);
			exit(0);
		}
		else if (strcmp(argv[i], "/?")==0 || strcmp(argv[i], "--help")==0)
		{
			printf("\nGlobulation 2\n");
			printf("Command line arguments:\n");
			printf("switches:\n");
#ifndef YOG_SERVER_ONLY
			printf("-c/-C\tenable/disable custom cursor\n");
			printf("-f/-F\tset/clear full screen\n");
			printf("-g/-G\tenable/disable OpenGL acceleration (GPU use)\n");
			printf("-h\thigh speed graphics: max of transparency effects\n");
			printf("-l\tlow speed graphics: disable some transparency effects\n");
			printf("-m/-M\tmute/unmute the sound (both music and speech)\n");
			printf("-r/-R\tset/clear resizable window\n");
			printf("-sgsl\tedit SGSL script in the map editor (default)\n");
			printf("-usl\tedit USL script in the map editor\n");
			printf("\n");
			printf("-d <directory>\tadd a directory to the directory search list\n");
			printf("-dl\tprint the directory search list\n");
			printf("-s <resolution>\tset resolution and depth (for instance : -s 640x480\n");
			printf("-u <username>\tspecify a user name\n");
			printf("-y <hostname>\tspecify an alternative hostname for YOG server\n");
			printf("-daemon\t runs the YOG server\n");
			printf("-router\t runs the YOG game router\n");
			printf("-nox <game file name> \t runs the game without using the X server\n");
			printf("-textshot <directory>\t takes pictures of various translation texts as they are drawn on the screen, requires the convert command\n");
			printf("-test-games\tCreates random games with AI and tests them\n");
			printf("-test-games-nox\tCreates random games with AI and tests them, without gui\n");
			printf("--ai-types <list>\tcomma-separated AI names to draw from in -test-games* (default: all)\n");
			printf("\t\tvalid: numbi, castor, warrush, reachtoinfinity, nicowar\n");
			printf("--map <name>\tpin the map for -test-games* (resolved as maps/<name>.map)\n");
			printf("--matchup <list>\tcomma-separated per-team AI names; matchup[k] plays team k\n");
			printf("\t\trequires --map; mutually exclusive with --ai-types\n");
			printf("--save-game-as <path>\twrite the tick-0 .game file before running -test-games*\n");
			printf("\t\t(pair with GLOB2_TEST_SEED for a reproducible scenario)\n");
			printf("-test-map-gen\tGenerates random maps endlessly, without gui\n");
			printf("-admin-router Allows you to connect to a YOG router to do administration\n");
			printf("-vs <name>\tsave a videoshot as name\n");
			printf("-replay <replay file name>\t replay the game stored in the specified file.\n");
#endif  // !YOG_SERVER_ONLY
			printf("-version\tprint the version and exit\n");
			exit(0);
		}
	}

	// Cross-flag validation for the random-game family. Fail-fast here
	// before any expensive setup (map listing, etc.) runs.
	if (!testGamesMatchup.empty() && testGamesMap.empty())
	{
		std::cerr << "--matchup requires --map; we need to know the map's "
			<< "team count to validate the matchup before starting a game"
			<< std::endl;
		exit(1);
	}
	if (!testGamesMatchup.empty() && !testGamesAIPool.empty())
	{
		std::cerr << "--matchup and --ai-types are mutually exclusive: "
			<< "--matchup pins each team's AI; --ai-types randomizes within "
			<< "a pool. Use one or the other." << std::endl;
		exit(1);
	}
	if (!testGamesSaveGameAs.empty() && !runTestGames)
	{
		std::cerr << "--save-game-as requires -test-games or -test-games-nox; "
			<< "the save happens at random-game creation time, which only "
			<< "runs in those modes" << std::endl;
		exit(1);
	}
}
