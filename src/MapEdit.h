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

#ifndef __GLOB2EDIT_H
#define __GLOB2EDIT_H

#include "GAG.h"
#include "Map.h"

class MapEdit
{
public:
	MapEdit();
	~MapEdit();
	int run(void);

private:
	void drawMap(int sx, int sy, int sw, int sh, bool needUpdate=true);
	void drawMiniMap(void);
	void renderMiniMap(void);
	void drawMenu(void);
	void draw(void);

	void handleMenuClick(int mx, int my, int button);
	void handleMapClick(int mx, int my);
	void handleKeyPressed(SDLKey key, bool pressed);

	void load(const char *name="default.map");
	void save(const char *name="default.map");
	void loadSave(bool isLoad);

	void updateUnits(int x, int y, int w, int h);

	enum EditMode
	{
		NONE,
		TERRAIN,
		RESSOURCE,
		BUILDING,
		UNIT,
		DELETE
	};

private:
	void regenerateClipRect(void);
	void drawSelRect(int x, int y, int w, int h);
	void viewportFromMxMY(int mx, int my);

private:
	Game game;
	bool isRunning;

	int viewportX, viewportY;
	int viewportW, viewportH;
	int viewportSpeedX[9], viewportSpeedY[9];
	SDL_Rect screenClip, mapClip;
	GraphicContext *gfx;

	int team;
	int terrainSize;
	int level;
	int type;
	EditMode editMode;

	Sprite *menu;

	Font *font;
};

#endif 
