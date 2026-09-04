// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Screen.h"
#include "GlobalContainer.h"
#include "DynamicClouds.h"


Glob2Screen::Glob2Screen()
{
}

Glob2Screen::~Glob2Screen()
{

}

void Glob2Screen::paint(void)
{
	static int time = 0;
	time++;
	randomSeed = 1;

	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
	dynamic_cast<GraphicContext*>(gfx)->finishDrawingSprite(globalContainer->terrain, 255);

	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		static DynamicClouds ds(&globalContainer->settings);
		//ds.render(globalContainer->gfx, 0, 0, getW(), getH(), time);
		ds.compute(0, 0, getW(), getH(), time);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::SHADOW);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::CLOUD);
	}
}

unsigned Glob2Screen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}




Glob2TabScreen::Glob2TabScreen(bool fullScreen, bool longerButtons)
	: TabScreen(fullScreen, longerButtons)
{
}

Glob2TabScreen::~Glob2TabScreen()
{

}

void Glob2TabScreen::paint(void)
{
	static int time = 0;
	time++;
	randomSeed = 1;

	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
	dynamic_cast<GraphicContext*>(gfx)->finishDrawingSprite(globalContainer->terrain, 255);

	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		static DynamicClouds ds(&globalContainer->settings);
		//ds.render(globalContainer->gfx, 0, 0, getW(), getH(), time);
		ds.compute(0, 0, getW(), getH(), time);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::SHADOW);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::CLOUD);
	}
}

unsigned Glob2TabScreen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}
