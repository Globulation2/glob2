/*
 * Globulation 2 Global Artwork Container
 * (contains all static stuff that should be loaded on startup)
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GlobalContainer.h"
#include "SDL.h"

GlobalContainer::GlobalContainer(void)
{
	memset(safer0, 7, 1024);
	memset(safer1, 7, 1024);
	memset(safer2, 7, 1024);
	memset(safer3, 7, 1024);
	memset(safer4, 7, 1024);
	memset(safer5, 7, 1024);
	memset(safer6, 7, 1024);
	memset(safer7, 7, 1024);
	memset(safer8, 7, 1024);
	memset(safer9, 7, 1024);
	graphicFlags=SDL_ANYFORMAT|SDL_SWSURFACE;

};

void GlobalContainer::parseArgs(int argc, char *argv[])
{
	{
		for (int  i=1; i<argc; i++)
		{
			if (strcmp(argv[i], "-f")==0)
				graphicFlags|=SDL_FULLSCREEN;
		}
	}
}

void GlobalContainer::load(void)
{
	// load palette
	SDL_RWops *stream=fileManager.open("data/MacPal","rb");
	macPal.load(stream, gfx.screen->format);
	SDL_RWclose(stream);
	ShadedPal=macPal;
	ShadedPal.toBlackAndWhite();

	// load terrain data
	terrain.setDefaultPal(&macPal);
	terrain.load("data/terrain.data");

	// load ressources
	ressources.setDefaultPal(&macPal);
	ressources.load("data/ressources.data");

	// load units
	units.setDefaultPal(&macPal);
	units.load("data/units.data");
	units.enableColorKey(0);

	// load buildings
	buildings.setDefaultPal(&macPal);
	buildings.load("data/buildings.data");
	buildings.enableColorKey(0);

	// load texts
	texts.load("data/texts.txt");

	// load fonts
	menuFont.load("data/menuFont.png");
	printf ("GlobalContainer::safe()=%d\n", safe());
};

bool GlobalContainer::safe(void)
{
	memset(safer0, 7, 1024);
	
	if (memcmp(safer1, safer0, 1024)!=0)
		return false;
	if (memcmp(safer2, safer0, 1024)!=0)
		return false;
	if (memcmp(safer3, safer0, 1024)!=0)
		return false;
	if (memcmp(safer4, safer0, 1024)!=0)
		return false;
	if (memcmp(safer5, safer0, 1024)!=0)
		return false;
	if (memcmp(safer6, safer0, 1024)!=0)
		return false;
	if (memcmp(safer7, safer0, 1024)!=0)
		return false;
	if (memcmp(safer8, safer0, 1024)!=0)
		return false;
	if (memcmp(safer9, safer0, 1024)!=0)
		return false;
	
	
	return true;
};
