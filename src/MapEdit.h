/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "Brush.h"
#include "GameGUILoadSave.h"
#include "Game.h"
#include "GUIBase.h"
#include "KeyboardManager.h"
#include <map>
#include "ScriptEditorScreen.h"
#include <string>
#include <vector>
#include "Minimap.h"

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


///A generic rectangle structure used for a variety of purposes, but mainly for the convience of the widget system
struct widgetRectangle
{
	widgetRectangle(int x, int y, int width, int height) : x(x), y(y), width(width), height(height) {}
	widgetRectangle() : x(0), y(0), width(0), height(0) {}
	bool is_in(int posx, int posy) { return posx>x && posx<(x+width) && posy>y && posy<(y+height); }

	int x;
	int y;
	int width;
	int height;
};


///This is the map editor menu screen. It has 5 buttons. Its very similair to the in-game main menu
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


///This is a text info box. It is used primarily for entering the names of the script areas,
///however it is generic enough to be recycled for other purposes.
class AskForTextInput : public OverlayScreen
{
public:
	enum
	{
		OK,
		CANCEL
	};
	AskForTextInput(const std::string& label, const std::string& current);
	void onAction(Widget *source, Action action, int par1, int par2);
	std::string getText();
private:
	TextInput* textEntry;
	TextButton* ok;
	TextButton* cancel;
	Text* label;
	std::string labelText;
	std::string currentText;
};



class MapEdit;

///This is a map editor widget, which is a widget that works within the map editor. Now to answer the crucial question, why not
///use libgag? Indeed, I had pondered on the use of libgag for quite some time, considering all of the odds and ends that would
///be required (I lied in bed for almost 3 hours thinking about it). I eventually decided, after much consideration, that libgag
///was not suited for amount of coupling between the widgets required in the map editor. The resulting classes from using libgag
///would be very tightly coupled. It would not improve maintainability, which is the ultimate goal, and the amount of work-arounds
///would not save much time from the code re-use. So, instead I made my own semi-widget system. These widgets do just enough to
///make the map editor easily maintained. They are, like libgag, very tightly coupled to the map editor, and can not be re-used.
///However, they do solve a few other problems. They automatically detect clicks of the mouse and send the appropriette action
///to be carried out to the map editor, which saves from tedious maintaining of x,y cordinates.
class MapEditorWidget
{
public:
	///This is the standard constructor. All of the members sent to it can be accessed by derived classes.
	MapEditorWidget(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action);
	virtual ~MapEditorWidget() { }
	void drawSelf();
	///This disables the widget, it will not draw itself or respond to mouse clicks
	void disable();
	///This enables the widget.
	void enable();
	///This tests whether the x,y cordinates are within this particular widgets area.
	bool is_in(int x, int y) { return area.is_in(x, y); }
	///This function handles a click with mouse positions relitive to the widget. It can be overrided, but derived classes
	///should be carefull to call the base class version after there customized code
	virtual void handleClick(int relMouseX, int relMouseY);
	///This function must be implemented by all derived classes. This is where the widget draws itself. It should use area.x
	///and area.y to get the cordinates.
	virtual void draw()=0;
	friend class MapEdit;
protected:
	MapEdit& me;
	widgetRectangle area;
	std::string group;
	std::string name;
	std::string action;
	bool enabled;
};


///This widget allows the selection of a building, of any type. When MapEdit::selectionName matches this widgets building_type, it will draw a selction
///arround its building as well. The large selector (56x46) is default, but if largeSelector is false, it will use the smaller selector, which is
///(32x32).
class BuildingSelectorWidget : public MapEditorWidget
{
public:
	BuildingSelectorWidget(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& building_type, bool largeSelector);
	void draw();
private:
	std::string building_type;
	bool largeSelector;
};


///This draws the selector of the selected team color. It darkens the currently selected team. (this is used when picking what team to place a building, flag, unit or zone for).
class TeamColorSelector : public MapEditorWidget
{
public:
	TeamColorSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action);
	void draw();
};


