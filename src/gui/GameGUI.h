// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once
#include <MapCamera.h>

#include <InputState.h>
#include <functional>
#include <atomic>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <unordered_map>
#include <valarray>
#include <variant>

#include "Game.h"
#include "TorusView.h"
#include "Brush.h"
#include "Campaign.h"
#include "MapHeader.h"
#include "KeyboardManager.h"
#include "MarkManager.h"
#include "GameGUIMessageManager.h"
#include "GameGUIDialog.h"
#include "GameSpeedControl.h"
#include "render/Minimap.h"
#include "OverlayAreas.h"
#include "scene/SceneExtract.h"
#include "GameGUIToolManager.h"
#include "GameGUIDefaultAssignManager.h"
#include "GameGUIGhostBuildingManager.h"
#include "BuildingGuiState.h"
#include "GameMusicController.h"
#include "sim/ClientCommandSink.h"
#include "sim/ClientEvents.h"
#include "sim/ClientRequests.h"

namespace GAGCore
{
	class BackgroundFileWriter;
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
namespace Hive { class Client; class Dialog; }
class GameGUITouch;
class ConnectionOverlay;
class GameGUI : public ClientCommandSink
{
	friend struct CustomGameSetupHarness;
	friend struct TurnClient;
	friend struct ScriptPresentationFixture;
    friend class TorusRenderIntegrationTest;
    TorusView torusView;
    bool torusPointerDown = false;
    bool torusMapPointer(int x, int y, int &mx, int &my) const;
    bool handleTorusPointer(const SDL_Event &event);
	friend class HighResolutionIntegrationHarness;
	friend class FailingUnitMarkersHarness;
public:
    void drawTorusMap(int originX, int originY, int team, unsigned options, int cloudGridLimit);
	///Constructs a GameGUI
	explicit GameGUI(bool persistPreferences = true);
	
	///Destroys the GameGUI
	~GameGUI();

	///Initializes all variables
	void init();
	///Moves the local viewport
	void adjustInitialViewport();
	void adjustLocalTeam();
	//! Handle mouse, keyboard and window resize inputs, and stats
	void step(void);
    // Host-supplied events and monotonic time; no event polling in this phase.
    void step(const std::vector<SDL_Event>& events, Uint64 now);
    void suspendInput();
    // A deliberate viewport jump is coming: stop any touch coasting first.
    void stopViewportMotion();
	//! Get order from gui, return NullOrder if
	std::shared_ptr<Order> getOrder(void);
	void configureLiveSpectatorView();
	//! Return position on x
	int getViewportX() { return viewportX; }
    void viewportResized(int oldWidth, int oldHeight, int width, int height);
	//! Return position on y
	int getViewportY() { return viewportY; }

	void drawAll(int team);
	void executeOrder(std::shared_ptr<Order> order);

	/// If setGameHeader is true, then the given gameHeader will replace the one loaded with
	/// the map, otherwise it will be ignored
	bool loadFromHeaders(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData=false, bool saveAI=false, const std::string& sourceFileName=std::string());
	GAGCore::CooperativeTask loadFromHeadersTask(MapHeader mapHeader, GameHeader gameHeader, bool setGameHeader, bool ignoreGUIData=false, bool saveAI=false, std::string sourceFileName=std::string());
	//! Same as loadFromHeadersTask, but from an already-open stream: a caller that already
	//! has the bytes in memory (a freshly generated custom game) skips the file entirely.
	GAGCore::CooperativeTask loadFromStreamTask(MapHeader mapHeader, GameHeader gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI, GAGCore::InputStream *stream);
	//!
	bool load(GAGCore::InputStream *stream, bool ignoreGUIData=false);
    GAGCore::CooperativeTask loadTask(GAGCore::InputStream *stream, bool ignoreGUIData=false);
	void save(GAGCore::OutputStream *stream, const std::string name, DeferredGameSHA1* deferredSHA1 = nullptr);

	void processEvent(SDL_Event *event);

