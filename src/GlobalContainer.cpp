/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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
#include "Header.h"
#include <string.h>
#include "NonANSICStdWrapper.h"

GlobalContainer::GlobalContainer(void)
{
	graphicFlags=DrawableSurface::DEFAULT;
	graphicWidth=640;
	graphicHeight=480;

	settings.ircURL=NULL;
	setIRCURL("irc.debian.org");
	settings.ircPort=6667;

	// set default values in settings or load them
	settings.userName[0]=0;
	char *userName;
#	ifdef WIN32
		userName=getenv("USERNAME");
#	else // angel > case of unix and MacIntosh Systems
		userName=getenv("USER");
#	endif
	if (!userName)
		userName="player";
	setUserName(userName);

	hostServer=false;
	gfx=NULL;
	terrain=NULL;
	terrainShader=NULL;
	ressources=NULL;
	units=NULL;
	buildings=NULL;
	
	menuFont=NULL;
	standardFont=NULL;
	littleFontGreen=NULL;
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
	if (settings.ircURL)
		delete[] settings.ircURL;
}

void GlobalContainer::setIRCURL(const char *name)
{
	if (settings.ircURL)
		delete[] settings.ircURL;
	int len=strlen(name)+1;
	settings.ircURL=new char[len];
	strncpy(settings.ircURL, name, len);
}

void GlobalContainer::setUserName(const char *name)
{
	strncpy(settings.userName, name, BasePlayer::MAX_NAME_LENGTH);
	settings.userName[BasePlayer::MAX_NAME_LENGTH-1]=0;
}

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
		if ((strcmp(argv[i], "-host")==0) || (strcmp(argv[i], "--host")==0))
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
			graphicFlags|=DrawableSurface::FULLSCREEN;
			continue;
		}

		if (strcmp(argv[i], "-a")==0)
		{
			graphicFlags|=DrawableSurface::HWACCELERATED;
			continue;
		}

		if (strcmp(argv[i], "-r")==0)
		{
			graphicFlags|=DrawableSurface::RESIZABLE;
			continue;
		}

		if (strcmp(argv[i], "-h")==0)
		{
			printf("\nGlobulation 2\n");
			printf("Cmd line arguments :\n");
			printf("-f\tset full screen\n");
			printf("-r\tset resizable window\n");
			printf("-s\tset resolution (for instance : -s640x480)\n");
			printf("-a\tset hardware accelerated gfx\n");
			printf("-d\tadd a directory to the directory search list\n");
			printf("-m\tspecify meta server hostname\n");
			printf("-p\tspecify meta server port\n");
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
					setIRCURL(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						setIRCURL(argv[i]);
				}
			}
			else if (argv[i][1] == 'p')
			{
				if (argv[i][2] != 0)
					settings.ircPort=atoi(&argv[i][2]);
				else
				{
					i++;
					if (i < argc)
						settings.ircPort=atoi(argv[i]);
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
					graphicWidth=ix;
				}
				if (iy!=0)
				{
					iy&=~(0x1F);
					if (iy<480)
						iy=480;
					graphicHeight=iy;
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
	//gfx->drawString((gfx->getW()-menuFont->getStringWidth(text))>>1, (gfx->getH()>>1)-30, menuFont, text);
}

void GlobalContainer::load(void)
{
	// load texts
	texts.load("data/texts.txt");

	if (!hostServer)
	{
		// create graphic context
		gfx=GraphicContext::createGraphicContext(DrawableSurface::GC_SDL);
		gfx->setRes(graphicWidth, graphicHeight, 32, globalContainer->graphicFlags);

		// load fonts
		menuFont=gfx->loadFont("data/fonts/arial24white.png");
		standardFont=gfx->loadFont("data/fonts/arial14white.png");
		littleFontGreen=gfx->loadFont("data/fonts/arial8green.png");

		initProgressBar();

		updateLoadProgressBar(10);
		// load terrain data
		terrain=gfx->loadSprite("data/gfx/terrain");

		// load shader for unvisible terrain
		terrainShader=gfx->loadSprite("data/gfx/shade");

		updateLoadProgressBar(30);
		// load ressources
		ressources=gfx->loadSprite("data/gfx/ressource");

		updateLoadProgressBar(40);
		// load units
		units=gfx->loadSprite("data/gfx/unit");

		updateLoadProgressBar(70);
		// load buildings
		buildings=gfx->loadSprite("data/gfx/building");

		updateLoadProgressBar(90);
		// load buildings types
		globalContainer->buildingsTypes.load("data/buildings.txt");
		updateLoadProgressBar(100);
	}
};
