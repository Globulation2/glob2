/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __GAME_GUI_H
#define __GAME_GUI_H

#include <queue>
#include <valarray>

#include "Game.h"
#include "Brush.h"
#include "Campaign.h"
#include "MapHeader.h"
#include "KeyboardManager.h"
#include "MarkManager.h"
#include "GameGUIMessageManager.h"
#include "Minimap.h"
#include "OverlayAreas.h"
#include "GameGUIToolManager.h"
#include "GameGUIDefaultAssignManager.h"
#include "GameGUIGhostBuildingManager.h"

namespace GAGCore
{
	class Font;
}
using namespace GAGCore;

namespace GAGGUI
{
	class OverlayScreen;
}
using namespace GAGGUI;

class TeamStats;
class InGameTextInput;
class Order;
class MapMarkOrder;

//! max unit working at a building
#define MAX_UNIT_WORKING 20
//! range of ratio for swarm
#define MAX_RATIO_RANGE 16

//! The Game Graphic User Interface
/*!
	Handle all user input during game, draw & handle menu.
*/
class GameGUI
{
public:
	///Constructs a GameGUI
	GameGUI();

	///Destroys the GameGUI
	~GameGUI();

	///Initializes all variables
	void init();
	///Moves the local viewport
	void adjustInitialViewport();
	void adjustLocalTeam();
	//! Handle mouse, keyboard and window resize inputs, and stats
	void step(void);
	//! Get order from gui, return NullOrder if
	boost::shared_ptr<Order> getOrder(void);
	//! Return position on x
	int getViewportX() { return viewportX; }
	//! Return position on y
	int getViewportY() { return viewportY; }

	void drawAll(int team);
	void executeOrder(boost::shared_ptr<Order> order);

	/// If setGameHeader is true, then the given gameHeader will replace the one loaded with
	/// the map, otherwise it will be ignored
	bool loadFromHeaders(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData=false, bool saveAI=false);
	//!
	bool load(GAGCore::InputStream *stream, bool ignoreGUIData=false);
	void save(GAGCore::OutputStream *stream, const std::string name);

	void processEvent(SDL_Event *event);

	// Engine has to call this every "real" steps. (or game steps)
	void syncStep(void);
	//! return the local team of the player who is running glob2
	Team *getLocalTeam(void) { return localTeam; }

	// Script interface
	void enableBuildingsChoice(const std::string &name);
	void disableBuildingsChoice(const std::string &name);
	bool isBuildingEnabled(const std::string &name);
	void enableFlagsChoice(const std::string &name);
	void disableFlagsChoice(const std::string &name);
	bool isFlagEnabled(const std::string &name);
	void enableGUIElement(int id);
	void disableGUIElement(int id);

	bool isSpaceSet() { return hasSpaceBeenClicked; }
	void setIsSpaceSet(bool value) { hasSpaceBeenClicked=value; }
	bool isSwallowSpaceKey() { return swallowSpaceKey; }
	void setSwallowSpaceKey(bool value) { swallowSpaceKey=value; }

	void showScriptText(const std::string &text);
	void showScriptTextTr(const std::string &text, const std::string &lang);
	void hideScriptText();

	// Stats for engine
	void setCpuLoad(int s);

	/// Sets this game as a campaign game from the provided campaign and the provided mission
	void setCampaignGame(Campaign& campaign, const std::string& missionName);

	/// Show the dialog that says that the replay ended
	void showEndOfReplayScreen();

