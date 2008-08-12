/*
  Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <string.h>

#include <Toolkit.h>
#include <GAG.h>
#include <GUIBase.h>

#include "FileManager.h"
#include "GameGUIKeyActions.h"
#include "Glob2Screen.h"
#include "Glob2Style.h"
#include "GlobalContainer.h"
#include "Header.h"
#include "IntBuildingType.h"
#include "KeyboardManager.h"
#include "LogFileManager.h"
#include "MapEditKeyActions.h"
#include "NonANSICStdWrapper.h"
#include "Player.h"
#include "Race.h"
#include "SoundMixer.h"
#include "UnitsSkins.h"
#include "VoiceRecorder.h"

// version related stuff
#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif
#ifndef PACKAGE_VERSION
	#define PACKAGE_VERSION "System Specific - not using autoconf"
#endif
#include "Version.h"
#include "YOGConsts.h"
#include "NetConsts.h"

#include "GraphicContext.h"

GlobalContainer::GlobalContainer(void)
{
	// Init toolkit
	Toolkit::init("glob2");

	// init virtual filesystem
	fileManager = Toolkit::getFileManager();
	assert(fileManager);
	fileManager->addWriteSubdir("maps");
	fileManager->addWriteSubdir("games");
	fileManager->addWriteSubdir("campaigns");
	fileManager->addWriteSubdir("thumbnails");
	fileManager->addWriteSubdir("gamelog ");
	fileManager->addWriteSubdir("logs");
	fileManager->addWriteSubdir("scripts");
	fileManager->addWriteSubdir("videoshots");
	logFileManager = new LogFileManager(fileManager);
	
	// load user preference
	settings.load();
	userName = settings.username.c_str();
	runNoX = false;
	
	hostServer = false;
	hostRouter = false;
	adminRouter = false;
	hostServerMapName[0] = 0;
	hostServerUserName[0] = 0;
	hostServerPassWord[0] = 0;
	
	runTestGames=false;
	runTestMapGeneration=false;
	automaticEndingGame=false;
	automaticEndingSteps=-1;
	
	gfx = NULL;
	mix = NULL;
	terrain = NULL;
	terrainShader = NULL;
	terrainBlack = NULL;
	ressources = NULL;
	units = NULL;
	unitsSkins = NULL;

	menuFont = NULL;
	standardFont = NULL;
	littleFont = NULL;
	
	voiceRecorder = NULL;
	automaticGameGlobalEndConditions=false;

	assert((int)USERNAME_MAX_LENGTH==(int)BasePlayer::MAX_NAME_LENGTH);
}

GlobalContainer::~GlobalContainer(void)
{
	// unlink GUI style
	if(!runNoX)
		delete Style::style;
	Style::style = &defaultStyle;
	
	// release unit skins
	if (unitsSkins)
		delete unitsSkins;
	
	// close sound
	if (mix)
		delete mix;
	
	if (voiceRecorder)
		delete voiceRecorder;
	
	// delete title image
	delete title;
	
	// release ressources
	Toolkit::close();
	
	// close virtual filesystem
	delete logFileManager;
}

void GlobalContainer::setUserName(const std::string &name)
{
	settings.username.assign(name, 0, USERNAME_MAX_LENGTH);
	userName = settings.username;
}

void GlobalContainer::pushUserName(const std::string &name)
{
	userName = name;
}

void GlobalContainer::popUserName()
{
	userName = settings.username;
}

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
		if (strcmp(argv[i], "-nox")==0 || strcmp(argv[i], "--nox")==0)
		{
			bool good = true;
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
				good = false;
			if (!good)
			{
				printf("usage:\n");
				printf("--nox <game file name> <number of steps> <number of runs>\n");
				printf("zero steps will make the game run until the end.\n");
				printf("\n");
				exit(0);
			}
			continue;
		}
		if (strcmp(argv[i], "-daemon")==0)
		{
			runNoX=true;
			hostServer=true;
			continue;
		}
		if (strcmp(argv[i], "-router")==0)
		{
			runNoX=true;
			hostRouter=true;
			continue;
		}
		if (strcmp(argv[i], "-admin-router")==0)
		{
			runNoX=true;
			adminRouter=true;
			continue;
		}
		if (strcmp(argv[i], "-test-games")==0)
		{
			runTestGames=true;
			automaticEndingGame = true;
			automaticGameGlobalEndConditions=true;
			continue;
		}
		if (strcmp(argv[i], "-test-games-nox")==0)
		{
			runTestGames=true;
			automaticEndingGame = true;
			runNoX=true;
			automaticGameGlobalEndConditions=true;
			continue;
		}
		if (strcmp(argv[i], "-test-map-gen")==0)
		{
			runTestMapGeneration = true;
			runNoX=true;
			continue;
		}
		if (strcmp(argv[i], "-host")==0 || strcmp(argv[i], "--host")==0)
		{
			if (i+3<argc)
			{
				strncpy(hostServerMapName, argv[i+1], 32);
				strncpy(hostServerUserName, argv[i+2], 32);
				strncpy(hostServerPassWord, argv[i+3], 32);
				runNoX = true;
				hostServer = true;
				pushUserName(argv[i+2]);
				i += 3;
				pushUserName(hostServerUserName);
			}
			else
			{
				printf("usage:\n");
				printf("--host <map file name> <YOG username> <YOG password>\n\n");
				exit(0);
			}
			continue;
		}
		if (strcmp(argv[i], "-version")==0 || strcmp(argv[i], "--version")==0)
		{
			printf("\nGlobulation 2 - %s\n\n", PACKAGE_VERSION);
			printf("Compiled on %s at %s\n\n", __DATE__, __TIME__);
#ifndef DX9_BACKEND
			SDL_version v;
			SDL_VERSION(&v);
			printf("Compiled with SDL version %d.%d.%d\n", v.major, v.minor, v.patch);
			v = *SDL_Linked_Version();
			printf("Linked with SDL version %d.%d.%d\n\n", v.major, v.minor, v.patch);
#else
			printf("Using DirectX 9 Backend\n\n");
#endif
			printf("Featuring :\n");
			printf("* Map version %d\n", VERSION_MINOR);
			printf("* Maps up to version %d can still be loaded\n", MINIMUM_VERSION_MINOR);
			printf("* Network Protocol version %d\n", NET_PROTOCOL_VERSION);
			printf("This program and all related materials are GPL, see COPYING for details.\n");
			printf("(C) 2001-2007 Stephane Magnenat, Luc-Olivier de Charriere and other contributors.\n");
			printf("See AUTHORS for a full list.\n\n");
			printf("Type %s --help for a list of command line options.\n\n", argv[0]);
			exit(0);
		}
		if (strcmp(argv[i], "-vs")==0)
		{
			if (i+1 < argc)
			{
				videoshotName = argv[i+1];
				i++;
			}
		}
		if (strcmp(argv[i], "-textshot")==0)
		{
			if(i+1 < argc)
			{
				GAGCore::DrawableSurface::translationPicturesDirectory = argv[i+1];
				i++;
			}
			else
			{
				std::cerr<<"-textshot requires a directory. Use \"-textshot .\" for the current directory."<<std::endl;
			}
		}
		if (strcmp(argv[i], "-f")==0)
		{
			settings.screenFlags |= GraphicContext::FULLSCREEN;
			continue;
		}
		if (strcmp(argv[i], "-F")==0)
		{
			settings.screenFlags &= ~GraphicContext::FULLSCREEN;
			continue;
		}

		if (strcmp(argv[i], "-c")==0)
		{
			settings.screenFlags |= GraphicContext::CUSTOMCURSOR;
			continue;
		}
		if (strcmp(argv[i], "-C")==0)
		{
			settings.screenFlags &= ~GraphicContext::CUSTOMCURSOR;
			continue;
		}

		if (strcmp(argv[i], "-r")==0)
		{
			settings.screenFlags |= GraphicContext::RESIZABLE;
			continue;
		}
		if (strcmp(argv[i], "-R")==0)
		{
			settings.screenFlags &= ~GraphicContext::RESIZABLE;
			continue;
		}
		
		if (strcmp(argv[i], "-g")==0)
		{
			settings.screenFlags |= GraphicContext::USEGPU;
			continue;
		}
		if (strcmp(argv[i], "-G")==0)
		{
			settings.screenFlags &= ~GraphicContext::USEGPU;
			continue;
		}

		if (strcmp(argv[i], "-l")==0)
		{
			settings.optionFlags |= OPTION_LOW_SPEED_GFX;
			continue;
		}
		if (strcmp(argv[i], "-h")==0)
		{
			settings.optionFlags &= ~OPTION_LOW_SPEED_GFX;
			continue;
		}
		if (strcmp(argv[i], "-m")==0)
		{
			settings.mute = 1;
			continue;
		}
		if (strcmp(argv[i], "-M")==0)
		{
			settings.mute = 0;
			continue;
		}

		if (strcmp(argv[i], "/?")==0 || strcmp(argv[i], "--help")==0)
		{
			printf("\nGlobulation 2\n");
			printf("Command line arguments:\n");
			printf("-s\tset resolution and depth (for instance : -s640x480 or -s640x480x32)\n");
			printf("-f/-F\tset/clear full screen\n");
			printf("-r/-R\tset/clear resizable window\n");
			printf("-g/-G\tenable/disable OpenGL acceleration (GPU use)\n");
			printf("-c/-C\tenable/disable custom cursor\n");
			printf("-l\tlow speed graphics: disable some transparency effects\n");
			printf("-h\thigh speed graphics: max of transparency effects\n");
			printf("-v\tset the music volume\n");
			printf("-m/-M\tmute/unmute the sound (both music and speech)\n");
			printf("-d\tadd a directory to the directory search list\n");
			printf("-u\tspecify a user name\n");
			printf("-y\tspecify an alternative hostname for YOG server\n");
			printf("-host <map file name> <YOG username> <YOG password>\t runs only as a YOG game host text-based server\n");
			printf("-daemon\t runs the YOG server\n");
			printf("-router\t runs the YOG game router\n");
			printf("-nox <game file name> \t runs the game without using the X server\n");
			printf("-textshot <directory>\t takes pictures of various translation texts as they are drawn on the screen, requires the convert command\n");
			printf("-test-games\tCreates random games with AI and tests them\n");
			printf("-test-games-nox\tCreates random games with AI and tests them, without gui\n");
			printf("-test-map-gen\tGenerates random maps endlessly, without gui\n");
			printf("-admin-router Allows you to connect to a YOG router to do administration\n");
			printf("-vs <name>\tsave a videoshot as name\n");
			printf("-version\tprint the version and exit\n");
			exit(0);
		}

		// the -d option appends a directory in the
		// directory search list.
		// the -m options set the meta sever name
		// the -p option set the port
		if (argv[i][0] == '-')
		{
			if(argv[i][1] == 'd')
			{
				if (argv[i][2] != 0)
					fileManager->addDir(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						fileManager->addDir(argv[i]);
				}
			}
			else if (argv[i][1] == 'y')
			{
				if (argv[i][2] != 0)
					yogHostName = &argv[i][2];
				else
				{
					i++;
					if (i < argc)
						yogHostName = argv[i];
				}
			}
			else if (argv[i][1] == 'u')
			{
				if (argv[i][2] != 0)
					setUserName(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						setUserName(argv[i]);
				}
			}
			else if (argv[i][1] == 'v')
			{
				if (argv[i][2] != 0)
					settings.musicVolume=atoi(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						settings.musicVolume=atoi(argv[i]);
				}
			}
			else if (argv[i][1] == 's')
			{
				const char *resStr=&(argv[i][2]);
				int ix, iy;
				int nscaned = sscanf(resStr, "%dx%dx", &ix, &iy);
				if (nscaned == 2)
				{
					if (ix!=0)
					{
						ix&=~(0x1F);
						if (ix<640)
							ix=640;
						settings.screenWidth = ix;
					}
					if (iy!=0)
					{
						iy&=~(0x1F);
						if (iy<480)
							iy=480;
						settings.screenHeight = iy;
					}
				}
			}
		}
	}
}

void GlobalContainer::updateLoadProgressScreen(int value)
{
	unsigned randomSeed = 1;
	unsigned columnCount = gfx->getW() / 32;
	unsigned limit = (value * columnCount) / 100;
	for (int y = 0; y < gfx->getH(); y += 32)
		for (int x = 0; x < gfx->getW(); x += 32)
		{
			randomSeed = randomSeed * 69069;
			unsigned index;
			if (x/32 < limit)
				index = ((randomSeed >> 16) & 0xF);
			else if (x/32 == limit)
				index = ((randomSeed >> 16) & 0x7) + 64;
			else
				index = ((randomSeed >> 16) & 0xF) + 128;
			gfx->drawSprite(x, y, terrain, index);
		}
	//gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), Color::black);
	gfx->drawSurface((gfx->getW()-title->getW())>>1, (gfx->getH()-title->getH())>>1, title);
	//gfx->drawFilledRect(((gfx->getW()-400)>>1), (gfx->getH()>>1)+11+180, (value)<<2, 20, 10, 50, 255, 80);
	gfx->nextFrame();
}


void GlobalContainer::load(void)
{
	// load texts
	if (!Toolkit::getStringTable()->load("data/texts.list.txt"))
	{
		std::cerr << "Fatal error : while loading \"data/texts.list.txt\"" << std::endl;
		assert(false);
		exit(-1);
	}
	// load texts
	if (!Toolkit::getStringTable()->loadIncompleteList("data/texts.incomplete.txt"))
	{
		std::cerr << "Fatal error : while loading \"data/texts.incomplete.txt\"" << std::endl;
		assert(false);
		exit(-1);
	}
	
	Toolkit::getStringTable()->setLang(Toolkit::getStringTable()->getLangCode(settings.language));
		

	if (!runNoX)
	{
		// create graphic context
		gfx = Toolkit::initGraphic(settings.screenWidth, settings.screenHeight, settings.screenFlags, "Globulation 2", "glob 2");
		gfx->setMinRes(640, 480);
		//gfx->setQuality((settings.optionFlags & OPTION_LOW_SPEED_GFX) != 0 ? GraphicContext::LOW_QUALITY : GraphicContext::HIGH_QUALITY);
		
		// load data required for drawing progress screen
		title = new DrawableSurface("data/gfx/title.png");
		terrain = Toolkit::getSprite("data/gfx/terrain");
		updateLoadProgressScreen(0);
		
		// create mixer
		mix = new SoundMixer(settings.musicVolume, settings.voiceVolume, settings.mute);
		mix->loadTrack("data/zik/intro.ogg");
		mix->loadTrack("data/zik/menu.ogg");
		mix->loadTrack("data/zik/a1.ogg");
		mix->loadTrack("data/zik/a2.ogg");
		mix->loadTrack("data/zik/a3.ogg");
		mix->setNextTrack(0);
		mix->setNextTrack(1);
		
		// create voice recorder
		voiceRecorder = new VoiceRecorder();
		
		updateLoadProgressScreen(15);
	}
	
	// load buildings types
	buildingsTypes.load();
	IntBuildingType::init();
	
	if (!runNoX)
	{
		updateLoadProgressScreen(35);
	}
	
	// load default unit types
	Race::loadDefault();
	// load ressources types
	ressourcesTypes.load("data/ressources.txt");
	
	// initiate keyboard actions
	GameGUIKeyActions::init();
	MapEditKeyActions::init();
	
	if (settings.version < 1)
	{
		KeyboardManager game(GameGUIShortcuts);
		game.loadDefaultShortcuts();
		game.saveKeyboardLayout();

		KeyboardManager edit(MapEditShortcuts);
		edit.loadDefaultShortcuts();
		edit.saveKeyboardLayout();
	}
	
	if (!runNoX)
	{
		updateLoadProgressScreen(40);
		
		// load fonts
		Toolkit::loadFont("data/fonts/cyberbit.ttf", 20, "menu");
		Toolkit::loadFont("data/fonts/cyberbit.ttf", 13, "standard");
		Toolkit::loadFont("data/fonts/cyberbit.ttf", 10, "little");
		menuFont = Toolkit::getFont("menu");
		menuFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));
		standardFont = Toolkit::getFont("standard");
		standardFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));
		littleFont = Toolkit::getFont("little");
		littleFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));

		updateLoadProgressScreen(50);
		// load terrain data
		//terrain = Toolkit::getSprite("data/gfx/terrain"); // terrain is already loaded as it is required to display progress screen
		terrainWater = Toolkit::getSprite("data/gfx/water");
		terrainCloud = Toolkit::getSprite("data/gfx/cloud");
		
		// black for unexplored terrain
		terrainBlack = Toolkit::getSprite("data/gfx/black");

		// load shader for unvisible terrain
		terrainShader = Toolkit::getSprite("data/gfx/shade");
		
		updateLoadProgressScreen(60);
		// load ressources
		ressources = Toolkit::getSprite("data/gfx/ressource");
		ressourceMini = Toolkit::getSprite("data/gfx/ressourcemini");
		areas = Toolkit::getSprite("data/gfx/area");
		bullet = Toolkit::getSprite("data/gfx/bullet");
		bulletExplosion = Toolkit::getSprite("data/gfx/explosion");
		deathAnimation = Toolkit::getSprite("data/gfx/death"); 

		updateLoadProgressScreen(70);
		// load units
		units = Toolkit::getSprite("data/gfx/unit");
		unitsSkins = new UnitsSkins();

		updateLoadProgressScreen(90);
		// load graphics for gui
		unitmini = Toolkit::getSprite("data/gfx/unitmini");
		gamegui = Toolkit::getSprite("data/gfx/gamegui");
		brush = Toolkit::getSprite("data/gfx/brush");
		magiceffect = Toolkit::getSprite("data/gfx/magiceffect");
		particles = Toolkit::getSprite("data/gfx/particle");
		
		// use custom style
		Style::style = new Glob2Style;

		updateLoadProgressScreen(100);
	}
};

Uint32 GlobalContainer::getConfigCheckSum()
{
	// TODO: add the units config
	return buildingsTypes.checkSum() + ressourcesTypes.checkSum() + Race::checkSumDefault();
}

const char *GlobalContainer::getComputerHostName(void)
{
	const char *hostNameFromEnvVar = getenv("HOSTNAME");
	if (hostNameFromEnvVar)
		return hostNameFromEnvVar;
	else
		return "localhost";
}

