/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "GlobalContainer.h"
#include "SDL.h"
#include <string.h>
#include "NonANSICStdWrapper.h"

GlobalContainer::GlobalContainer(void)
{
	graphicFlags=DrawableSurface::DEFAULT;
	metaServerName=NULL;
	setMetaServerName("moneo.calodox.org");
	metaServerPort=3000;
}

GlobalContainer::~GlobalContainer(void)
{
	if (terrain)
		delete terrain;
	if (terrainShader)
		delete terrainShader;
	if (ressources)
		delete ressources;
	if (units)
		delete units;
	if (buildings)
		delete buildings;
	if (menuFont)
		delete menuFont;
	if (standardFont)
		delete standardFont;
	if (littleFontGreen)
		delete littleFontGreen;
	if (gfx)
		delete gfx;
	if (metaServerName)
		delete[] metaServerName;
}

void GlobalContainer::setMetaServerName(char *name)
{
	if (metaServerName)
		delete[] metaServerName;
	metaServerName=new char[strlen(name)+1];
	strcpy(metaServerName, name);
}

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
		if (strcmp(argv[i], "-f")==0)
		{
			graphicFlags|=DrawableSurface::FULLSCREEN;
			continue;
		}

		if (strcmp(argv[i], "-a")==0)
		{
			graphicFlags|=DrawableSurface::HWACCELERATED;
			continue;
		}

		if (strcmp(argv[i], "-h")==0)
		{
			printf("\nGlobulation 2\n");
			printf("Cmd line arguments :\n");
			printf("-f\tset full screen\n");
			printf("-a\tset hardware accelerated gfx\n");
			printf("-d\tadd a directory to the directory search list\n");
			printf("-m\tspecify meta server hostname\n");
			printf("-p\tspecify meta server port\n\n");
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
					fileManager.addDir(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						fileManager.addDir(argv[i]);
				}
			}
			else if (argv[i][1] == 'm')
			{
				if (argv[i][2] != 0)
					setMetaServerName(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						setMetaServerName(argv[i]);
				}
			}
			else if (argv[i][1] == 'p')
			{
				if (argv[i][2] != 0)
					metaServerPort=atoi(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						metaServerPort=atoi(argv[i]);
				}
			}
		}
	}
}

void GlobalContainer::updateLoadProgressBar(int value)
{
	gfx->drawRect((gfx->getW()-402)>>1, (gfx->getH()>>1)+10, 402, 22, 180, 180, 180);
	gfx->drawFilledRect((gfx->getW()-400)>>1, (gfx->getH()>>1)+11, value<<2, 20, 255, 255, 255);
	gfx->updateRect((gfx->getW()-402)>>1, (gfx->getH()>>1)-30, 402, 62);
}

void GlobalContainer::initProgressBar(void)
{
	char *text;
	text=texts.getString("[loading glob2]");
	gfx->drawString((gfx->getW()-menuFont->getStringWidth(text))>>1, (gfx->getH()>>1)-30, menuFont, text);
}

void GlobalContainer::load(void)
{
	// set default values in settings or load them
#	ifdef WIN32 // angel > case Win32 caca
		strncpy(settings.userName, getenv("USERNAME"), BasePlayer::MAX_NAME_LENGTH);
#	else // angel > case of unix and MacIntosh Systems
		strncpy(settings.userName, getenv("USER"), BasePlayer::MAX_NAME_LENGTH);
#	endif
	settings.userName[BasePlayer::MAX_NAME_LENGTH-1]=0;
	// TODO : loading code

	// create graphic context
	gfx=GraphicContext::createGraphicContext(DrawableSurface::GC_SDL);
	gfx->setRes(640, 480, 32, globalContainer->graphicFlags);

	// load fonts
	menuFont=gfx->loadFont("data/fonts/arial24white.png");
	standardFont=gfx->loadFont("data/fonts/arial14white.png");
	littleFontGreen=gfx->loadFont("data/fonts/arial8green.png");

	// load texts
	texts.load("data/texts.txt");
	initProgressBar();

	updateLoadProgressBar(10);
	// load terrain data
	terrain=gfx->loadSprite("data/gfx/terrain");

	// load shader for unvisible terrain
	terrainShader=gfx->loadSprite("data/gfx/shade");

	updateLoadProgressBar(40);
	// load ressources
	ressources=gfx->loadSprite("data/gfx/ressource");

	updateLoadProgressBar(50);
	// load units
	units=gfx->loadSprite("data/gfx/unit");

	updateLoadProgressBar(80);
	// load buildings
	buildings=gfx->loadSprite("data/gfx/building");

	updateLoadProgressBar(100);
};
