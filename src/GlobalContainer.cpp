/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "GlobalContainer.h"
#include "Header.h"
#include <string.h>
#include "NonANSICStdWrapper.h"
#include "Player.h"
#include "FileManager.h"
#include "LogFileManager.h"

Settings::Settings()
{
	// set default values in settings or load them
	char *newUsername;
#	ifdef WIN32
		newUsername=getenv("USERNAME");
#	else // angel > case of unix and MacIntosh Systems
		newUsername=getenv("USER");
#	endif
	if (!newUsername)
		newUsername="player";
	username=newUsername;

	screenFlags=DrawableSurface::DEFAULT;
	screenWidth=640;
	screenHeight=480;
	graphicType=DrawableSurface::GC_SDL;
	optionFlags=0;
	defaultLanguage=0;
}

GlobalContainer::GlobalContainer(void)
{
	// init virtual filesystem
	fileManager=Toolkit::getFileManager();
	assert(fileManager);
	fileManager->addWriteSubdir("maps");
	fileManager->addWriteSubdir("games");
	fileManager->addWriteSubdir("logs");
	logFileManager=new LogFileManager(fileManager);

	// load user preference
	settings = deserialize<Settings>("preferences.xml");
	userName=settings->username.c_str();

	hostServer=false;
	gfx=NULL;
	terrain=NULL;
	terrainShader=NULL;
	terrainBlack=NULL;
	ressources=NULL;
	units=NULL;
	buildings=NULL;

	menuFont=NULL;
	standardFont=NULL;
	littleFont=NULL;

	assert((int)USERNAME_MAX_LENGTH==(int)BasePlayer::MAX_NAME_LENGTH);
}

GlobalContainer::~GlobalContainer(void)
{
	// save user preference
	settings->defaultLanguage = texts.getLang();
	base::XMLFileWriter xt("preferences.xml");
	base::TextOutputStream<base::XMLFileWriter> tos(&xt);
	tos.write(settings);

	// releasing ressources
	Toolkit::releaseSprite("terrain");
	Toolkit::releaseSprite("shading");
	Toolkit::releaseSprite("black");
	Toolkit::releaseSprite("ressources");
	Toolkit::releaseSprite("units");
	Toolkit::releaseSprite("buildings");
	Toolkit::releaseSprite("gamegui");
	Toolkit::releaseFont("menu");
	Toolkit::releaseFont("standard");
	Toolkit::releaseFont("little");
	if (gfx)
		delete gfx;
}

void GlobalContainer::setUserName(const char *name)
{
	settings->username.assign(name, USERNAME_MAX_LENGTH);
	userName=settings->username.c_str();
}

void GlobalContainer::pushUserName(const char *name)
{
	userName=name;
}

void GlobalContainer::popUserName()
{
	userName=settings->username.c_str();
}

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
		if (strcmp(argv[i], "-host")==0 || strcmp(argv[i], "--host")==0)
		{
			if (i+1<argc)
			{
				strncpy(hostServerMapName, argv[i+1], 32);
				hostServer=true;
				i++;
			}
			else
			{
				printf("usage: -host MapName.\n");
				hostServer=true;
				strncpy(hostServerMapName, "default.map", 32);
			}
			continue;
		}
		if (strcmp(argv[i], "-f")==0)
		{
			settings->screenFlags|=DrawableSurface::FULLSCREEN;
			continue;
		}

		if (strcmp(argv[i], "-a")==0)
		{
			settings->screenFlags|=DrawableSurface::HWACCELERATED;
			continue;
		}

		if (strcmp(argv[i], "-r")==0)
		{
			settings->screenFlags|=DrawableSurface::RESIZABLE;
			continue;
		}

		if (strcmp(argv[i], "-b")==0)
		{
			settings->screenFlags|=DrawableSurface::NO_DOUBLEBUF;
			continue;
		}

		if (strcmp(argv[i], "-l")==0)
		{
			settings->optionFlags|=OPTION_LOW_SPEED_GFX;
			continue;
		}

		if (strcmp(argv[i], "-h")==0 || strcmp(argv[i], "--help")==0)
		{
			printf("\nGlobulation 2\n");
			printf("Cmd line arguments :\n");
			printf("-f\tset full screen\n");
			printf("-r\tset resizable window\n");
			printf("-s\tset resolution (for instance : -s640x480)\n");
			printf("-a\tset hardware accelerated gfx\n");
			printf("-b\tdisable double buffering (usefull on OS X in fullscreen)\n");
			printf("-l\tlow speed graphics : disable some transparency effects\n");
			printf("-t\ttype of gfx rendere : 0 = SDL, 1 = OpenGL\n");
			printf("-d\tadd a directory to the directory search list\n");
			printf("-u\tspecify an user name\n");
			printf("-host MapName\t runs Globulation 2 as a game host text-only server\n\n");
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
					settings->graphicType=(DrawableSurface::GraphicContextType)atoi(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						settings->graphicType=(DrawableSurface::GraphicContextType)atoi(argv[i]);
				}
			}
			else if (argv[i][1] == 's')
			{
				const char *resStr=&(argv[i][2]);
				int ix, iy;
				sscanf(resStr, "%dx%d", &ix, &iy);
				if (ix!=0)
				{
					ix&=~(0x1F);
					if (ix<640)
						ix=640;
					settings->screenWidth=ix;
				}
				if (iy!=0)
				{
					iy&=~(0x1F);
					if (iy<480)
						iy=480;
					settings->screenHeight=iy;
				}
			}
		}
	}
}