	// Engine has to call this every "real" steps. (or game steps)
	void syncStep(void);
	//! Returns once a pending autosave has reached the disk.
	void waitForAutosave();
    bool savePending();
	//! return the local team of the player who is running glob2
	Team *getLocalTeam(void) { return localTeam; }

	//! Apply every queued simulation notice (ClientEvents) to the GUI. The
	//! engine calls this after each tick; executeOrder, step and drawAll call
	//! it too, so the GUI never acts on a stale view of the simulation.
	void consumeClientEvents();

	// Script interface (ClientCommandSink)
	void enableBuildingsChoice(const std::string &name) override;
	void disableBuildingsChoice(const std::string &name) override;
	bool isBuildingEnabled(const std::string &name) override;
	void enableFlagsChoice(const std::string &name) override;
	void disableFlagsChoice(const std::string &name) override;
	bool isFlagEnabled(const std::string &name) override;
	void enableGUIElement(int id) override;
	void disableGUIElement(int id) override;
	void setHighlight(int highlight, bool on) override;

	//! Whether a Space acknowledgement is waiting for the SGSL script.
	bool isSpaceSet() const { return clientRequests.scriptSpacePending(); }
	void setIsSpaceSet(bool value)
	{
		if (value)
			clientRequests.requestScriptSpace();
		else
			clientRequests.discardScriptSpace();
	}
	void setSwallowSpaceKey(bool value) override { swallowSpaceKey=value; }

	void showScriptText(const std::string &text) override;
	void setScriptPresentationText(std::string text, bool publishHistory = true) override;
	void showScriptTextTr(const std::string &text, const std::string &lang) override;
	void hideScriptText() override;

	/// Sets this game as a campaign game from the provided campaign and the provided mission
	void setCampaignGame(Campaign& campaign, const std::string& missionName);
	
	/// Show the dialog that says that the replay ended
	void showEndOfReplayScreen();
	
	///This is an enum for the current highlight object. The highlighted object is shown with a large arrow.
	///This is primarily for tutorials
	enum HighlightObject
	{
		///This causes the main menu icon to be highlighted
		HighlightMainMenuIcon=1,
		///This causes all workers on the map to be highlighted
		HighlightWorkers=2,
		///This causes all explorers on the map to be highlighted
		HighlightExplorers=3,
		///This causes all warriors on the map to be highlighted
		HighlightWarriors=4,
		///This causes the right-side menu to be highlighted
		HighlightRightSidePanel=5,
		///This causes the minimap icons to be highlighted
		HighlightUnderMinimapIcon=6,
		///This causes the units working bar to be highlighted
		HighlightUnitsAssignedBar=7,
		///This causes the worker/explorer/warrior ratio bars on a swarm to be highlighted
		HighlightRatioBar=8,
		///This causes the workers working/free statistic to be highlighted
		HighlightWorkersWorkingFreeStat=9,
		///This causes the explorers working/free statistic to be highlighted
		HighlightExplorersWorkingFreeStat=10,
		///This causes the warriors working/free statistic to be highlighted
		HighlightWarriorsWorkingFreeStat=11,
		///This causes the forbidden zone to be highlighted
		HighlightForbiddenZoneOnPanel=12,
		///This causes the defense zone to be highlighted
		HighlightGuardZoneOnPanel=13,
		///This causes the clearing zone to be highlighted
		HighlightClearingZoneOnPanel=14,
		///This causes the brush selector to be highlighted
		HighlightBrushSelector=15,
		
		///Anything above this number causes a particular building on the right side menu to be highlighted,
		///the value is HighlightBuilding+IntBuildingType
		HighlightBuildingOnPanel=50,
		///Anything above this number causes the particular building on the actual map to be highlighted
		///the value is HighlightBuilding+IntBuildingType
		HighlightBuildingOnMap=100,
	};
	
	///Stores the currently highlighted elements
	std::set<int> highlights;
	
