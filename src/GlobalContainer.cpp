/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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

#include "FileManager.h"
#include "GlobalContainer.h"
#include "Header.h"
#include "LogFileManager.h"
#include "NonANSICStdWrapper.h"
#include "Player.h"
#include "SoundMixer.h"
#include "IntBuildingType.h"

// version related stuff
#ifdef HAVE_CONFIG_H
	#include <config.h>
#else
	#define PACKAGE_VERSION "System Specific - not using autoconf"
#endif
#include "Version.h"
#include "YOGConsts.h"
#include "NetConsts.h"


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
	fileManager->addWriteSubdir("logs");
	fileManager->addWriteSubdir("scripts");
	logFileManager = new LogFileManager(fileManager);

	// load user preference
	settings.load();
	userName = settings.username.c_str();

	runNoX = false;
	runNoXGameName[0] = 0;
	
	hostServer = false;
	hostServerMapName[0] = 0;
	hostServerUserName[0] = 0;
	hostServerPassWord[0] = 0;
	
	gfx = NULL;
	mix = NULL;
	terrain = NULL;
	terrainShader = NULL;
	terrainBlack = NULL;
	ressources = NULL;
	units = NULL;

	menuFont = NULL;
	standardFont = NULL;
	littleFont = NULL;

	assert((int)USERNAME_MAX_LENGTH==(int)BasePlayer::MAX_NAME_LENGTH);
}

GlobalContainer::~GlobalContainer(void)
{
	// close sound
	if (mix)
		delete mix;
	
	// release ressources
	Toolkit::close();
	
	// close virtual filesystem
	delete logFileManager;
}

void GlobalContainer::setUserName(const char *name)
{
	settings.username.assign(name, USERNAME_MAX_LENGTH);
	userName = settings.username.c_str();
}

void GlobalContainer::pushUserName(const char *name)
{
	userName = name;
}

void GlobalContainer::popUserName()
{
	userName = settings.username.c_str();
}

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
		if (strcmp(argv[i], "-nox")==0 || strcmp(argv[i], "--nox")==0)
		{
			if (i+1<argc)
			{
				strncpy(runNoXGameName, argv[i+1], 32);
				runNoX = true;
				i++;
			}
			else
			{
				printf("usage:\n");
				printf("--nox <game file name>\n\n");
				exit(0);
			}
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
			printf("* YOG Protocol version %d\n\n", YOG_PROTOCOL_VERSION);
			printf("This program and all related materials are GPL, see COPYING for details.\n");
			printf("(C) 2001-2004 Stephane Magnenat, Luc-Olivier de Charriere and other contributors.\n");
			printf("See AUTHORS for a full list.\n\n");
			printf("Type %s --help for a list of command line options.\n\n", argv[0]);
			exit(0);
		}
		if (strcmp(argv[i], "-f")==0)
		{
			settings.screenFlags |= DrawableSurface::FULLSCREEN;
			continue;
		}
		if (strcmp(argv[i], "-F")==0)
		{
			settings.screenFlags &= ~DrawableSurface::FULLSCREEN;
			continue;
		}

		if (strcmp(argv[i], "-a")==0)
		{
			settings.screenFlags |= DrawableSurface::HWACCELERATED;
			continue;
		}
		if (strcmp(argv[i], "-A")==0)
		{
			settings.screenFlags &= ~DrawableSurface::HWACCELERATED;
			continue;
		}
		
		if (strcmp(argv[i], "-c")==0)
		{
			settings.screenFlags |= DrawableSurface::CUSTOMCURSOR;
			continue;
		}
		if (strcmp(argv[i], "-C")==0)
		{
			settings.screenFlags &= ~DrawableSurface::CUSTOMCURSOR;
			continue;
		}

		if (strcmp(argv[i], "-r")==0)
		{
			settings.screenFlags |= DrawableSurface::RESIZABLE;
			continue;
		}
		if (strcmp(argv[i], "-R")==0)
		{
			settings.screenFlags &= ~DrawableSurface::RESIZABLE;
			continue;
		}

		if (strcmp(argv[i], "-b")==0)
		{
			settings.screenFlags |= DrawableSurface::DOUBLEBUF;
			continue;
		}
		if (strcmp(argv[i], "-B")==0)
		{
			settings.screenFlags &= ~DrawableSurface::DOUBLEBUF;
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
			settings.musicVolume=0;
			continue;
		}

		if (strcmp(argv[i], "/?")==0 || strcmp(argv[i], "--help")==0)
		{
			printf("\nGlobulation 2\n");
			printf("Command line arguments:\n");
			printf("-t\ttype of gfx rendere: 0 = SDL, 1 = OpenGL\n");
			printf("-s\tset resolution and depth (for instance : -s640x480 or -s640x480x32)\n");
			printf("-f/-F\tset/clear full screen\n");
			printf("-r/-R\tset/clear resizable window\n");
			printf("-a/-A\tset/clear hardware accelerated gfx\n");
			printf("-b/-B\tenable/disable double buffering (useful on OS X in fullscreen)\n");
			printf("-c/-C\tenable/disable custom cursor\n");
			printf("-l\tlow speed graphics: disable some transparency effects\n");
			printf("-h\thigh speed graphics: max of transparency effects\n");
			printf("-v\tset the music volume\n");
			printf("-m\tmute the music\n");
			printf("-d\tadd a directory to the directory search list\n");
			printf("-u\tspecify a user name\n");
			printf("-host <map file name> <YOG username> <YOG password>\t runs only as a YOG game host text-based server\n");
			printf("-nox <game file name> \t runs the game without using the X server\n");
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
			else if (argv[i][1] == 't')
			{
				if (argv[i][2] != 0)
					settings.graphicType=(DrawableSurface::GraphicContextType)atoi(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						settings.graphicType=(DrawableSurface::GraphicContextType)atoi(argv[i]);
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
				int ix, iy, id;
				int nscaned = sscanf(resStr, "%dx%dx%d", &ix, &iy, &id);
				if (nscaned > 1)
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
				if (nscaned > 2)
				{
					if ((id == 16) || (id == 32))
						settings.screenDepth = id;
				}
			}
		}
	}
}