///This is a level selector. It draws a small picture denoting the level, and darkens if this selectors level is the currently selected one. Its at the bottom
///of the building and flag views. 
class SingleLevelSelector : public MapEditorWidget
{
public:
	SingleLevelSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int level, int& levelNum);
	void draw();
private:
	int level;
	int& levelNum;
};


///This is an under-the-minimap icon.
class PanelIcon : public MapEditorWidget
{
public:
	PanelIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int iconNumber, int panelModeHilight);
	void draw();
private:
	int iconNumber;
	int panelModeHilight;
};


///This is the small icon to the left of the minimap tat you use to open the in-game menu.
class MenuIcon : public MapEditorWidget
{
public:
	MenuIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action);
	void draw();
};


///This is a selector for a type of zone, like forbidden, guarding or clearing zone. Three of these are needed to be able to select any type of zone.
class ZoneSelector : public MapEditorWidget
{
public:
	enum ZoneType
	{
		ForbiddenZone,
		GuardingZone,
		ClearingZone,
	};
	
	ZoneSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, ZoneType zoneType);
	void draw();
private:
	ZoneType zoneType;
};


///This is a brush selector. It draws that tool with the various shapes you can place terrain and zones in
///Its in the middle of the flag panel and the bottom of the terrain panel
class BrushSelector : public MapEditorWidget
{
public:
	BrushSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, BrushTool& brushTool);
	void draw();
private:
	BrushTool& brushTool;
};


///This allows the selection of a particular type of unit. It draws a selector arround the unit when that unit type is the one selected.
class UnitSelector : public MapEditorWidget
{
public:
	UnitSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int unitType);
	void draw();
private:
	int unitType;
};



///This is a terrain selector. It allows for the selection of a variety of terrains and ressources. It draws a selector arround the particular terrain type
///when that terrain is selected.
class TerrainSelector : public MapEditorWidget
{
public:
	enum TerrainType
	{
		Grass,
		Sand,
		Water,
		Wheat,
		Trees,
		Stone,
		Algae,
		Papyrus,
		CherryTree,
		OrangeTree,
		PruneTree,
		NoTerrain,
	};
	TerrainSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, TerrainType terrainType);
	void draw();
private:
	TerrainType terrainType;
};


///This is a generic blue button. It can be selected and unselected, but not automatically. Its takes the un-translated from of the text, still inside [brackets]
class BlueButton : public MapEditorWidget
{
public:
	BlueButton(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& text);
	void draw();
	void setSelected();
	void setUnselected();
private:
	std::string text;
	bool selected;
};



///This is the plus icon on the teams view that is used to add another team to the map
class PlusIcon : public MapEditorWidget
{
public:
	PlusIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action);
	void draw();
};



///This is the minus icon on the teams view that is used to remove a team from the map
class MinusIcon : public MapEditorWidget
{
public:
	MinusIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action);
	void draw();
};


///This is a single team info, that only draws itself and handles clicks when there is a team with its number in the game. It shows
///the color of the team, and allows for selecting between various strings of options for the team (right now only one string is
///present, "human", more may be added later)
class TeamInfo : public MapEditorWidget
{
public:
	TeamInfo(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int teamNum, std::vector<std::string>& options);
	void draw();
	void handleClick(int relMouseX, int relMouseY);
	void setSelectionPos(int pos) { selectorPos=pos; }
	int getSelectionPos() { return selectorPos; }
private:
	int teamNum;
	int selectorPos;
	std::vector<std::string>& options;
};



///This is the title shown when you select a unit for editing. It is just text.
class UnitInfoTitle : public MapEditorWidget
{
public:
	UnitInfoTitle(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Unit* unit);
	void draw();
	void setUnit(Unit* unit);
private:
	Unit* unit;
};



///This is the picture shown when you select a unit for editing.
class UnitPicture : public MapEditorWidget
{
public:
	UnitPicture(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Unit* unit);
	void draw();
	void setUnit(Unit* unit);
private:
	Unit* unit;
};



