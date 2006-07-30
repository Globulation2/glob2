/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  Copyright (C) 2006 Bradley Arsenault

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

#include "Game.h"
#include "GUIBase.h"
#include "Brush.h"
#include "GameGUILoadSave.h"
#include "ScriptEditorScreen.h"

#include <string>
#include <vector>
#include <map>

#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>


namespace GAGCore
{
	class Sprite;
	class Font;
}
using namespace GAGCore;
namespace GAGGUI
{
	class OverlayScreen;
}
using namespace GAGGUI;
class Unit;

struct Rectangle
{
	Rectangle(int x, int y, int width, int height) : x(x), y(y), width(width), height(height) {}
	Rectangle() : x(0), y(0), width(0), height(0) {}
	bool is_in(int posx, int posy) { return posx>x && posx<(x+width) && posy>y && posy<(y+height); }

	int x;
	int y;
	int width;
	int height;
};



class MapEditMenuScreen : public OverlayScreen
{
public:
	MapEditMenuScreen();
	virtual ~MapEditMenuScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);

	enum
	{
		LOAD_MAP,
		SAVE_MAP,
		OPEN_SCRIPT_EDITOR,
		RETURN_EDITOR,
		QUIT_EDITOR
	};
};



class MapEdit
{
public:
	MapEdit();
	~MapEdit();
	bool load(const char *filename);
	bool save(const char *filename, const char *name);

	//void resize(int sizeX, int sizeY);
	int run(int sizeX, int sizeY, TerrainType terrainType);
	int run(void);
	
	void mapHasBeenModiffied(void) { hasMapBeenModified=true; } // moved public so that newly cerated map are modified

	Game game;
private:
	bool do_quit;

	void drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode);
	void drawMiniMap(void);
	void renderMiniMap(void);

	enum PanelMode
	{
		AddBuildings,
		AddFlagsAndZones,
		Terrain,
		Teams,
	};

	void drawMenu(void);
	void drawPanelButtons(int pos);
	void drawChoice(int pos, std::vector<std::string> &types, unsigned numberPerLine = 2);
	void drawTextCenter(int x, int y, const char *caption, int i=-1);
	void drawScrollBox(int x, int y, int value, int valueLocal, int act, int max);
	void drawFlagView();
	void drawMenuEyeCandy();
	void drawTerrainView();
	void drawTeamView();
	void drawMultipleSelection(int x, int y, std::vector<std::string>& strings, unsigned int pos);
	void drawTeamSelector(int x, int y);
	void drawUnitOnMap();

	void register_buttons();
	int processEvent(SDL_Event& event);
	void handleKeyPressed(SDLKey key, bool pressed);
	void perform_action(const std::string& action);
	void delegateMenu(SDL_Event& event);

	int viewportX;
	int viewportY;
	int xspeed;
	int yspeed;
	int mouseX;
	int mouseY;

	bool hasMapBeenModified;
	int team;

	PanelMode panelmode;
	std::vector<std::string> buildingsChoiceName;
	std::vector<std::string> flagsChoiceName;
	std::string selectionName;
	void drawBuildingSelectionOnMap();
	int building_level;
	bool is_upgradable(int building_level);

	void disableBuildingsView();
	void enableBuildingsView();
	void disableFlagView();
	void enableFlagView();
	void disableTerrainView();
	void enableTerrainView();
	void disableTeamView();
	void enableTeamView();

	Sprite *menu;
	Font *font;

	enum SelectionMode
	{
		PlaceNothing,
		PlaceBuilding,
		PlaceZone,
		PlaceTerrain,
		PlaceUnit,
	} selectionMode;

	std::map<std::string, boost::tuple<Rectangle, std::string, bool, bool> > button_areas;
	void activate_area(const std::string& name);
	void deactivate_area(const std::string& name);
	bool is_activated(const std::string& name);
	void add_area(const std::string& name, const Rectangle& area, const std::string& action, bool is_activated, bool on_release=false);
	std::string get_action(int x, int y, bool is_release);

	void minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport);
	bool is_dragging_minimap;

	int last_placement_x;
	int last_placement_y;

	bool showingMenuScreen;
	MapEditMenuScreen* menuscreen;

	bool showing_load;
	bool showing_save;
	LoadSaveScreen* loadsavescreen;

	bool showingScriptEditor;
	ScriptEditorScreen* script_editor;


	// Brushes
	enum BrushType
	{
		ForbiddenBrush,
		GuardAreaBrush,
		ClearAreaBrush,
		NoBrush,
		
	} brushType;
	BrushTool brush;
	BrushAccumulator brushAccumulator;
	void handleBrushClick(int mx, int my);
	bool is_dragging_zone;

	enum SelectedTerrain
	{
		Grass,
		Sand,
		Water,
		Wheat,
		Trees,
		Stone,
		Algae,
		CherryTree,
		OrangeTree,
		PruneTree,
		NoTerrain,
	} terrainType;
	void handleTerrainClick(int mx, int my);
	bool is_dragging_terrain;

	std::vector<std::string> team_view_selector_keys;
	std::vector<unsigned int> selector_positions;

	enum SelectedUnit
	{
		Worker,
		Explorer,
		Warrior,
		NoUnit,
	} selectedUnit;
	int unit_level;
};




#endif 
