/*
 * Globulation 2 Editor main
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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
	void handleKeyPressed(SDLKey key);

	void load(void);
	void save(void);

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

private:
	Game game;
	bool isRunning;

	int viewportX, viewportY;
	int viewportW, viewportH;
	SDL_Rect screenClip, mapClip;
	SDLGraphicContext *gfx;

	int team;
	int terrainSize;
	int level;
	int type;
	EditMode editMode;

	IMGGraphicArchive *menu;
	
	SDLBitmapFont *font;
};

#endif 