///This is a small text object. It shows two values and a label, like "label 1/2". The denominator can be fixed or variable. Either way, the numerator is done 
///by pointer because this class is used for the convinient edditing of values in a Unit or Building
class FractionValueText : public MapEditorWidget
{
public:
	FractionValueText(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& label, Sint32* numerator, Sint32* denominator);
	FractionValueText(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& label, Sint32* numerator, Sint32 denominator);
	~FractionValueText();
	void draw();
	void setValues(Sint32* numerator, Sint32* denominator);
	void setValues(Sint32* numerator);
private:
	std::string label;
	Sint32* numerator;
	Sint32* denominator;
	bool isDenominatorPreset;
};



///This is a scroll box, similair to FractionValueText, that allows for the changing of its two values. 
class ValueScrollBox : public MapEditorWidget
{
public:
	ValueScrollBox(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Sint32* value, Sint32* max);
	ValueScrollBox(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Sint32* value, Sint32 max);
	~ValueScrollBox();
	void draw();
	void handleClick(int relMouseX, int relMouseY);
	void setValues(Sint32* value, Sint32* max);
	void setValues(Sint32* value);
private:
	Sint32* value;
	Sint32* max;
	bool isMaxPreset;
};



///This is the title shown at the top of the menu when you select a building
class BuildingInfoTitle : public MapEditorWidget
{
public:
	BuildingInfoTitle(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Building* building);
	void draw();
	void setBuilding(Building* building);
private:
	Building* building;
};



///This is the picture shown at the top of the menu when you select a building
class BuildingPicture : public MapEditorWidget
{
public:
	BuildingPicture(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Building* building);
	void draw();
	void setBuilding(Building* building);
private:
	Building* building;
};



///This is a generic textual label that takes its label pre-translated. It simply draws its text, and it can be centered or un-centered in its space.
class TextLabel : public MapEditorWidget
{
public:
	TextLabel(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& label, bool centered, const std::string& emptyLabel);
	void draw();
	void setLabel(const std::string& label);
private:
	std::string label;
	std::string emptyLabel;
	bool centered;
};



///This widget cycles between multiple numbers. It draws the number, and increments it when clicked. If it goes over its max, it goes back to 1.
class NumberCycler : public MapEditorWidget
{
public:
	NumberCycler(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int maxNumber);
	void draw();
	int getIndex();
	void handleClick(int relMouseX, int relMouseY);
private:
	int maxNumber;
	int currentNumber;
};



///This is the map editor class in all its glory.
class MapEdit
{
public:
	MapEdit();
	~MapEdit();
	///Loads the game given by a particular file name
	bool load(const char *filename);
	///Saves the game to a particular file name
	bool save(const char *filename, const char *name);

	///This function sets the map a particular size and uniform terrain type, then goes into the main loop
	int run(int sizeX, int sizeY, TerrainType terrainType);
	///This is the main loop function. It "ticks" every 33 miliseconds, handling events and drawing as it goes.
	int run(void);
	
	void mapHasBeenModiffied(void) { hasMapBeenModified=true; }

	Game game;
	friend class MapEditorWidget;
	friend class BuildingSelectorWidget;
	friend class TeamColorSelector;
	friend class SingleLevelSelector;
	friend class PanelIcon;
	friend class MenuIcon;
	friend class ZoneSelector;
	friend class BrushSelector;
	friend class UnitSelector;
	friend class TerrainSelector;
	friend class BlueButton;
	friend class PlusIcon;
	friend class MinusIcon;
	friend class TeamInfo;
	friend class BuildingInfoTitle;
private:
	///If this is set, the map editor will exit as soon as it finishes drawing and proccessing events
	bool doQuit;

