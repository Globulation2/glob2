/*
 * Globulation 2 Global Artwork Container
 * (contains all static stuff that should be loaded on startup)
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GlobalContainer.h"
#include "SDL.h"

GlobalContainer::GlobalContainer(void)
{
	graphicFlags=DrawableSurface::DEFAULT;
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
	if (gfx)
		delete gfx;
}

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	for (int  i=1; i<argc; i++)
	{
		if (strcmp(argv[i], "-f")==0) {
			graphicFlags|=DrawableSurface::FULLSCREEN;
			continue;
		}

		if (strcmp(argv[i], "-a")==0) {
			graphicFlags|=DrawableSurface::HWACCELERATED;
			continue;
		}

		// the -d option appends a directory in the
		// directory search list.
		if ((argv[i][0] == '-') && (argv[i][1] == 'd')) {
			if (argv[i][2] != 0)
				fileManager.addDir(&argv[i][2]);
			else {
				i++;
				if (i < argc) 
					fileManager.addDir(argv[i]);
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

	// load texts
	texts.load("data/texts.txt");

};