	///This is an enum for the current hilight object. The hilighted object is shown with a large arrow.
	///This is primarily for tutorials
	enum HilightObject
	{
		///This causes the main menu icon to be hilighted
		HilightMainMenuIcon=1,
		///This causes all workers on the map to be hilighted
		HilightWorkers=2,
		///This causes all explorers on the map to be hilighted
		HilightExplorers=3,
		///This causes all warriors on the map to be hilighted
		HilightWarriors=4,
		///This causes the right-side menu to be hilighted
		HilightRightSidePanel=5,
		///This causes the minimap icons to be hilighted
		HilightUnderMinimapIcon=6,
		///This causes the units working bar to be hilighted
		HilightUnitsAssignedBar=7,
		///This causes the worker/explorer/warrior ratio bars on a swarm to be hilighted
		HilightRatioBar=8,
		///This causes the workers working/free statistic to be hilighted
		HilightWorkersWorkingFreeStat=9,
		///This causes the exploresrs working/free statistic to be hilighted
		HilightExplorersWorkingFreeStat=10,
		///This causes the warriors working/free statistic to be hilighted
		HilightWarriorsWorkingFreeStat=11,
		///This causes the forbidden zone to be hilighted
		HilightForbiddenZoneOnPanel=12,
		///This causes the defense zone to be hilighted
		HilightGuardZoneOnPanel=13,
		///This causes the clearing zone to be hilighted
		HilightClearingZoneOnPanel=14,
		///This causes the brush selector to be hilighted
		HilightBrushSelector=15,

		///Anything above this number causes a particular building on the right side menu to be hilighted,
		///the value is HilightBuilding+IntBuildingType
		HilightBuildingOnPanel=50,
		///Anything above this number causes the particular building on the actual map to be hilighted
		///the value is HilightBuilding+IntBuildingType
		HilightBuildingOnMap=100,
	};

	///Stores the currently hilighted elements
	std::set<int> hilights;

	struct HilightArrowPosition
	{
		HilightArrowPosition(int x, int y, int sprite) : x(x), y(y), sprite(sprite) {}
		int x;
		int y;
		int sprite;
	};
	///The arrows must be the last things to be drawn,
	///So there positions are stored during the drawing
	///proccess, and they are drawn last
	std::vector<HilightArrowPosition> arrowPositions;

	///This sends the hilight values to the Game class, setting Game::hilightBuildingType and Game::hilightUnitType
	void updateHilightInGame();