void GlobalContainer::updateLoadProgressBar(int value)
{
	static int lastX=0;
	gfx->drawRect((gfx->getW()-402)>>1, (gfx->getH()>>1)+10+180, 402, 22, 180, 180, 180);
	gfx->drawFilledRect(((gfx->getW()-400)>>1)+(lastX<<2), (gfx->getH()>>1)+11+180, (value-lastX)<<2, 20, 10, 50, 255, 80);
	gfx->updateRect((gfx->getW()-402)>>1, (gfx->getH()>>1)-30+180, 402, 62);
	lastX=value;
}

void GlobalContainer::initProgressBar(void)
{
	//char *text;
	//text=texts.getString("[loading glob2]");
	gfx->loadImage("data/gfx/IntroMN.png");
	gfx->updateRect(0, 0, 0, 0);
	//gfx->drawString((gfx->getW()-menuFont->getStringWidth(text))>>1, (gfx->getH()>>1)-30, menuFont, "%s", text);
}

void GlobalContainer::load(void)
{
	// load texts
	if (!texts.load("data/texts.txt"))
	{
		fprintf(stderr, "Fatal error : the file \"data/texts.txt\" can't be found !");
		assert(false);
		exit(-1);
	}
	texts.setLang(settings->defaultLanguage);

	if (!hostServer)
	{
		// create graphic context
		gfx=GraphicContext::createGraphicContext((DrawableSurface::GraphicContextType)settings->graphicType);
		gfx->setRes(settings->screenWidth, settings->screenHeight, 32, settings->screenFlags);

		// load fonts
		gfx->loadFont("data/fonts/sans.ttf", 22, "menu");
		menuFont=Toolkit::getFont("menu");
		menuFont->setColor(255, 255, 255);

		gfx->loadFont("data/fonts/sans.ttf", 14, "standard");
		standardFont=Toolkit::getFont("standard");
		standardFont->setColor(255, 255, 255);

		gfx->loadFont("data/fonts/sans.ttf", 10, "little");
		littleFont=Toolkit::getFont("little");
		littleFont->setColor(255, 255, 255);

		initProgressBar();

		updateLoadProgressBar(10);
		// load terrain data
		gfx->loadSprite("data/gfx/terrain", "terrain");
		terrain=Toolkit::getSprite("terrain");

		// load shader for unvisible terrain
		gfx->loadSprite("data/gfx/shade", "shading");
		terrainShader=Toolkit::getSprite("shading");
		
		// black for unexplored terrain
		gfx->loadSprite("data/gfx/black", "black");
		terrainBlack=Toolkit::getSprite("black");

		updateLoadProgressBar(30);
		// load ressources
		gfx->loadSprite("data/gfx/ressource", "ressources");
		ressources=Toolkit::getSprite("ressources");

		updateLoadProgressBar(40);
		// load units
		gfx->loadSprite("data/gfx/unit", "units");
		units=Toolkit::getSprite("units");

		updateLoadProgressBar(70);
		// load buildings
		gfx->loadSprite("data/gfx/building", "buildings");
		buildings=Toolkit::getSprite("buildings");

		updateLoadProgressBar(90);
		// load graphics for gui
		gfx->loadSprite("data/gfx/unitmini", "buildings");
		unitmini=Toolkit::getSprite("buildings");
		gfx->loadSprite("data/gfx/gamegui", "gamegui");
		gamegui=Toolkit::getSprite("gamegui");

		updateLoadProgressBar(95);
		// load buildings types
		buildingsTypes.load("data/buildings.txt");

		// load ressources types
		ressourcesTypes=deserialize<RessourcesTypes>("data/ressources.xml");
		updateLoadProgressBar(100);
	}
};
