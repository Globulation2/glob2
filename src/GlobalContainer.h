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
#include "Player.h"

struct Settings
{
	char userName[BasePlayer::MAX_NAME_LENGTH];
};

class GlobalContainer
{
public:
	GlobalContainer(void);
	virtual ~GlobalContainer(void);

	void parseArgs(int argc, char *argv[]);
	void load(void);

private:
	void setMetaServerName(char *name);
	void initProgressBar(void);
	void updateLoadProgressBar(int value);

public:
	Uint32 graphicFlags;
	char *metaServerName;
	Uint16 metaServerPort;

	Settings settings;

	FileManager fileManager;

	GraphicContext *gfx;
	Sprite *terrain;
	Sprite *terrainShader;
	Sprite *ressources;
	Sprite *units;
	Sprite *buildings;
	Font *menuFont;
	Font *standardFont;

	StringTable texts;

	BuildingsTypes buildingsTypes;
};

extern GlobalContainer *globalContainer;

#endif 