	///This draws the map and various on-map elements
	void drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode);

	///This draws the minimap
	void drawMiniMap(void);

	///This is the mode of the right-hand-side panel
	enum PanelMode
	{
		AddBuildings,
		AddFlagsAndZones,
		Terrain,
		Teams,
		UnitEditor,
		BuildingEditor,
	};

	///This draws the right side menu in its entirety
	void drawMenu(void);
	///Draws some of the fancy bars, and the bar at the top of the screen, the eye-candy so to speek
	void drawMenuEyeCandy();
	///Draws a unit under the cursor. This should be done and only done when the user is placing a unit.
	void drawPlacingUnitOnMap();
	///Draws a building under the cursor. Like drawPlacingUnitOnMap, it should only be done when placing a building
	void drawBuildingSelectionOnMap();

	///This proccesses an event from the SDL
	int processEvent(SDL_Event& event);
	///Handles a key pressed. For most keys, this means going to the keyboard shortcuts. For the arrow keys, it starts or stops scrolling the map
	void handleKeyPressed(SDL_keysym key, bool pressed);
	///This performs an action in the form of the string. This is where allot of code goes. As opposed to using seperate functions for such a large
	///number of possible actions, or just inlining them, this system locates them all here, and every small bit has a name as well. It makes debugging
	///easy in some ways, and it also greatly improves readability. All of the widget "actions" come to here.
	void performAction(const std::string& action, int relMouseX=0, int relMouseY=0);
	///This delegates a sdl event to one of the menus, if they are open, and handle end codes of the menus appropriettly
	void delegateMenu(SDL_Event& event);

	///This states whether the minimap was rendered or not
	bool wasMinimapRendered;

	///This is the x position of the map that is being painted on the screen
	int viewportX;
	///This is the y position of the map that is being painted on the screen
	int viewportY;
	///This is the x-scrolling speed
	int xSpeed;
	///This is the y-scrolling speed
	int ySpeed;
	///This is the mouse x position, updated whenever the mouse moves
	int mouseX;
	///This is the mouse y position, updated whenever the mouse moves
	int mouseY;
	///This is the mouse x position relitive to its last position
	int relMouseX;
	///This is the mouse y position relitive to its last position
	int relMouseY;
	///This boolean states whether we are dragging the screen with the middle mouse button
	bool isScrollDragging;

	///the keyboardManager handles keyboard shortcuts
	KeyboardManager keyboardManager;

	bool hasMapBeenModified;
	///Provides the currently active team number
	int team;

	PanelMode panelMode;

	//Miscalaneous panel icons
	PanelIcon* buildingView;
	PanelIcon* flagsView;
	PanelIcon* terrainView;
	PanelIcon* teamsView;
	MenuIcon* menuIcon;

	///Building view
	///@{
	BuildingSelectorWidget* swarm;
	BuildingSelectorWidget* inn;
	BuildingSelectorWidget* hospital;
	BuildingSelectorWidget* racetrack;
	BuildingSelectorWidget* swimmingpool;
	BuildingSelectorWidget* barracks;
	BuildingSelectorWidget* school;
	BuildingSelectorWidget* defencetower;
	BuildingSelectorWidget* stonewall;
	BuildingSelectorWidget* market;
	TeamColorSelector* building_view_tcs;
	SingleLevelSelector* building_view_level1;
	SingleLevelSelector* building_view_level2;
	SingleLevelSelector* building_view_level3;
	///@}

	///Flag, zone, and unit view
	///@{
	BuildingSelectorWidget* warflag;
	BuildingSelectorWidget* explorationflag;
	BuildingSelectorWidget* clearingflag;
	ZoneSelector* forbiddenZone;
	ZoneSelector* guardZone;
	ZoneSelector* clearingZone;
	BrushSelector* zoneBrushSelector;
	UnitSelector* worker;
	UnitSelector* explorer;
	UnitSelector* warrior;
	TeamColorSelector* flag_view_tcs;
	SingleLevelSelector* flag_view_level1;
	SingleLevelSelector* flag_view_level2;
	SingleLevelSelector* flag_view_level3;
	SingleLevelSelector* flag_view_level4;
	///@}

	///Terrain, ressources and areas view
	///@{
	TerrainSelector* grass;
	TerrainSelector* sand;
	TerrainSelector* water;
	TerrainSelector* wheat;
	TerrainSelector* trees;
	TerrainSelector* stone;
	TerrainSelector* algae;
	TerrainSelector* papyrus;
	TerrainSelector* orange;
	TerrainSelector* cherry;
	TerrainSelector* prune;
	BlueButton* deleteButton;
	BlueButton* noRessourceGrowthButton;
	BlueButton* areasButton;
	NumberCycler* areaNumber;
	TextLabel* areaNameLabel;
	BrushSelector* terrainBrushSelector;
	///@}

	///Teams view
	///@{
	PlusIcon* increaseTeams;
	MinusIcon* decreaseTeams;
	TeamInfo* teamInfo1;
	TeamInfo* teamInfo2;
	TeamInfo* teamInfo3;
	TeamInfo* teamInfo4;
	TeamInfo* teamInfo5;
	TeamInfo* teamInfo6;
	TeamInfo* teamInfo7;
	TeamInfo* teamInfo8;
	TeamInfo* teamInfo9;
	TeamInfo* teamInfo10;
	TeamInfo* teamInfo11;
	TeamInfo* teamInfo12;
	///@}

	///Unit editor view
	///@{
	UnitInfoTitle* unitInfoTitle;
	UnitPicture* unitPicture;
	FractionValueText* unitHPLabel;
	ValueScrollBox* unitHPScrollBox;
	FractionValueText* unitWalkLevelLabel;
	ValueScrollBox* unitWalkLevelScrollBox;
	FractionValueText* unitSwimLevelLabel;
	ValueScrollBox* unitSwimLevelScrollBox;
	FractionValueText* unitHarvestLevelLabel;
	ValueScrollBox* unitHarvestLevelScrollBox;
	FractionValueText* unitBuildLevelLabel;
	ValueScrollBox* unitBuildLevelScrollBox;
	FractionValueText* unitAttackSpeedLevelLabel;
	ValueScrollBox* unitAttackSpeedLevelScrollBox;
	FractionValueText* unitAttackStrengthLevelLabel;
	ValueScrollBox* unitAttackStrengthLevelScrollBox;
	FractionValueText* unitMagicGroundAttackLevelLabel;
	ValueScrollBox* unitMagicGroundAttackLevelScrollBox;
	///@}

	///Building editing view
	///@{
	BuildingInfoTitle* buildingInfoTitle;
	BuildingPicture* buildingPicture;
	FractionValueText* buildingHPLabel;
	ValueScrollBox* buildingHPScrollBox;
	FractionValueText* buildingFoodQuantityLabel;
	ValueScrollBox* buildingFoodQuantityScrollBox;
	FractionValueText* buildingAssignedLabel;
	ValueScrollBox* buildingAssignedScrollBox;
	FractionValueText* buildingWorkerRatioLabel;
	ValueScrollBox* buildingWorkerRatioScrollBox;
	FractionValueText* buildingExplorerRatioLabel;
	ValueScrollBox* buildingExplorerRatioScrollBox;
	FractionValueText* buildingWarriorRatioLabel;
	ValueScrollBox* buildingWarriorRatioScrollBox;
	FractionValueText* buildingCherryLabel;
	ValueScrollBox* buildingCherryScrollBox;
	FractionValueText* buildingOrangeLabel;
	ValueScrollBox* buildingOrangeScrollBox;
	FractionValueText* buildingPruneLabel;
	ValueScrollBox* buildingPruneScrollBox;
	FractionValueText* buildingStoneLabel;
	ValueScrollBox* buildingStoneScrollBox;
	FractionValueText* buildingBulletsLabel;
	ValueScrollBox* buildingBulletsScrollBox;
	FractionValueText* buildingMinimumLevelLabel;
	ValueScrollBox* buildingMinimumLevelScrollBox;
	FractionValueText* buildingRadiusLabel;
	ValueScrollBox* buildingRadiusScrollBox;
	///@}

	///This is the name of the currently selected building type
	std::string selectionName;
	///This is the level of the buildings in the menu. It is
    ///changed by the buttons at the bottom of the building menu
	int buildingLevel;
	///Returns whether the particular type of building is upgradable
	bool isUpgradable(int buildingType);

	///A pointer to the editor sprites
	Sprite *menu;

	///Denotes the mode the cursor is in, such as placing a building, terrain, removing objects, editing a building, etc..
	enum SelectionMode
	{
		PlaceNothing,
		PlaceBuilding,
		PlaceZone,
		PlaceTerrain,
		PlaceUnit,
		RemoveObject,
		EditingUnit,
		EditingBuilding,
		ChangeAreas,
		ChangeNoRessourceGrowthAreas,
	} selectionMode;

	///Adds a widget to be drawn and respond to clicks
	void addWidget(MapEditorWidget* widget);
	///Finds and runs the action accossiatted with a particular mouse x and mouse y position. Returns false if none is found, true otherwise
	bool findAction(int x, int y);
	///Enables only widgets with this particular groupname, or the name "any" as a special name
	void enableOnlyGroup(const std::string& group);
	///Draws all enabled widgets
	void drawWidgets();
	std::vector<MapEditorWidget*> mew;

	///Translates a mouse x and mouse y position to cordinates on the Map
	void minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport);
	///Denotes whether the user is holding the mouse button down, dragging the minimap
	bool isDraggingMinimap;
	///The minimap
	Minimap minimap;

	///This is the last placement of terrain, zones, or else, so that the game doesn't use allot of cpu by small mouse movements
	int lastPlacementX;
	///This is the last placement of terrain, zones, or else, so that the game doesn't use allot of cpu by small mouse movements
	int lastPlacementY;

	///Tells whether the menu screen is being drawn right now
	bool showingMenuScreen;
	MapEditMenuScreen* menuScreen;

	///Tells whether the load-game menu screen is being drawn right now
	bool showingLoad;
	///Tells whether the save-game menu screen is being drawn right now
	bool showingSave;
	LoadSaveScreen* loadSaveScreen;

	///Tells whether the script editor is being drawn
	bool showingScriptEditor;
	ScriptEditorScreen* scriptEditor;


	///The various types of brushes for placing a zone
	enum BrushType
	{
		ForbiddenBrush,
		GuardAreaBrush,
		ClearAreaBrush,
		NoBrush,
		
	} brushType;
	///This is a brush tool that the BrushSelector widget uses
	BrushTool brush;
	///This is an accumulator, it builds up a list of positions to be changes
	BrushAccumulator brushAccumulator;
	///Handles brush click to place a zone
	void handleBrushClick(int mx, int my);
	///Tells whether the user is dragging the brush, continually placing zone
	bool isDraggingZone;


	///The type of terrain that is selected
	TerrainSelector::TerrainType terrainType;
	///Handles a click or drag of the mouse when placing terrain
	void handleTerrainClick(int mx, int my);
	///Tells whether the terrain is being dragged, continually placing more
	bool isDraggingTerrain;
	///Handles a click or drag of the mouse when removing objects
	void handleDeleteClick(int mx, int my);
	///Tells whether the delete tool is being dragged
	bool isDraggingDelete;

	///This vector of the keys on the team view. It allows one to choose between AI and human teams for campaign games
	std::vector<std::string> teamViewSelectorKeys;

	///Tells the type of unit that is being placed
	enum PlacingUnit
	{
		Worker,
		Explorer,
		Warrior,
		NoUnit,
	} placingUnit;
	///Tells the level of skills of the unit that is being placed
	int placingUnitLevel;

	///The gid of the unit that is currently selected
	int selectedUnitGID;
	///The gid of the building that is currently selected
	int selectedBuildingGID;
	
	///Tells whether the text input box
	bool isShowingAreaName;
	AskForTextInput* areaName;
	///Tells whether we are dragging the area placement tool
	bool isDraggingArea;
	///Handles a click or drag of the script area placement tool
	void handleAreaClick(int mx, int my);

	///Tells whether where dragging the no ressource growth area placement tool
	bool isDraggingNoRessourceGrowthArea;
	///Handles a click or drag of the no ressource growth area placement tool
	void handleNoRessourceGrowthClick(int mx, int my);
};




#endif 
