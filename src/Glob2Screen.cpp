#include "Glob2Screen.h"
#include "GlobalContainer.h"


Glob2Screen::Glob2Screen()
{
}

Glob2Screen::~Glob2Screen()
{
	
}

void Glob2Screen::paint(void)
{
	static int cloudDisplacement = -1023;
	randomSeed = 1;
	
	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
			
	// draw cloud shadow if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		for (int y = 0 + (cloudDisplacement % -512); y < getH(); y += 512)
			for (int x = 0 + (cloudDisplacement % -512); x < getW(); x += 512)
				globalContainer->gfx->drawSprite(x, y, globalContainer->terrainCloud, 0);
	}
	
	// compute cloud coordinates, used for displacement of clouds but also water
	if (cloudDisplacement > 0)
		cloudDisplacement -= 1024;
	else
		cloudDisplacement++;
}

unsigned Glob2Screen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}
