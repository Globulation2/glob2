// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <valarray>
#include <variant>

#include "Game.h"
#include "Brush.h"
#include "Campaign.h"
#include "MapHeader.h"
#include "KeyboardManager.h"
#include "MarkManager.h"
#include "GameGUIMessageManager.h"
#include "render/Minimap.h"
#include "OverlayAreas.h"
#include "GameGUIToolManager.h"
#include "GameGUIDefaultAssignManager.h"
#include "GameGUIGhostBuildingManager.h"
#include "BuildingGuiState.h"
#include "GameMusicController.h"

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
	std::shared_ptr<Order> getOrder(void);
	//! Return position on x
	int getViewportX() { return viewportX; }
	//! Return position on y
	int getViewportY() { return viewportY; }

	void drawAll(int team);
	void executeOrder(std::shared_ptr<Order> order);

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

	// Sim → GUI lifecycle hooks. The simulation path (Team::syncStep)
	// calls these when a unit dies or a building is demolished, so the
	// sim itself never reads GameGUI-owned selection state. The hook
	// runs entirely on the local client's GUI state; checkSelection()
	// picks up the resulting NULL on the next draw and tears down the
	// rest of the panel. In the Rust port, do not duplicate selection
	// between sim and GUI — keep it solely on per-viewer GUI state and
	// drop these hooks entirely.
	void onUnitDestroyed(Unit *u);
	void onBuildingDestroyed(Building *b);

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
	
	///This sends the hilight values to the Game class, setting Game::highlightBuildingType and Game::highlightUnitType
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
	/// Number of consecutive GUI steps the local view has been blocked waiting
	/// on an away/late player (i.e. game.anyPlayerWaited has stayed true). Reset
	/// to 0 as soon as the wait clears. Used only to debounce the on-screen
	/// "[waiting for X]" notice in GameGUIDraw — it is not part of simulation or
	/// network state and is never checksummed, networked, or saved.
	int anyPlayerWaitedTimeFor;