	KeyboardManager keyboardManager;
public:
	Game game;
	friend class Game;
	bool gamePaused;
	bool hardPause;
	bool isRunning;
	bool notmenu;
	//! true if user close the glob2 window.
	bool exitGlobCompletely;
	//! true if the game needs to flush all outgoing orders and exit
	bool flushOutgoingAndExit;
	//! if this is not empty, then Engine should load the map with this filename.
	std::string toLoadGameFileName;
	//bool showExtendedInformation;
	bool drawHealthFoodBar, drawPathLines, drawAccessibilityAids;
	int localPlayer, localTeamNo;
	int viewportX, viewportY;
private:
	// Helper function for key and menu
	void repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade);

	bool processGameMenu(SDL_Event *event);
	bool processScrollableWidget(SDL_Event *event);
	void handleRightClick(void);
	void handleKey(SDL_keysym key, bool pressed);
	void handleKeyAlways(void);
	void handleKeyDump(SDL_KeyboardEvent key);
	void handleMouseMotion(int mx, int my, int button);
	void handleMapClick(int mx, int my, int button);
	void handleMenuClick(int mx, int my, int button);
	void handleReplayProgressBarClick(int mx, int my, int button);

	void handleActivation(Uint8 state, Uint8 gain);
	void nextDisplayMode(void);
	void minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport);

	// Drawing support functions
	void drawScrollBox(int x, int y, int value, int valueLocal, int act, int max);
	void drawXPProgressBar(int x, int y, int act, int max);
	void drawButton(int x, int y, std::string caption, int r=128, int g=128, int b=128, bool doLanguageLookup=true);
	void drawBlueButton(int x, int y, std::string caption, bool doLanguageLookup=true);
	void drawRedButton(int x, int y, std::string caption, bool doLanguageLookup=true);
	void drawTextCenter(int x, int y, std::string caption);
	void drawValueAlignedRight(int y, int v);
	void drawCosts(int ressources[BASIC_COUNT], Font *font);
	void drawCheckButton(int x, int y, std::string caption, bool isSet);
	void drawRadioButton(int x, int y, bool isSet);

	void iterateSelection(void);
	void centerViewportOnSelection(void);

	//! Draw the top of screen bar, called by drawOverlayInfos
	void drawTopScreenBar(void);
	//! Draw the infos that are over the others, like the message, the waiting players, ...
	void drawOverlayInfos(void);
	//! Draw the particles (eye-candy)
	void drawParticles(void);
	//! Draw the panel
	void drawPanel(void);
	//! Draw the buttons associated to the panel
	void drawPanelButtons(int y);
	//! Draw a single button of the panel
	void drawPanelButton(int y, int pos, int numButtons, int sprite);
	//! Draw a choice of buildings or flags
	void drawChoice(int pos, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine = 2);
	//! Draw a choice of flags
	void drawFlagView(void);
	//! Draw the infos from a unit
	void drawUnitInfos(void);
	//! Draw the infos and actions from a building
	void drawBuildingInfos(void);
	//! Draw the infos about a ressource on map (type and number left)
	void drawRessourceInfos(void);
	//! Draw the replay panel
	void drawReplayPanel(void);
	//! Draw the bottom bar with the replay's time bar
	void drawReplayProgressBar(bool drawBackground = true);

	//! Draw the menu during game
	void drawInGameMenu(void);
	//! Draw the message input field
	void drawInGameTextInput(void);
	//! Draw the message history field
	void drawInGameScrollableText(void);

	void moveFlag(int mx, int my, bool drop);
	//! One viewport has moved and a flag or a brush is selected, update its position
	void dragStep(int mx, int my, int button);
	//! on each step, check if we have won or lost
	void checkWonConditions(void);

	//! given the game state, change the music
	void musicStep(void);

	friend class InGameAllianceScreen;

	//! Display mode
	enum DisplayMode
	{
		CONSTRUCTION_VIEW=0,
		FLAG_VIEW,
		STAT_TEXT_VIEW,
		STAT_GRAPH_VIEW,
		NB_VIEWS,
	} displayMode;

	//! Display modes in replays
	enum ReplayDisplayMode
	{
		RDM_REPLAY_VIEW,
		RDM_STAT_TEXT_VIEW,
		RDM_STAT_GRAPH_VIEW,
		RDM_NB_VIEWS,
	} replayDisplayMode;

	//! Selection mode
	enum SelectionMode
	{
		NO_SELECTION=0,
		BUILDING_SELECTION,
		UNIT_SELECTION,
		RESSOURCE_SELECTION,
		TOOL_SELECTION,
		BRUSH_SELECTION
	} selectionMode;
	union
	{
		Building* building;
		Unit* unit;
		int ressource;
	} selection;

	// Brushes
	BrushTool brush;
	GameGUIToolManager toolManager;

	//! Unset and clean everything related to the selection so a new one can be set
	void cleanOldSelection(void);
	void setSelection(SelectionMode newSelMode, void* newSelection=NULL);
	void setSelection(SelectionMode newSelMode, unsigned newSelection);
	void clearSelection(void) { setSelection(NO_SELECTION); }
	void checkSelection(void);

	/// This function causes all information about the selected unit to be dumped
	void dumpUnitInformation(void);


	// What's visible or hidden on GUI
	std::vector<std::string> buildingsChoiceName;
	std::vector<bool> buildingsChoiceState;
	std::vector<std::string> flagsChoiceName;
	std::vector<bool> flagsChoiceState;
	enum HidableGUIElements
	{
		HIDABLE_BUILDINGS_LIST = 0x1,
		HIDABLE_FLAGS_LIST = 0x2,
		HIDABLE_TEXT_STAT = 0x4,
		HIDABLE_GFX_STAT = 0x8,
		HIDABLE_ALLIANCE = 0x10,
	};
	Uint32 hiddenGUIElements;

	//! Tells whether a space was clicked recently, to read in by the script engine
	bool hasSpaceBeenClicked;

	//! When set, tells the gui not to treat clicking the space key as usual, but instead, it will "swallow" (ignore) it
	bool swallowSpaceKey;
	//! Set to the SGSL display text of the previous frame. This is so the system knows when the text changes.
	std::string previousSGSLText;
	//! USL script text
	std::string scriptText;
	//! whether script text was updated in last step, required because of our translation override common text mechanism
	bool scriptTextUpdated;

	//! True if the mouse's button way never relased since selection.
	bool selectionPushed;
	//! The position of the flag when it was pushed.
	Sint32 selectionPushedPosX, selectionPushedPosY;
	//! True if the mouse's button way never relased since click im minimap.
	bool miniMapPushed;
	//! True if we try to put a mark in the minimap
	bool putMark;
	//! True if we are panning
	bool panPushed;
	//! Coordinate of mouse when began panning
	int panMouseX, panMouseY;
	//! Coordinate of viewport when began panning
	int panViewX, panViewY;

	bool showStarvingMap;
	bool showDamagedMap;
	bool showDefenseMap;
	bool showFertilityMap;
	OverlayArea overlay;

	bool showUnitWorkingToBuilding;

	TeamStats *teamStats;
	Team *localTeam;

	Uint32 chatMask;

	std::list<boost::shared_ptr<Order> > orderQueue;

	Minimap minimap;

	int mouseX, mouseY;
	//! for mouse motion
	int viewportSpeedX, viewportSpeedY;

	// menu related functions
	enum InGameMenu
	{
		IGM_NONE=0,
		IGM_MAIN,
		IGM_LOAD,
		IGM_SAVE,
		IGM_OPTION,
		IGM_ALLIANCE,
		IGM_OBJECTIVES,
		IGM_END_OF_GAME
	} inGameMenu;
	OverlayScreen *gameMenuScreen;

	///Denotes the name of the game save for saving,
	///set on loading the map
	std::string defualtGameSaveName;

	bool hasEndOfGameDialogBeenShown;

	GameGUIMessageManager messageManager;
	InGameScrollableHistory* scrollableText;

	/// Add a message to the list of messages
	void addMessage(const GAGCore::Color& color, const std::string &msgText, bool chat);

	// Message stuff
	int eventGoPosX, eventGoPosY; //!< position on map of last event
	int eventGoType; //!< type of last event
	int eventGoTypeIterator; //!< iterator to iter on ctrl + space press

	//! Transform a text to multi line according to screen width
	void setMultiLine(const std::string &input, std::vector<std::string> *output, std::string indent="");

	// Typing stuff :
	InGameTextInput *typingInputScreen;
	int typingInputScreenPos;
	int typingInputScreenInc;

	///This manages map marks
	MarkManager markManager;

	//! add a minimap mark
	void addMark(boost::shared_ptr<MapMarkOrder> mmo);

	// records CPU usage percentages
	static const unsigned SMOOTHED_CPU_SIZE=32;
	int smoothedCPULoad[SMOOTHED_CPU_SIZE];
	int smoothedCPUPos;

	// Stuff for the correct working of the campaign
	Campaign* campaign;
	std::string missionName;

	GameGUIDefaultAssignManager defaultAssign;

	GameGUIGhostBuildingManager ghostManager;

	///Because its possible to move the scrollwheel faster than the engine can handle it
	///multiple scroll wheel events compound
	int scrollWheelChanges;

	///This function flushes orders from the scrollWheel at the end of every frame
	void flushScrollWheelOrders();

	//! A particle is cute and only for eye candy
	struct Particle
	{
		float x, y; //!< position on screen in pixels
		float vx, vy; //!< speed in pixels per tick
		float ax, ay; //!< acceleration in pixels per tick
		int age; //!< current age of the particle
		int lifeSpan; //!< maximum age of the particle

		int startImg; //!< image of the particle at birth
		int endImg; //!< image of the partile at death
		Color color; //!< color (team) of this particle
	};

	typedef std::set<Particle*> ParticleSet;

	//! All particles visible on screen
	ParticleSet particles;

	//! Generate new particles if required
	void generateNewParticles(std::set<Building*> *visibleBuildings);
	//! Move all particles by a certain amount of pixels
	void moveParticles(int oldViewportX, int viewportX, int oldViewportY, int viewportY);
};

#endif

