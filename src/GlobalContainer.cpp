/*
 * Globulation 2 Global Artwork Container
 * (contains all static stuff that should be loaded on startup)
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GlobalContainer.h"
#include "SDL.h"
#include <string.h>

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

void GlobalContainer::load(void)
{
	// create graphic context
	gfx=GraphicContext::createGraphicContext(DrawableSurface::GC_SDL);
	gfx->setRes(640, 480, 32, globalContainer->graphicFlags);

	// load terrain data
	terrain=gfx->loadSprite("data/gfx/terrain");

	// load shader for unvisible terrain
	terrainShader=gfx->loadSprite("data/gfx/shade");

	// load ressources
	ressources=gfx->loadSprite("data/gfx/ressource");

	// load units
	units=gfx->loadSprite("data/gfx/unit");

	// load buildings
	buildings=gfx->loadSprite("data/gfx/building");

	// load fonts
	menuFont=gfx->loadFont("data/fonts/arial24white.png");
	standardFont=gfx->loadFont("data/fonts/arial14white.png");

	// load texts
	texts.load("data/texts.txt");

};
