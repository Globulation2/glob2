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