	struct HighlightArrowPosition
	{
		HighlightArrowPosition(int x, int y, int sprite) : x(x), y(y), sprite(sprite) {}
		int x;
		int y;
		int sprite;
	};
	///The arrows must be the last things to be drawn,
	///So there positions are stored during the drawing
	///proccess, and they are drawn last
	std::vector<HighlightArrowPosition> arrowPositions;
	
	///This sends the highlight values to the Game class, setting Game::highlightBuildingType and Game::highlightUnitType
	void updateHighlightInGame();
	
	KeyboardManager keyboardManager;
public:
	///Simulation → client notices and client → simulation requests. Declared
	///before `game`, which keeps pointers to both (Game::clientEvents,
	///Game::clientRequests), so they outlive it.
	ClientEvents clientEvents;
	ClientRequests clientRequests;
	Game game;
	/// Live network games always use normal speed; replays remain adjustable.
	bool canChangeGameSpeed() const;
	/// Chevrons the HUD speed control lights for the current speed.
	int litSpeedChevrons() const;
	/// Step the HUD speed control: forwards to the next chevron (the last wraps
	/// to normal speed), backwards to the previous one.
	void cycleGameSpeed(bool forwards = true);
	/// Ticks per second the simulation is paced at; 0 when uncapped.
	double targetTickRate() const;
	/// The measured tick rate against its target: 0 on pace (or paused, uncapped
	/// or not yet measured), 1 below 90% of it, 2 below 75%.
	int tickRateShortfall() const;
	/// Where the top bar draws the speed control and the tick rate.
	int topBarSpeedX() const;
	/// The scene this frame draws: the simulation's published scene when the
	/// simulation runs on its own thread, else the one drawAll extracted.
	Game *replayTelemetryGame();
	const Scene& drawnScene() const { return publishedScene ? *publishedScene : frameScene; }
	/// Draw scenes published by the simulation thread (null: extract in drawAll).
	void setPublishedScene(const Scene* scene) { publishedScene = scene; }
	/// What the next scene should show; read by extraction, which runs where the
	/// game may be read. GUI state it reads changes only while the simulation is parked.
	SceneRequest sceneRequest();
	/// Extract the next scene from the game (the simulation thread calls this).
	void extractScene(Scene& scene) { sceneExtractor.extract(game, sceneRequest(), scene); }
	/// Per-frame GUI work that reads or writes the game, for threaded execution:
	/// the simulation is parked while it runs (SimulationRunner::withGame).
	void threadedClientStep(const std::vector<SDL_Event>& events, Uint64 now);
	/// True while the simulation runs on its own thread.
	bool simulationThreaded = false;
	/// When the latest tick finished and the interval to the next (ms; 0 = uncapped),
	/// recorded by the engine next to the simulation and copied into extracted Scenes.
	void recordTick(Uint64 time, Uint32 interval)
	{
		lastTickTime = time;
		tickInterval = interval;
		tickClock = (((tickClock >> 32) + 1) << 32) | Uint32(time);
	}
	/// Water and cloud animation phase of this GUI's map view (presentation only).
	int mapAnimationTime() const { return view.render.animationTime; }
	friend class Game;
	// Read by the simulation thread as well as the GUI (see SimulationRunner).
	std::atomic<bool> gamePaused{false};
	std::atomic<bool> hardPause{false};
	std::atomic<bool> isRunning{false};
	bool notmenu;
	//! true if user close the glob2 window.
	bool exitGlobCompletely;
	//! true if the game needs to flush all outgoing orders and exit
	bool flushOutgoingAndExit;
	//! if this is not empty, then Engine should load the map with this filename.
	std::string toLoadGameFileName;
	bool drawHealthFoodBar, drawPathLines, drawAccessibilityAids;
	int localPlayer = 0, localTeamNo = 0;
	std::shared_ptr<Hive::Client> hive;
	void updateCommander(bool caughtUp);
	void openCommander();
	std::unique_ptr<Hive::Dialog> hiveCards;
	bool typingCommander = false;
	bool enqueueCommanderOrders(const std::vector<std::shared_ptr<Order>> &orders, const std::function<bool()> &commit);
	int viewportX, viewportY;
	MapCamera camera;
	bool zoomControlPushed=false;
	void updateCamera();
	bool zoomMap(double steps,int x,int y);
	int mapMouseX(int x)const {return camera.localX(x);}
	int mapMouseY(int y)const {return camera.localY(y);}
	/// Number of consecutive GUI steps the local view has been blocked waiting
	/// on an away/late player (i.e. game.anyPlayerWaited has stayed true). Reset
	/// to 0 as soon as the wait clears. Used only to debounce the on-screen
	/// "[waiting for X]" notice in GameGUIDraw — it is not part of simulation or
	/// network state and is never checksummed, networked, or saved.
	int anyPlayerWaitedTimeFor;
	/// Turn-protocol games: the connection lines to show where the "[waiting for X]"
	/// notice goes (players reconnecting or lagging, our own reconnect or catch-up).
	/// Empty function for every other game; an empty result falls back to the
	/// waiting notice. Presentation only, never simulated or saved.
	std::function<std::vector<std::string>()> connectionNotice;
	/// Turn-protocol games: the connection panel and cards (ConnectionOverlay.h) that
	/// replace the "[waiting for X]" notice. Null for every other game. Presentation
	/// only, never simulated, networked or saved.
	std::unique_ptr<ConnectionOverlay> connectionOverlay;
	/// Turn-protocol games (online or LAN): what the in-game menu offers and what
	/// leaving costs. Loading and saving have no meaning there, and leaving asks first.
	/// Presentation only, never simulated, networked or saved.
	struct NetworkMatch
	{
		bool active = false; ///< a turn-protocol game (online or LAN)
		bool online = false; ///< on a platform instance (else LAN)
		bool rated = false;
		bool fromRoom = true; ///< a room match (else a quick match)
	} networkMatch;
	/// A one-line notice in the message list (connection changes).
	void addNotice(const std::string &text);
	/// Pausing in network matches. Queue matches limit pauses per seat; the turn
	/// session enforces it deterministically (TurnLockstepSession::setPauseLimit).
	/// This is what the menus and the Paused label show of it. Presentation only.
	struct PauseState
	{
		bool limited = false;
		int pausesLeft = -1;  ///< this player's, when limited
		int secondsLeft = -1; ///< this player's pause time left, when limited
		int pausedBy = -1;    ///< the player whose pause is running, or -1
		int pauserSecondsLeft = -1; ///< their pause time left, when limited
	};
	/// Set by the engine for turn games; empty elsewhere (pausing is unlimited).
	std::function<PauseState()> pauseState;
	/// The pause (or resume) the player asked for; a pause they have none left of
	/// is not sent, and says so.
	void requestPause(bool pause);
	/// Whether the menus offer Pause to this player now.
	bool pauseAvailable() const;
private:
	friend class GameGUISelectionHarness;
	friend class SavegameSafetyHarness;
	friend class TorusRenderIntegrationTest;
	friend class TorusRenderBenchmark;
	friend class SoftwareRenderBenchmark;
	bool persistPreferences;
    friend class GameGUITouch;
    friend class GameGUITouchHarness;
	friend class MobileGalleryGameplay;
	std::unique_ptr<GameGUITouch> touch;

