/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __GLOB2EDIT_H
#define __GLOB2EDIT_H

#include "GAG.h"
#include "Map.h"

class MapEdit
{
public:
	MapEdit();
	~MapEdit();
	//void resize(int sizeX, int sizeY);
	int run(int sizeX, int sizeY, Map::TerrainType terrainType);
	int run(void);

private:
	void drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode);
	void drawMiniMap(void);
	void renderMiniMap(void);
	void drawMenu(void);
	void draw(void);

	int processEvent(const SDL_Event *event);
	void askConfirmationToQuit(void);
	void handleMenuClick(int mx, int my, int button);
	void handleMapClick(int mx, int my);
	void handleMapClick(void);
	void paintCoordodinates(int mx, int my);
	void paintCoordodinates(void);
	void paintEditMode(int mx, int my, bool clearOld, bool mayUpdate);
	void paintEditMode(bool clearOld, bool mayUpdate);
	
	void mapHasBeenModiffied(void);
	
	void handleKeyPressed(SDLKey key, bool pressed);
public:
	bool load(const char *name);
private:
	bool save(const char *name);
	// execute the load/save dialog
	void loadSave(bool isLoad);
	// execute the script editor dialog
	void scriptEditor(void);

	void updateUnits(int x, int y, int w, int h);
public:
	enum EditMode
	{
		EM_NONE,
		EM_TERRAIN,
		EM_RESSOURCE,
		EM_BUILDING,
		EM_UNIT,
		EM_DELETE
	};

private:
	void regenerateClipRect(void);
	void drawSelRect(int x, int y, int w, int h);
	void viewportFromMxMY(int mx, int my);

public:
	Game game;
	bool hasMapBeenModiffied;
private:
	bool isRunning;

	int viewportX, viewportY;
	int viewportW, viewportH;
	int viewportSpeedX[9], viewportSpeedY[9];
	int centeredTeam; // The last centered team when the user typed "tab".
	SDL_Rect screenClip, mapClip;
	GraphicContext *gfx;

	int team;
	int terrainSize;
	int level;
	int type;
	EditMode editMode;
	bool wasClickInMap;
	bool minimapPushed;
	EditMode pushedBrush;

	Sprite *menu;

	Font *font;
private:
	int savedMx, savedMy;
	EditMode oldBrush;
	int orX, orY, orW, orH;
};

#endif 