private:
	// Helper function for key and menu
	void repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade);
	
	bool processGameMenu(SDL_Event *event);
	bool processScrollableWidget(SDL_Event *event);
	void handleRightClick(void);
	void handleKey(SDL_Keysym key, bool pressed);
	void handleKeyAlways(void);
	void handleKeyDump(SDL_KeyboardEvent key);
	void handleKeySwitchToAreaBrush(int figure);
	void handleKeySelectConstruct(const char *buildingName);
	void handleKeySelectPlaceFlag(const char *flagName);
	void handleKeySelectPlaceArea(GameGUIToolManager::ZoneType zone);
	void handleMouseMotion(int mx, int my, int button);
	void handleMapClick(int mx, int my, int button);
	void handleMenuClick(int mx, int my, int button);
	void handleMenuClickBuildingSelection(int mx, int my, int button);
	void handleReplayProgressBarClick(int mx, int my, int button);

	void handleActivation(Uint8 state, Uint8 gain);
	void nextDisplayMode(void);
	void minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport);

	// Drawing support functions
	void drawScrollBox(int x, int y, int valueLocal, int act, int max);
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
	//! Draw the panel: clip rect, background, tutorial hilight, panel buttons,
	//! then defers to dispatchSelectionPanel for the body.
	void drawPanel(void);
	//! Dispatch on selectionMode. BUILDING_/UNIT_/RESSOURCE_SELECTION each draw
	//! their per-selection panel; the default arm forwards to either
	//! dispatchDisplayModePanel or dispatchReplayDisplayModePanel depending on
	//! globalContainer->replaying.
	void dispatchSelectionPanel(void);
	//! Dispatch on displayMode (non-replay path). Asserts on an unknown mode.
	void dispatchDisplayModePanel(void);
	//! Dispatch on replayDisplayMode (replay path). Asserts on an unknown mode.
	void dispatchReplayDisplayModePanel(void);
	//! Draw the buttons associated to the panel
	void drawPanelButtons(int y);
	//! Draw a single button of the panel
	void drawPanelButton(int y, int pos, int numButtons, int sprite);
	//! Draw a choice of buildings or flags. Thin coordinator over the four helpers below.
	//! `panelTopY` is the single Y origin for both the sprite grid and the mouse hit grid;
	//! all four helpers anchor to it so layout and hit-test cannot drift apart.
	void drawChoice(int panelTopY, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine = 2);
	//! Paint the icon grid for the choice panel and queue any tutorial-hilight arrows.
	//! `panelTopY` is the Y of the first row of cells.
	void drawChoiceSprites(int panelTopY, const std::vector<std::string>& types, const std::vector<bool>& states, unsigned numberPerLine);
	//! Paint the selection-highlight sprite over cell `selIdx`.
	//! `panelTopY` is the Y of the first row of cells.
	void drawChoiceHighlight(int panelTopY, size_t selIdx, unsigned numberPerLine);
	//! Return the cell index the mouse is currently over, or nullopt if not over any cell.
	//! `panelTopY` is the Y of the first row of cells — must match the value passed to
	//! drawChoiceSprites / drawChoiceHighlight so click/hover/draw share one origin.
	std::optional<size_t> pickChoiceUnderMouse(int panelTopY, size_t count, unsigned numberPerLine) const;
	//! Paint the resource/info text block at the bottom of the right panel for the given type.
	void drawChoiceInfoPanel(const std::string& type);
	//! Draw a choice of flags
	void drawFlagView(void);
	//! Draw the infos from a unit
	void drawUnitInfos(void);
	//! Draw the infos and actions from a building. Thin coordinator that calls
	//! the per-section helpers below in vertical order.
	void drawBuildingInfos(void);
	//! Draw the centered title row ("<building> (<player>)") and the
	//! subtitle ("level N — (building site) — Prestige"). Advances ypos past
	//! the title block.
	void drawBuildingHeader(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the building's mini-sprite icon framed by the panel icon backing,
	//! at the current ypos. Does not advance ypos.
	void drawBuildingIcon(Building* selBuild, BuildingType* buildingType, int ypos);
	//! Draw the HP label and current/max value (red below 1/5th max). No
	//! ypos advance — sits in the icon row next to the icon.
	void drawBuildingHP(Building* selBuild, BuildingType* buildingType, int ypos);
	//! Draw the units-inside count ("N/maxUnitInside" when ALIVE, otherwise
	//! the "still N units" message). Ally-gated. No ypos advance.
	void drawBuildingInsideStats(Building* selBuild, BuildingType* buildingType, int ypos);
	//! Draw a flag building's "in way" / "on the spot" unit counts using the
	//! displayed (optimistic) flag position/range so the numbers track a drag
	//! or scroll-resize. Ally-gated. No ypos advance.
	void drawBuildingFlagInfo(Building* selBuild, BuildingType* buildingType, int ypos);
	//! Draw the "working" label, count, and the maxUnitWorking scrollbox.
	//! Queues the tutorial hilight arrow when active. Ally-gated. Advances
	//! ypos past the working bar when present.
	void drawBuildingWorkingControls(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the three priority radio buttons (low / medium / high) for
	//! buildings with maxUnitWorking>0. Ally-gated. Advances ypos.
	void drawBuildingPriorityControls(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the flag's stay-range scrollbox. Ally-gated. Advances ypos.
	void drawBuildingRangeControls(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the time-to-leave progress bar showing units' insideTimeout (extracted from drawBuildingInfos)
	void drawBuildingTimeToLeaveBar(Building* selBuild, BuildingType* buildingType, int& ypos, unsigned& unitInsideBarYDec);
	//! Draw the flag-type-specific controls for clearing/war/exploration flags (extracted from drawBuildingInfos)
	void drawBuildingFlagControls(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw armor / shoot damage / shoot range text rows for combat buildings.
	//! Advances ypos.
	void drawBuildingCombatStats(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the market exchange panel (per-happyness ressource readouts) for
	//! buildings that can exchange and that the local team has shared-vision
	//! exchange visibility on. Advances ypos.
	void drawBuildingExchange(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw non-exchange resource readouts ("name: cur/max") and the bullets
	//! row for shooters. Ally-gated; skipped for exchange buildings. Advances
	//! ypos.
	void drawBuildingResources(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the swarm production progress bar plus the per-unit-type ratio
	//! scrollboxes (worker / explorer / warrior). Queues the ratio-bar
	//! tutorial hilight arrow when active. Ally-gated. Advances ypos.
	void drawBuildingSwarmRatios(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw any "X units can't access resource"-style explanations of why the
	//! building isn't filling its assigned worker slots. Ally-gated. Advances
	//! ypos.
	void drawBuildingFailureReasons(Building* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the repair / upgrade / destroy / cancel action buttons at the
	//! bottom of the panel, plus the upgrade-preview tooltip on hover. Only
	//! shown when the local team owns the building. Uses absolute
	//! bottom-of-screen Y; does not consume ypos.
	void drawBuildingActionButtons(Building* selBuild, BuildingType* buildingType, unsigned unitInsideBarYDec);
	//! Draw the upgrade preview tooltip (cost + new abilities) shown on hover over the upgrade button (extracted from drawBuildingInfos)
	void drawBuildingUpgradePreview(Building* selBuild, BuildingType* buildingType, unsigned unitInsideBarYDec);
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
	
	//! Owns the in-game music state machine. Reset by init() at the start of
	//! every loaded game; advanced once per simulation tick from stepGameLogic.
	GameMusicController musicController;

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
	//! Payload for the current selection, tagged by selectionMode. std::monostate
	//! is the active alternative for the three payload-less modes (NO_SELECTION,
	//! and TOOL_/BRUSH_SELECTION, whose real state lives in toolManager/brush).
	//! BUILDING_/UNIT_/RESSOURCE_SELECTION hold Building*/Unit*/int respectively.
	//! Read it through selectionBuilding()/selectionUnit()/selectionRessource(),
	//! which assert (via std::get) that the active alternative matches the mode.
	std::variant<std::monostate, Building*, Unit*, int> selection;
	
	// Brushes
	BrushTool brush;
	GameGUIToolManager toolManager;

	//! Unset and clean everything related to the selection so a new one can be set
	void cleanOldSelection(void);
	void setSelection(SelectionMode newSelMode, void* newSelection=NULL);
	void setSelection(SelectionMode newSelMode, unsigned newSelection);
	void clearSelection(void) { setSelection(NO_SELECTION); }
	//! Typed selection-payload accessors. Each asserts (via std::get) that the
	//! active variant alternative matches selectionMode; a tag/payload desync
	//! throws std::bad_variant_access rather than silently reinterpreting bytes.
	//! Precondition: selectionMode is the matching mode (caller-guaranteed).
	Building* selectionBuilding() const { return std::get<Building*>(selection); }
	Unit* selectionUnit() const { return std::get<Unit*>(selection); }
	int selectionRessource() const { return std::get<int>(selection); }
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

	std::list<std::shared_ptr<Order> > orderQueue;

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
	/// The single active in-game overlay (main menu, alliances, options, save/load,
	/// objectives, or end-of-game dialog). Non-null iff inGameMenu != IGM_NONE.
	/// Owned here: reset()/assignment auto-deletes the previous overlay, so callers
	/// never pair delete with NULL by hand. (Rust port: Option<Box<dyn OverlayScreen>>.)
	std::unique_ptr<OverlayScreen> gameMenuScreen;

	///Denotes the name of the game save for saving,
	///set on loading the map	
	std::string defaultGameSaveName;

	bool hasEndOfGameDialogBeenShown;
	
	GameGUIMessageManager messageManager;
	InGameScrollableHistory* scrollableText;

	/// Selects which message-history list a wrapped line is appended to.
	enum class HistoryList { Game, Chat };

	/// Continuation-line indent applied when word-wrapping script text into
	/// the chat history. Distinguishes wrapped continuation visually from a
	/// new message.
	static constexpr const char* kScriptTextContinuationIndent = "    ";
	/// timeLeft sentinel meaning "do not draw as a transient floating
	/// message". The line is still appended to the history list and remains
	/// visible only via the scrollable history overlay.
	static constexpr int kHistoryOnlyTimeoutMs = 0;
	/// Default lifetime (ms) for a transient game-event toast. Mirrors the
	/// default argument of the InGameMessage constructor; named here so
	/// callers can pass it explicitly instead of relying on the default.
	static constexpr int kGameMessageDefaultTimeoutMs = 8000;
	/// Lifetime (ms) for a transient chat broadcast — kept on screen
	/// longer than normal so multi-line broadcasts are readable before
	/// fading.
	static constexpr int kChatBroadcastTimeoutMs = 16000;

	/// Add a message to the list of messages
	void addMessage(const GAGCore::Color& color, const std::string &msgText, bool chat);

	//! Word-wrap \a text via setMultiLine and append every resulting line to
	//! one of the message-history lists. Both histories are LIFO
	//! (push_front), so the wrapped lines are fed in reverse to preserve
	//! the original top-to-bottom reading order on screen. \a target picks
	//! which list receives them; \a lineColor and \a lineTimeoutMs are
	//! passed straight through to each per-line InGameMessage.
	void publishMessageHistoryLines(const std::string& text, HistoryList target,
		const GAGCore::Color& lineColor, int lineTimeoutMs, const std::string& indent);

	// Message stuff
	int eventGoPosX, eventGoPosY; //!< position on map of last event
	int eventGoType; //!< type of last event
	int eventGoTypeIterator; //!< iterator to iter on ctrl + space press
	
	//! Word-wrap \a input into \a output, breaking at spaces so each line fits the
	//! message-panel pixel width (screen width minus right menu and side padding),
	//! measured via globalContainer->standardFont. Continuation lines are prefixed
	//! with \a indent. Empty input yields an empty output (no lines pushed).
	void setMultiLine(const std::string &input, std::vector<std::string> *output, std::string indent="");
	
	// Typing stuff :
	InGameTextInput *typingInputScreen;
	int typingInputScreenPos;
	int typingInputScreenInc;

	///This manages map marks	
	MarkManager markManager;
	
	//! add a minimap mark
	void addMark(std::shared_ptr<MapMarkOrder> mmo);
	
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

	///Per-building GUI-side pending order state (optimistic shadow).
	///See BuildingGuiState.h. Public so render code can read pending positions.
	BuildingGuiStateMap buildingGuiState;

	///Per-client viewer state (selection + mouse). NOT simulation state — see
	///Game::ViewState. Owned here (not on Game) and passed into game.drawMap.
	Game::ViewState view;

	///Accessor: pending value if set, else authoritative from `b`.
	Sint32 displayedPosX(const Building& b) const;
	Sint32 displayedPosY(const Building& b) const;
	Sint32 displayedMaxUnitWorking(const Building& b) const;
	Sint32 displayedUnitStayRange(const Building& b) const;
	Sint32 displayedPriority(const Building& b) const;
	bool displayedClearingResource(const Building& b, int i) const;
	Sint32 displayedMinLevelToFlag(const Building& b) const;
	std::array<Sint32, NB_UNIT_TYPE> displayedRatio(const Building& b) const;

	///Get-or-create the pending state for a building (used by GUI mutators).
	BuildingGuiState& pendingFor(Uint16 gid) { return buildingGuiState[gid]; }

	///Called from executeOrder: clear pending fields the order has now made authoritative.
	void reconcileBuildingGuiState(const std::shared_ptr<Order>& order);
	
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