	//! Serializes the game and hands the bytes to autosaveWriter.
	void autosave();
	//! Tick of this session's latest autosave, or -1 before the first.
	Sint64 lastAutosaveStep;
    bool autosavePending=false;
	//! Writes autosaves off the game thread; created by the first autosave.
	std::unique_ptr<GAGCore::BackgroundFileWriter> autosaveWriter;

	// Helper function for key and menu
	void repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade);
	
	bool processGameMenu(SDL_Event *event);
	bool processScrollableWidget(SDL_Event *event);
	bool processTypingInput(SDL_Event *event);

	void handleRightClick(void);
	void handleKey(SDL_KeyboardEvent key, bool pressed, bool repeat = false);
	void toggleTorusView();
	void handleKeyAlways(void);
	void handleKeyDump(SDL_KeyboardEvent key);
	void changeGameSpeed(int amount);
	void setGameSpeed(int speed);
	void handleKeySwitchToAreaBrush(int figure);
	void handleKeySelectConstruct(const char *buildingName);
	void handleKeySelectPlaceFlag(const char *flagName);
	void handleKeySelectPlaceArea(GameGUIToolManager::ZoneType zone);
	void handleMouseMotion(int mx, int my, int button);
	void handleMouseButtonDown(SDL_MouseButtonEvent mouseEvent);
	void handleMouseButtonUp(SDL_MouseButtonEvent mouseEvent);
	void handleMenuIconClick(SDL_MouseButtonEvent mouseEvent);
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
	void drawCosts(int resources[BASIC_COUNT], Font *font);
	void drawCheckButton(int x, int y, std::string caption, bool isSet);
	void drawRadioButton(int x, int y, bool isSet);