struct ProgressBar
{
	DrawableSurface *s;
} progress;

void GlobalContainer::updateLoadProgressBar(int value)
{
	gfx->drawSurface((gfx->getW()-progress.s->getW())>>1, (gfx->getH()-progress.s->getH())>>1, progress.s);
	gfx->drawFilledRect(((gfx->getW()-400)>>1), (gfx->getH()>>1)+11+180, (value)<<2, 20, 10, 50, 255, 80);
	gfx->nextFrame();
}

void GlobalContainer::initProgressBar(void)
{
	progress.s = new DrawableSurface();
	progress.s->loadImage("data/gfx/IntroMN.png");
	progress.s->drawRect((progress.s->getW()-402)>>1, (progress.s->getH()>>1)+10+180, 402, 22, 180, 180, 180);
	gfx->drawSurface((gfx->getW()-progress.s->getW())>>1, (gfx->getH()-progress.s->getH())>>1, progress.s);
	gfx->nextFrame();
}

void GlobalContainer::destroyProgressBar(void)
{
	delete progress.s;
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
	Toolkit::getStringTable()->setLang(settings.defaultLanguage);

	if (!runNoX)
	{
		// create graphic context
		Toolkit::initGraphic();
		// and set res
		gfx = Toolkit::getGraphicContext();
		gfx->setMinRes(640, 480);
		gfx->setRes(settings.screenWidth, settings.screenHeight, globalContainer->settings.screenDepth, settings.screenFlags, (DrawableSurface::GraphicContextType)settings.graphicType);
		gfx->setQuality((settings.optionFlags & OPTION_LOW_SPEED_GFX) != 0 ? GraphicContext::LOW_QUALITY : GraphicContext::HIGH_QUALITY);
		gfx->setCaption("Globulation 2", "glob 2");
	}
	
	if (!runNoX)
	{
		initProgressBar();
		
		// create mixer
		mix = new SoundMixer(settings.musicVolume);
		mix->loadTrack("data/zik/intro.ogg");
		mix->loadTrack("data/zik/menu.ogg");
		mix->loadTrack("data/zik/a1.ogg");
		mix->loadTrack("data/zik/a2.ogg");
		mix->loadTrack("data/zik/a3.ogg");
		mix->setNextTrack(0);
		mix->setNextTrack(1);
		
		updateLoadProgressBar(10);
	}
	
	// load buildings types
	buildingsTypes.load();
	IntBuildingType::init();
	// load ressources types
	ressourcesTypes.load("data/ressources.txt");
	
	if (!runNoX)
	{
		updateLoadProgressBar(35);
		
		// load fonts
		Toolkit::loadFont("data/fonts/sans.ttf", 22, "menu");
		Toolkit::loadFont("data/fonts/sans.ttf", 14, "standard");
		Toolkit::loadFont("data/fonts/sans.ttf", 10, "little");
		menuFont = Toolkit::getFont("menu");
		standardFont = Toolkit::getFont("standard");
		littleFont = Toolkit::getFont("little");

		updateLoadProgressBar(45);
		// load terrain data
		terrain = Toolkit::getSprite("data/gfx/terrain");
		
		// black for unexplored terrain
		terrainBlack = Toolkit::getSprite("data/gfx/black");

		// load shader for unvisible terrain
		terrainShader = Toolkit::getSprite("data/gfx/shade");
		
		updateLoadProgressBar(65);
		// load ressources
		ressources = Toolkit::getSprite("data/gfx/ressource");
		ressourceMini = Toolkit::getSprite("data/gfx/ressourcemini");
		bullet = Toolkit::getSprite("data/gfx/bullet");
		bulletExplosion = Toolkit::getSprite("data/gfx/explosion"); 

		updateLoadProgressBar(70);
		// load units
		units = Toolkit::getSprite("data/gfx/unit");

		updateLoadProgressBar(90);
		// load graphics for gui
		unitmini = Toolkit::getSprite("data/gfx/unitmini");
		gamegui = Toolkit::getSprite("data/gfx/gamegui");
		brush = Toolkit::getSprite("data/gfx/brush");

		updateLoadProgressBar(100);
		destroyProgressBar();
	}
};
