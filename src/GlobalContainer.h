/*
 * Globulation 2 Global Artwork Container
 * (contains all static stuff that should be loaded on startup)
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GLOBALCONTAINER_H
#define __GLOBALCONTAINER_H

#include "GAG.h"
#include "StringTable.h"
#include "FileManager.h"
#include "BuildingType.h"

class GlobalContainer
{
public:
	GlobalContainer(void);
	virtual ~GlobalContainer(void);

	void parseArgs(int argc, char *argv[]);

public:
	Uint32 graphicFlags;

	void load(void);


	FileManager fileManager;

	GraphicContext *gfx;
	Sprite *terrain;
	Sprite *terrainShader;
	Sprite *ressources;
	Sprite *units;
	Sprite *buildings;
	Font *menuFont;

	StringTable texts;

	BuildingsTypes buildingsTypes;
};

extern GlobalContainer *globalContainer;

#endif 