	void iterateSelection(void);
	void centerViewportOnSelection(void);
	
	//! Draw the top of screen bar, called by drawOverlayInfos
	void drawTopScreenBar(void);
	//! Draw the infos that are over the others, like the message, the waiting players, ...
	void drawOverlayInfos(void);
	//! Draw the particles (eye-candy). @p advance steps their age and physics.
	//! Emission is suppressed separately while the game is paused.
	void drawParticles(bool advance);
	//! Draw the panel: clip rect, background, tutorial highlight, panel buttons,
	//! then defers to dispatchSelectionPanel for the body.
	void drawPanel(void);
	//! Dispatch on selectionMode. BUILDING_/UNIT_/RESOURCE_SELECTION each draw
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
	//! Paint the icon grid for the choice panel and queue any tutorial-highlight arrows.
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
	void drawBuildingHeader(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the building's mini-sprite icon framed by the panel icon backing,
	//! at the current ypos. Does not advance ypos.
	void drawBuildingIcon(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos);
	//! Draw the HP label and current/max value (red below 1/5th max). No
	//! ypos advance — sits in the icon row next to the icon.
	void drawBuildingHP(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos);
	//! Draw the units-inside count ("N/maxUnitInside" when ALIVE, otherwise
	//! the "still N units" message). Ally-gated. No ypos advance.
	void drawBuildingInsideStats(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos);
	//! Draw a flag building's "in way" / "on the spot" unit counts using the
	//! displayed (optimistic) flag position/range so the numbers track flag
	//! movement or range edits. Ally-gated. No ypos advance.
	void drawBuildingFlagInfo(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos);
	//! Draw the "working" label, count, and the maxUnitWorking scrollbox.
	//! Queues the tutorial highlight arrow when active. Ally-gated. Advances
	//! ypos past the working bar when present.
	void drawBuildingWorkingControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the three priority radio buttons (low / medium / high) for
	//! buildings with maxUnitWorking>0. Ally-gated. Advances ypos.
	void drawBuildingPriorityControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the flag's stay-range scrollbox. Ally-gated. Advances ypos.
	void drawBuildingRangeControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the time-to-leave progress bar showing units' insideTimeout (extracted from drawBuildingInfos)
	void drawBuildingTimeToLeaveBar(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos, unsigned& unitInsideBarYDec);
	//! Draw the flag-type-specific controls for clearing/war/exploration flags (extracted from drawBuildingInfos)
	void drawBuildingFlagControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw armor / shoot damage / shoot range text rows for combat buildings.
	//! Advances ypos.
	void drawBuildingCombatStats(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the market exchange panel (per-happyness resource readouts) for
	//! buildings that can exchange and that the local team has shared-vision
	//! exchange visibility on. Advances ypos.
	void drawBuildingExchange(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw non-exchange resource readouts ("name: cur/max") and the bullets
	//! row for shooters. Ally-gated; skipped for exchange buildings. Advances
	//! ypos.
	void drawBuildingResources(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the swarm production progress bar plus the per-unit-type ratio
	//! scrollboxes (worker / explorer / warrior). Queues the ratio-bar
	//! tutorial highlight arrow when active. Ally-gated. Advances ypos.
	void drawBuildingSwarmRatios(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw any "X units can't access resource"-style explanations of why the
	//! building isn't filling its assigned worker slots. Ally-gated. Advances
	//! ypos.
	void drawBuildingFailureReasons(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos);
	//! Draw the repair / upgrade / destroy / cancel action buttons at the
	//! bottom of the panel, plus the upgrade-preview tooltip on hover. Only
	//! shown when the local team owns the building. Uses absolute
	//! bottom-of-screen Y; does not consume ypos.
	void drawBuildingActionButtons(const SceneBuildingPanel* selBuild, BuildingType* buildingType, unsigned unitInsideBarYDec);
	//! Draw the upgrade preview tooltip (cost + new abilities) shown on hover over the upgrade button (extracted from drawBuildingInfos)
	void drawBuildingUpgradePreview(const SceneBuildingPanel* selBuild, BuildingType* buildingType, unsigned unitInsideBarYDec);
	//! Draw the infos about a resource on map (type and number left)
	void drawResourceInfos(void);
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
	//! Queues a move of one of the local team's flags to tile (x, y), replacing any
	//! move of the same flag still in the queue, and shows the flag there at once.
	void queueFlagMove(Building &flag, int x, int y, bool drop);
	//! The local team's flag at a viewport-relative map point: an exact tile hit,
	//! or else the nearest flag whose tile centre is within `reachPoints` screen
	//! points (0 for exact hits only).
	Building *flagAt(int mx, int my, double reachPoints);
	//! The touch reach around flags for a contact at a screen point, in points.
	double flagReachAt(double screenX, double screenY) const;
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
		RESOURCE_SELECTION,
		TOOL_SELECTION,
		BRUSH_SELECTION
	} selectionMode;
	//! Payload for the current selection, tagged by selectionMode. std::monostate
	//! is the active alternative for the three payload-less modes (NO_SELECTION,
	//! and TOOL_/BRUSH_SELECTION, whose real state lives in toolManager/brush).
	//! BUILDING_/UNIT_/RESOURCE_SELECTION hold BuildingRef/UnitRef/int
	//! respectively. Entities are held by reference (gid + generation), never by
	//! pointer, so a selection cannot dangle and cannot jump to a newcomer that
	//! reuses the gid. Read it through selectionBuilding()/selectionUnit()/
	//! selectionResource(), which assert (via std::get) that the active
	//! alternative matches the mode and resolve the entity through Game.
	std::variant<std::monostate, BuildingRef, UnitRef, int> selection;
	
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
	//! The entity accessors return null once the entity is gone.
	Building* selectionBuilding() const { return game.resolveBuilding(std::get<BuildingRef>(selection)); }
	Unit* selectionUnit() const { return game.resolveUnit(std::get<UnitRef>(selection)); }
	int selectionResource() const { return std::get<int>(selection); }
	//! Selected building/unit, or null in any other mode or once it is gone.
	Building* selectedBuildingOrNull() const;
	Unit* selectedUnitOrNull() const;
	void checkSelection(void);
	//! Refresh `view` (resolved pointers for the renderer) and publish the
	//! observed building to clientRequests. Call after the selection or the
	//! simulation changed and before drawing.
	void syncSelectionView(void);
	//! Apply one simulation notice; see consumeClientEvents().
	void handleClientEvent(ClientEventVariant&& event);
	//! GameEvents per team, aged like Team::updateEvents did, until step()
	//! shows the local team's.
	std::array<std::deque<GameEvent>, Team::MAX_COUNT> pendingTeamEvents;

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

	//! When set, tells the gui not to treat clicking the space key as usual, but instead, it will "swallow" (ignore) it
	bool swallowSpaceKey;
	//! Set to the SGSL display text of the previous frame. This is so the system knows when the text changes.
	std::string previousSGSLText;
	//! USL script text
	std::string scriptText;
	//! whether script text was updated in last step, required because of our translation override common text mechanism
	bool scriptTextUpdated;

	//! True if the mouse's button way never released since selection.
	bool selectionPushed;
	//! The position of the flag when it was pushed.
	Sint32 selectionPushedPosX, selectionPushedPosY;
	//! True if the mouse's button way never released since click im minimap.
	bool miniMapPushed;
	//! True if we try to put a mark in the minimap
	bool putMark;
	//! True if we are panning
	bool panPushed;
	//! True while a left-button drag on the map is panning (excluding flag and tool drags)
	bool mapPanPushed = false;
	//! Coordinate of mouse when began panning
	int panMouseX, panMouseY;
	//! Coordinate of viewport when began panning
	int panViewX, panViewY;

	bool showStarvingMap;
	bool showDamagedMap;
	bool showDefenseMap;
	bool showFertilityMap;

	bool showUnitWorkingToBuilding;

	TeamStats *teamStats;
	int measurementPage = 0;
	void drawStatisticsPage(int y);
	//! Each side's chance of winning, under the statistics. Drawn for live
	//! spectators, independently of the game's winning conditions.
	void drawWinProbabilities(int x, int y);
	Team *localTeam;

	Uint32 chatMask;

	std::list<std::shared_ptr<Order> > orderQueue;

	Minimap minimap;

	int mouseX, mouseY;
	//! for mouse motion
	int viewportSpeedX, viewportSpeedY;
	Uint64 lastViewportStep;
    GAGCore::InputState inputState;
    int lastMouseX = 0, lastMouseY = 0;
    Uint32 lastMouseButtonState = 0;

	// menu related functions
	enum InGameMenu
	{
		IGM_NONE = 0,
		IGM_MAIN,
		IGM_LOAD,
		IGM_SAVE,
		IGM_OPTION,
		IGM_ALLIANCE,
		IGM_OBJECTIVES,
		IGM_END_OF_GAME,
		IGM_HIVE,
		IGM_CONFIRM_LEAVE,
		IGM_TELEMETRY
	} inGameMenu;
	// The dialog receiving input, if any: the menu, the chat composer or the history.
	Glob2UI::InGameDialog *activeDialog() const;
	// Show a dialog as the in-game menu of kind `menu`; it replaces any open one.
	void openDialog(InGameMenu menu, std::unique_ptr<Glob2UI::InGameDialog> dialog);
	void closeDialog();
	void openMainMenu();
	/// "Leave match?" with what leaving costs (networkMatch).
	std::unique_ptr<Glob2UI::InGameDialog> makeLeaveConfirmation() const;
	void openChat();
	void closeChat();
	void toggleHistory();
	void saveGameTo(class LoadSaveDialog &dialog);
	/// The single active in-game dialog (main menu, alliances, options, save/load,
	/// objectives, or end-of-game dialog). Non-null iff inGameMenu != IGM_NONE.
	std::unique_ptr<Glob2UI::InGameDialog> gameMenuScreen;

	///Denotes the name of the game save for saving,
	///set on loading the map	
	std::string defaultGameSaveName;

	bool hasEndOfGameDialogBeenShown;

	GameGUIMessageManager messageManager;
	std::unique_ptr<InGameScrollableHistory> scrollableText;

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
	
	// The chat composer while a message is being typed.
	std::unique_ptr<InGameTextInput> typingInputScreen;

	///This manages map marks	
	MarkManager markManager;
	
	//! add a minimap mark
	void addMark(std::shared_ptr<MapMarkOrder> mmo);
	
	// Executed ticks (high half) and when the latest finished (low half, ms).
	// Written by the simulation next to each tick, sampled by drawAll.
	std::atomic<Uint64> tickClock{0};
	// The smoothed simulation tick rate the HUD shows.
	TickRateMeter tickRate;

	// Stuff for the correct working of the campaign
	Campaign* campaign;
	std::string missionName;

	GameGUIDefaultAssignManager defaultAssign;
	
	GameGUIGhostBuildingManager ghostManager;

	///Per-building GUI-side pending order state (optimistic shadow).
	///See BuildingGuiState.h. Public so render code can read pending positions.
	BuildingGuiStateMap buildingGuiState;

	///Per-client viewer state (selection + mouse). NOT simulation state — see
	///Game::ViewState. Owned here (not on Game) and passed into game.drawMap.
	Game::ViewState view;
	///The scene drawn this frame, extracted from `game` at the start of drawAll.
	Scene frameScene;
	Uint64 lastTickTime = 0;
	Uint32 tickInterval = 0;
	///Scene published by the simulation thread, or null when drawAll extracts
	///frameScene itself (serial execution).
	const Scene* publishedScene = nullptr;
	///Extracts frameScene; keeps the state that spans frames (the overlay map).
	SceneExtractor sceneExtractor;

	///Accessor: pending value if set, else authoritative from `b`.
	Sint32 displayedPosX(const Building& b) const;
	Sint32 displayedPosY(const Building& b) const;
	Sint32 displayedMaxUnitWorking(const Building& b) const;
    void requestBuildingConstruction(Building& building);
    void requestBuildingDestruction(Building& building);
    bool requestWorkerAllocation(Building& building, int requested);
    bool requestBuildingPriority(Building& building, int requested);
    bool requestFlagRange(Building& building, int requested);
	Sint32 displayedUnitStayRange(const Building& b) const;
	Sint32 displayedPriority(const Building& b) const;
	bool displayedClearingResource(const Building& b, int i) const;
	Sint32 displayedMinLevelToFlag(const Building& b) const;
	std::array<Sint32, NB_UNIT_TYPE> displayedRatio(const Building& b) const;
	// The same for the selected building's panel model.
	Sint32 displayedPosX(const SceneBuildingPanel& b) const { return ::displayedPosX(buildingGuiState, b); }
	Sint32 displayedPosY(const SceneBuildingPanel& b) const { return ::displayedPosY(buildingGuiState, b); }
	Sint32 displayedMaxUnitWorking(const SceneBuildingPanel& b) const { return ::displayedMaxUnitWorking(buildingGuiState, b); }
	Sint32 displayedUnitStayRange(const SceneBuildingPanel& b) const { return ::displayedUnitStayRange(buildingGuiState, b); }
	Sint32 displayedPriority(const SceneBuildingPanel& b) const { return ::displayedPriority(buildingGuiState, b); }
	bool displayedClearingResource(const SceneBuildingPanel& b, int i) const { return ::displayedClearingResource(buildingGuiState, b, i); }
	Sint32 displayedMinLevelToFlag(const SceneBuildingPanel& b) const { return ::displayedMinLevelToFlag(buildingGuiState, b); }
	std::array<Sint32, NB_UNIT_TYPE> displayedRatio(const SceneBuildingPanel& b) const { return ::displayedRatio(buildingGuiState, b); }

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
		int endImg; //!< image of the particle at death
		Color color; //!< color (team) of this particle
	};
	
	typedef std::set<Particle*> ParticleSet;
	
	//! All particles visible on screen
	ParticleSet particles;
	//! Presentation-only randomness for eye-candy. Never use syncRand() for visual
	//! effects: the synchronized RNG belongs to the simulation and its checksums.
	std::minstd_rand effectsRandom;
	//! Uniform value in [0, 1] from effectsRandom.
	float effectsUnit() { return std::uniform_real_distribution<float>(0.f, 1.f)(effectsRandom); }
	
	//! Generate new particles if required
	void generateNewParticles(std::set<Uint16> *visibleBuildings);
	//! Update overview navigation and particle offsets after viewport movement
	void viewportChanged(int oldViewportX, int viewportX, int oldViewportY, int viewportY);
};
