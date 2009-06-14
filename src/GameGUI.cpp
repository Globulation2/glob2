/*
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

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <sstream>
#include <iostream>
#include <algorithm>

#include <FileManager.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIStyle.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <FormatableString.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "IRC.h"
#include "SoundMixer.h"
#include "VoiceRecorder.h"
#include "GameGUIKeyActions.h"
#include "Player.h"

#include "config.h"

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_keysym.h>
#else
#include <Types.h>
#endif

#define TYPING_INPUT_BASE_INC 7
#define TYPING_INPUT_MAX_POS 46

// these values are manually layouted for cuteste perception
#define YPOS_BASE_DEFAULT 180
#define YPOS_BASE_CONSTRUCTION (YPOS_BASE_DEFAULT + 5)
#define YPOS_BASE_FLAG (YPOS_BASE_DEFAULT + 5)
#define YPOS_BASE_STAT YPOS_BASE_DEFAULT
#define YPOS_BASE_BUILDING (YPOS_BASE_DEFAULT + 10)
#define YPOS_BASE_UNIT (YPOS_BASE_DEFAULT + 10)
#define YPOS_BASE_RESSOURCE YPOS_BASE_DEFAULT

#define YOFFSET_NAME 28
#define YOFFSET_ICON 52
#define YOFFSET_CARYING 34
#define YOFFSET_BAR 32
#define YOFFSET_INFOS 12
#define YOFFSET_TOWER 22

#define YOFFSET_B_SEP 6

#define YOFFSET_TEXT_BAR 16
#define YOFFSET_TEXT_PARA 14
#define YOFFSET_TEXT_LINE 12

#define YOFFSET_PROGRESS_BAR 10

#define YOFFSET_BRUSH 56

#define RIGHT_MENU_WIDTH 160
#define RIGHT_MENU_HALF_WIDTH (RIGHT_MENU_WIDTH / 2)
#define RIGHT_MENU_OFFSET ((RIGHT_MENU_WIDTH -128)/2)
#define RIGHT_MENU_RIGHT_OFFSET (RIGHT_MENU_WIDTH - RIGHT_MENU_OFFSET)

#define REPLAY_PANEL_XOFFSET 25
#define REPLAY_PANEL_YOFFSET (YPOS_BASE_STAT+10)
#define REPLAY_PANEL_SPACE_BETWEEN_OPTIONS 22
#define REPLAY_PANEL_PLAYERLIST_YOFFSET (5*REPLAY_PANEL_SPACE_BETWEEN_OPTIONS+5)

using namespace boost;

enum GameGUIGfxId
{
	EXCHANGE_BUILDING_ICONS = 21
};

//! The screen that contains the text input while typing message in game
class InGameTextInput:public OverlayScreen
{
protected:
	//! the text input widget
	TextInput *textInput;

public:
	//! InGameTextInput constructor
	InGameTextInput(GraphicContext *parentCtx);
	//! InGameTextInput destructor
	virtual ~InGameTextInput() { }
	//! React on action from any widget (but there is only one anyway)
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	//! Return the text typed
	std::string getText(void) const { return textInput->getText(); }
	//! Set the text
	void setText(const char *text) const { textInput->setText(text); }
};

InGameTextInput::InGameTextInput(GraphicContext *parentCtx)
:OverlayScreen(parentCtx, 492, 34)
{
	textInput=new TextInput(5, 5, 482, 24, ALIGN_LEFT, ALIGN_LEFT, "standard", "", true, 256);
	addWidget(textInput);
	dispatchInit();
}

void InGameTextInput::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==TEXT_VALIDATED)
	{
		endValue=0;
	}
}

GameGUI::GameGUI()
	: keyboardManager(GameGUIShortcuts), game(this), toolManager(game, brush, defaultAssign, ghostManager),
	  minimap(globalContainer->runNoX, 
	         RIGHT_MENU_WIDTH, // width of the menu
	         globalContainer->gfx->getW(), // width of the screen
	         20, // x offset
	         10, // y offset
	         128, // width
	         128, //height
	         Minimap::ShowFOW), // minimap mode
	  
	  ghostManager(game)
{
}

GameGUI::~GameGUI()
{
	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); ++it)
		delete *it;
}

void GameGUI::init()
{
	notmenu = false;
	isRunning=true;
	gamePaused=false;
	hardPause=false;
	exitGlobCompletely=false;
	flushOutgoingAndExit=false;
	drawHealthFoodBar=true;
	drawPathLines=false;
	drawAccessibilityAids=false;
	viewportX=0;
	viewportY=0;
	mouseX=0;
	mouseY=0;
	displayMode=CONSTRUCTION_VIEW;
	replayDisplayMode=RDM_REPLAY_VIEW;
	selectionMode=NO_SELECTION;
	selectionPushed=false;
	selection.building = NULL;
	selection.unit = NULL;
	miniMapPushed=false;
	putMark=false;
	showUnitWorkingToBuilding=true;
	chatMask=0xFFFFFFFF;
	hasSpaceBeenClicked=false;
	swallowSpaceKey=false;
	scriptTextUpdated = false;

	viewportSpeedX=0;
	viewportSpeedY=0;

	showStarvingMap=false;
	showDamagedMap=false;
	showDefenseMap=false;
	showFertilityMap=false;
	
	inGameMenu=IGM_NONE;
	gameMenuScreen=NULL;
	typingInputScreen=NULL;
	scrollableText=NULL;
	typingInputScreenPos=0;

	eventGoTypeIterator = 0;
	localTeam=NULL;
	teamStats=NULL;

	hasEndOfGameDialogBeenShown=false;
	panPushed=false;

	buildingsChoiceName.clear();
	buildingsChoiceName.push_back("swarm");
	buildingsChoiceName.push_back("inn");
	buildingsChoiceName.push_back("hospital");
	buildingsChoiceName.push_back("racetrack");
	buildingsChoiceName.push_back("swimmingpool");
	buildingsChoiceName.push_back("barracks");
	buildingsChoiceName.push_back("school");
	buildingsChoiceName.push_back("defencetower");
	buildingsChoiceName.push_back("stonewall");
	buildingsChoiceName.push_back("market");
	buildingsChoiceState.resize(buildingsChoiceName.size(), true);

	flagsChoiceName.clear();
	flagsChoiceName.push_back("explorationflag");
	flagsChoiceName.push_back("warflag");
	flagsChoiceName.push_back("clearingflag");
	flagsChoiceState.resize(flagsChoiceName.size(), true);

	hiddenGUIElements=0;
	
 	for (size_t i=0; i<SMOOTHED_CPU_SIZE; i++)
		smoothedCPULoad[i]=0;
	smoothedCPUPos=0;

	campaign=NULL;
	missionName="";
	
	scrollWheelChanges=0;
	
	hilights.clear();
}

void GameGUI::adjustLocalTeam()
{
	assert(localTeamNo>=0);
	assert(localTeamNo<Team::MAX_COUNT);
	assert(game.gameHeader.getNumberOfPlayers()>0);
	assert(game.gameHeader.getNumberOfPlayers()<Team::MAX_COUNT);
	assert(localTeamNo<game.mapHeader.getNumberOfTeams());

	localTeam = game.teams[localTeamNo];
	assert(localTeam);
	teamStats = &localTeam->stats;
	
	// recompute local forbidden and guard areas
	game.map.computeLocalForbidden(localTeamNo);
	game.map.computeLocalGuardArea(localTeamNo);
	game.map.computeLocalClearArea(localTeamNo);
	
	// set default event position
	eventGoPosX = localTeam->startPosX;
	eventGoPosY = localTeam->startPosY;
	eventGoType = 0;
}

void GameGUI::adjustInitialViewport()
{
	assert(localTeam);
	viewportX=localTeam->startPosX-((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
	viewportY=localTeam->startPosY-(globalContainer->gfx->getH()>>6);
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();
}

void GameGUI::moveFlag(int mx, int my, bool drop)
{
	if (globalContainer->replaying) return;

	int posX, posY;
	Building* selBuild=selection.building;
	game.map.cursorToBuildingPos(mx, my, selBuild->type->width, selBuild->type->height, &posX, &posY, viewportX, viewportY);
	if ((selBuild->posXLocal!=posX)
		||(selBuild->posYLocal!=posY)
		||(drop && (selectionPushedPosX!=posX || selectionPushedPosY!=posY)))
	{
		Uint16 gid=selBuild->gid;
		shared_ptr<OrderMoveFlag> oms(new OrderMoveFlag(gid, posX, posY, drop));
		// First, we check if anoter move of the same flag is already in the "orderQueue".
		bool found=false;
		for (std::list<shared_ptr<Order> >::iterator it=orderQueue.begin(); it!=orderQueue.end(); ++it)
		{
			if ( ((*it)->getOrderType()==ORDER_MOVE_FLAG))
			{
				if(static_pointer_cast<OrderMoveFlag>(*it)->gid==gid)
				{
					(*it) = oms;
					found=true;
					break;
				}
			}
		}
		if (!found)
			orderQueue.push_back(oms);
		selBuild->posXLocal=posX;
		selBuild->posYLocal=posY;
	}
}

void GameGUI::dragStep(int mx, int my, int button)
{
	/* We used to use SDL_GetMouseState, like the following
		commented-out code, but that was buggy and prevented
		dragging from correctly going through intermediate cells.
		It is vital to use the mouse position and button status as
		it was at the time in the middle of the event stream, not
		as it is now.  So instead we make sure the correct data is
		passed to us as a parameter. */
	// int mx, my;
	// Uint8 button = SDL_GetMouseState(&mx, &my);
        // fprintf (stderr, "enter dragStep: button: %d, mx: %d, selectionMode: %d\n", button, mx, selectionMode);
	if ((button&SDL_BUTTON(1)) && (mx<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH))
	{
		// Update flag
		if (selectionMode == BUILDING_SELECTION)
		{
			Building* selBuild=selection.building;
			if (selBuild && selectionPushed && (selBuild->type->isVirtual))
				moveFlag(mx, my, false);
		}
		// Update tool
		else if (selectionMode==BRUSH_SELECTION || selectionMode==TOOL_SELECTION)
		{
			toolManager.handleMouseDrag(mx, my, localTeamNo, viewportX, viewportY);
		}
	}
        // fprintf (stderr, "exit dragStep\n");
}

/* We need to keep track of the last recorded mouse position for use
   in drag steps.  We can't simply use SDL_GetMouseState to get this
   information, because we need the information as it was in the
   middle of the event stream.  (There may be many later events we
   have not yet processed.) */
int lastMouseX = 0, lastMouseY = 0; // can't make these Uint16 because of SDL_GetMouseState
Uint16 lastMouseButtonState = 0;

void GameGUI::step(void)
{
	SDL_Event event, mouseMotionEvent, windowEvent;
	bool wasMouseMotion=false;
	bool wasWindowEvent=false;
	int oldMouseMapX = -1, oldMouseMapY = -1; // hopefully the values here will never matter
	// we get all pending events but for mousemotion we only keep the last one
	while (SDL_PollEvent(&event))
	{
		if (event.type==SDL_MOUSEMOTION)
		{
			lastMouseX = event.motion.x;
			lastMouseY = event.motion.y;
			lastMouseButtonState = event.motion.state;
			int mouseMapX, mouseMapY;
			bool onViewport = (lastMouseX < globalContainer->gfx->getW()-RIGHT_MENU_WIDTH);
			/* We keep track for each mouse motion event
				of which map cell it corresponds to.  When
				dragging, we will use this to make sure we
				process at least one event per map cell,
				and only discard multiple events when they
				are for the same map cell.  This is
				necessary to make dragging work correctly
				when drawing areas with the brush. */
			if (onViewport)
			{
				game.map.cursorToBuildingPos (lastMouseX, lastMouseY, 1, 1, &mouseMapX, &mouseMapY, viewportX, viewportY);
			}
			else
			{
				/* We interpret all locations outside the
					viewport as being equivalent, and
					distinct from any map location. */
				mouseMapX = -1;
				mouseMapY = -1;
			}
			// fprintf (stderr, "mouse motion: (lastMouseX,lastMouseY): (%d,%d), (mouseMapX,mouseMapY): (%d,%d), (oldMouseMapX,oldMouseMapY): (%d,%d)\n", lastMouseX, lastMouseY, mouseMapX, mouseMapY, oldMouseMapX, oldMouseMapY);
			/* Make sure dragging does not skip over map cells by
				processing the old stored event rather than throwing
				it away. */
			if (wasMouseMotion
				&& (lastMouseButtonState & SDL_BUTTON(1)) // are we dragging? (should not be hard-coding this condition but should be abstract somehow)
				&& ((mouseMapX != oldMouseMapX)
					|| (mouseMapY != oldMouseMapY))
			)
			{
				// fprintf (stderr, "processing old event instead of discarding it\n");
				processEvent(&mouseMotionEvent);
			}
			oldMouseMapX = mouseMapX;
			oldMouseMapY = mouseMapY;
			mouseMotionEvent=event;
			wasMouseMotion=true;
		}
#		ifdef USE_OSX
		else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q && SDL_GetModState() & KMOD_META)
		{
			isRunning=false;
			exitGlobCompletely=true;
		}
#		endif
#		ifdef USE_WIN32
		else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F4 && SDL_GetModState() & KMOD_ALT)
		{
			isRunning=false;
			exitGlobCompletely=true;
		}
#		endif
		else if ((event.type == SDL_MOUSEBUTTONDOWN) || (event.type == SDL_MOUSEBUTTONUP))
		{
			lastMouseButtonState = SDL_GetMouseState (&lastMouseX, &lastMouseY);
			/* We ignore what SDL_GetMouseState does to
				lastMouseX and lastMouseY, because that may
				reflect many subsequent events that we have not
				yet processed.  Technically, we shouldn't use
				SDL_GetMouseState at all but should calculate the
				button state by keeping track of what has
				happened.  However, I haven't had the programming
				energy to do this, so I am cheating in the line
				above. */
			lastMouseX = event.button.x;
			lastMouseY = event.button.y;
			processEvent (&event);
		}
		else if (event.type==SDL_ACTIVEEVENT)
		{
			windowEvent=event;
			wasWindowEvent=true;
		}
		else
		{
			processEvent(&event);
		}
	}
	if (wasMouseMotion)
		processEvent(&mouseMotionEvent);
	if (wasWindowEvent)
		processEvent(&windowEvent);

	flushScrollWheelOrders();

	int oldViewportX = viewportX;
	int oldViewportY = viewportY;
	
	viewportX += game.map.getW();
	viewportY += game.map.getH();
	handleKeyAlways();
	viewportX += viewportSpeedX;
	viewportY += viewportSpeedY;
	viewportX &= game.map.getMaskW();
	viewportY &= game.map.getMaskH();

	if ((viewportX!=oldViewportX) || (viewportY!=oldViewportY))
	{
		dragStep(lastMouseX, lastMouseY, lastMouseButtonState);
		moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
	}

	assert(localTeam);
	boost::shared_ptr<GameEvent> gevent = localTeam->getEvent();
	while(gevent)
	{
		Color c = gevent->formatColor();
		addMessage(c, gevent->formatMessage(), false);
		eventGoPosX = gevent->getX();
		eventGoPosY = gevent->getY();
		eventGoType = gevent->getEventType();
		gevent = localTeam->getEvent();
	}
	
	// voice step
	boost::shared_ptr<OrderVoiceData> orderVoiceData;
	while ((orderVoiceData = globalContainer->voiceRecorder->getNextOrder()) != NULL)
	{
		orderVoiceData->recepientsMask = chatMask ^ (chatMask & (1<<localPlayer));
		orderQueue.push_back(orderVoiceData);
	}
	
	// TODO: die with SGSL
	// Check if the text being displayed has changed, and if it has, add it to the history box
	if(game.script.isTextShown && game.script.textShown != previousSGSLText)
	{
		//Split into one per line
		std::vector<std::string> messages;
		setMultiLine(game.script.textShown, &messages, "    ");

		///Add each line as a seperate message to the message manager.
		///Must be done backwards to appear in the right order
		for (int i=messages.size()-1; i>=0; i--)
		{
			messageManager.addChatMessage(InGameMessage(messages[i], Color(255, 255, 255), 0));
		}
		
		previousSGSLText = game.script.textShown;
	}
	
	// Check if the text being displayed has changed, and if it has, add it to the history box
	if (scriptTextUpdated)
	{
		// Split into one per line
		std::vector<std::string> messages;
		setMultiLine(scriptText, &messages, "    ");
	
		// Add each line as a seperate message to the message manager.
		// Must be done backwards to appear in the right order
		for (int i=messages.size()-1; i>=0; i--)
		{
			messageManager.addChatMessage(InGameMessage(messages[i], Color(255, 255, 255), 0));
		}
		
		scriptTextUpdated = false;
	}
	
	// music step
	musicStep();

	boost::shared_ptr<Order> order = toolManager.getOrder();
	while(order)
	{
		orderQueue.push_back(order);
		order = toolManager.getOrder();
	}

	///This shows the mission briefing at the begginning of the mission
	if(game.stepCounter == 12)
	{
		if(game.missionBriefing != "")
		{
			if(gameMenuScreen)
			{
				delete gameMenuScreen;
				gameMenuScreen=NULL;
			}
			inGameMenu=IGM_OBJECTIVES;
			gameMenuScreen = new InGameObjectivesScreen(this, true);
		}
	}

	if(game.stepCounter % 25 == 1)
	{
		if(showStarvingMap)
			overlay.compute(game, OverlayArea::Starving, localTeamNo);
		else if(showDamagedMap)
			overlay.compute(game, OverlayArea::Damage, localTeamNo);
		else if(showDefenseMap)
			overlay.compute(game, OverlayArea::Defence, localTeamNo);
		else if(showFertilityMap)
			overlay.compute(game, OverlayArea::Fertility, localTeamNo);
	}

	// do we have won or lost conditions
	checkWonConditions();
	
	if (game.anyPlayerWaited) // TODO: warning valgrind
		game.anyPlayerWaitedTimeFor++;
}

void GameGUI::musicStep(void)
{
	static unsigned warTimeout = 0;
	static unsigned buildingTimeout = 0;
	
	// something bad happened
	if (localTeam->wasRecentEvent(GEUnitUnderAttack) ||
		localTeam->wasRecentEvent(GEUnitLostConversion) ||
		localTeam->wasRecentEvent(GEBuildingUnderAttack))
	{
	   warTimeout = 220;
	   globalContainer->mix->setNextTrack(4, true);
	}
	
	// something good happened
	if (localTeam->wasRecentEvent(GEUnitGainedConversion) ||
		localTeam->wasRecentEvent(GEBuildingCompleted))
	{
		buildingTimeout = 220;
		globalContainer->mix->setNextTrack(3, true);
	}
	
	// if end of special thing
	if ((buildingTimeout == 1) || (warTimeout == 1))
		globalContainer->mix->setNextTrack(2, true);
	
	// decay variables
	if (warTimeout > 0)
		warTimeout--;
	if (buildingTimeout > 0)
		buildingTimeout--;
}

void GameGUI::syncStep(void)
{
	assert(localTeam);
	assert(teamStats);

	if ((game.stepCounter&255) == 79)
	{
		const char *name = Toolkit::getStringTable()->getString("[auto save]");
		std::string fileName = glob2NameToFilename("games", name, "game");
		OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(fileName));
		if (stream->isEndOfStream())
		{
			std::cerr << "GameGUI::syncStep : can't open autosave file " << name << " for writing" << std::endl;
		}
		else
		{
			save(stream, name);
		}
		delete stream;
	}
}

bool GameGUI::processScrollableWidget(SDL_Event *event)
{
	scrollableText->translateAndProcessEvent(event);
	return true;
}

bool GameGUI::processGameMenu(SDL_Event *event)
{
	gameMenuScreen->translateAndProcessEvent(event);
	switch (inGameMenu)
	{
		case IGM_MAIN:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameMainScreen::LOAD_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_LOAD;
					if (globalContainer->replaying)
						gameMenuScreen = new LoadSaveScreen("replays", "replay", true, std::string(Toolkit::getStringTable()->getString("[load replay]")), defualtGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					else
						gameMenuScreen = new LoadSaveScreen("games", "game", true, false, defualtGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					return true;
				}
				break;
				case InGameMainScreen::SAVE_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_SAVE;
					gameMenuScreen = new LoadSaveScreen("games", "game", false, false, defualtGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					return true;
				}
				break;
				case InGameMainScreen::OPTIONS:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_OPTION;
					gameMenuScreen = new InGameOptionScreen(this);
					return true;
				}
				break;
				case InGameMainScreen::RETURN_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_NONE;
					gameMenuScreen=NULL;
					return true;
				}
				break;
				case InGameMainScreen::QUIT_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_NONE;
					gameMenuScreen=NULL;
					orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
					flushOutgoingAndExit=true;
					return true;
				}
				break;
				default:
				return false;
			}
		}

		case IGM_ALLIANCE:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameAllianceScreen::OK :
				{
					Uint32 playerMask[5];
					Uint32 teamMask[5];
					playerMask[0]=((InGameAllianceScreen *)gameMenuScreen)->getAlliedMask();
					playerMask[1]=((InGameAllianceScreen *)gameMenuScreen)->getEnemyMask();
					playerMask[2]=((InGameAllianceScreen *)gameMenuScreen)->getExchangeVisionMask();
					playerMask[3]=((InGameAllianceScreen *)gameMenuScreen)->getFoodVisionMask();
					playerMask[4]=((InGameAllianceScreen *)gameMenuScreen)->getOtherVisionMask();
					teamMask[0]=teamMask[1]=teamMask[2]=teamMask[3]=teamMask[4]=0;

					// mask are for players, we need to convert them to team.
					for (int pi=0; pi<game.gameHeader.getNumberOfPlayers(); pi++)
					{
						int otherTeam=game.players[pi]->teamNumber;
						for (int mi=0; mi<5; mi++)
						{
							if (playerMask[mi]&(1<<pi))
							{
								// player is set, set team
								teamMask[mi]|=(1<<otherTeam);
							}
						}
					}
					
					// we have a special cases for uncontroled Teams:
					// FIXME : remove this
					for (int ti=0; ti<game.mapHeader.getNumberOfTeams(); ti++)
						if (game.teams[ti]->playersMask==0)
							teamMask[1]|=(1<<ti); // we want to hit them.
					
					orderQueue.push_back(shared_ptr<Order>(new SetAllianceOrder(localTeamNo,
						teamMask[0], teamMask[1], teamMask[2], teamMask[3], teamMask[4])));
					chatMask=((InGameAllianceScreen *)gameMenuScreen)->getChatMask();
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					gameMenuScreen=NULL;
				}
				return true;

				default:
				return false;
			}
		}

		case IGM_OPTION:
		{
			if (gameMenuScreen->endValue == InGameOptionScreen::OK)
			{
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;
			}
			else
			{
				return false;
			}
		}

		case IGM_OBJECTIVES:
		{
			if (gameMenuScreen->endValue == InGameObjectivesScreen::OK)
			{
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;
			}
			else
			{
				return false;
			}
		}

		case IGM_LOAD:
		case IGM_SAVE:
		{
			switch (gameMenuScreen->endValue)
			{
				case LoadSaveScreen::OK:
				{
					std::string locationName=((LoadSaveScreen *)gameMenuScreen)->getFileName();
					if (inGameMenu==IGM_LOAD)
					{
						toLoadGameFileName = locationName;
						orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
					}
					else
					{
    					defualtGameSaveName=((LoadSaveScreen *)gameMenuScreen)->getName();
						OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(locationName));
						if (stream->isEndOfStream())
						{
							std::cerr << "GGU : Can't save map " << locationName << std::endl;
						}
						else
						{
							const char *name = ((LoadSaveScreen *)gameMenuScreen)->getName();
							assert(name);
							save(stream, name);
						}
						delete stream;
					}
				}

				case LoadSaveScreen::CANCEL:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;

				default:
				return false;
			}
		}

		case IGM_END_OF_GAME:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameEndOfGameScreen::QUIT:
				orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
				flushOutgoingAndExit=true;

				case InGameEndOfGameScreen::CONTINUE:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				gamePaused = false; //!< At the end of replays, the game is paused.
				return true;

				default:
				return false;
			}
		}

		default:
		return false;
	}
}

void GameGUI::processEvent(SDL_Event *event)
{
	// handle typing
	if (typingInputScreen)
	{
		if ((event->type==SDL_KEYDOWN) && (event->key.keysym.sym == SDLK_ESCAPE))
		{
			typingInputScreenInc=-TYPING_INPUT_BASE_INC;
			typingInputScreen->endValue=1;
		}
		
		typingInputScreen->translateAndProcessEvent(event);
		
		if (typingInputScreen->endValue==0)
		{
			//Interpret message
			std::string message = typingInputScreen->getText();
			Uint32 nchatMask = chatMask;
			if(message[0] == '/')
			{
				std::string name;
				for(int i=1; message[i]!=' '; ++i)
					name += message[i];
				message = message.substr(message.find(' ')+1);
				if(name=="a")
				{
					nchatMask = localTeam->allies;
				}
				else
				{
					for(int i=0; i<game.gameHeader.getNumberOfPlayers(); ++i)
					{
						if(name == game.gameHeader.getBasePlayer(i).name)
						{
							nchatMask = game.gameHeader.getBasePlayer(i).teamNumberMask | localTeam->me;
							break;
						}
					}
				}
			}
			
			if (!message.empty())
			{
				orderQueue.push_back(shared_ptr<Order>(new MessageOrder(nchatMask, MessageOrder::NORMAL_MESSAGE_TYPE, message.c_str())));
				typingInputScreen->setText("");
			}
			typingInputScreenInc=-TYPING_INPUT_BASE_INC;
			typingInputScreen->endValue=1;
			return;
		}
	}

	// the dump (debug) keys are always handled
	if (event->type == SDL_KEYDOWN)
		handleKeyDump(event->key);
	
	
	if (event->type==SDL_MOUSEBUTTONUP)
	{
		int button=event->button.button;
		if (button==SDL_BUTTON_MIDDLE)
		{
			panPushed=false;
		}
	}
	
	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		int button=event->button.button;
		if (button==SDL_BUTTON_LEFT)
		{
			// NOTE : if there is more than this, move to a func
			if ((event->button.y<34) && (event->button.x<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+16) && (event->button.x>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-32+16))
			{
				if(inGameMenu!=IGM_NONE)
				{
					delete gameMenuScreen;
					gameMenuScreen=NULL;
				}
				if(inGameMenu==IGM_MAIN)
				{
					inGameMenu=IGM_NONE;
				}
				else
				{
					gameMenuScreen=new InGameMainScreen(globalContainer->replaying);
					inGameMenu=IGM_MAIN;
				}
			}
			// NOTE : if there is more than this, move to a func
			if ((event->button.y>36) && (event->button.y<70) && (event->button.x<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+16) && (event->button.x>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-32+16))
			{
				if(inGameMenu!=IGM_NONE)
				{
					delete gameMenuScreen;
					gameMenuScreen=NULL;
				}
				if(inGameMenu==IGM_ALLIANCE)
				{
					inGameMenu=IGM_NONE;
				}
				else
				{
					gameMenuScreen=new InGameAllianceScreen(this);
					inGameMenu=IGM_ALLIANCE;
				}
			}

			// NOTE : if there is more than this, move to a func
			if ((event->button.y>72) && (event->button.y<106) && (event->button.x<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+16) && (event->button.x>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-32+16))
			{
				if(inGameMenu!=IGM_NONE)
				{
					delete gameMenuScreen;
					gameMenuScreen=NULL;
				}
				if(inGameMenu==IGM_OBJECTIVES)
				{
					inGameMenu=IGM_NONE;
				}
				else
				{
					gameMenuScreen=new InGameObjectivesScreen(this, false);
					inGameMenu=IGM_OBJECTIVES;
				}
			}
		}
	}
	
	
	// if there is a menu he get events first
	if (inGameMenu)
	{
		notmenu=true;
		processGameMenu(event);
	}
	else
	{
		notmenu=false;
		if (scrollableText)
		{
			processScrollableWidget(event);
		}
		if (event->type==SDL_KEYDOWN)
		{
			handleKey(event->key.keysym, true);
		}
		else if (event->type==SDL_KEYUP)
		{
			handleKey(event->key.keysym, false);
		}
		else if (event->type==SDL_MOUSEBUTTONDOWN)
		{
			int button=event->button.button;
			//int state=event->button.state;

			if (button==SDL_BUTTON_RIGHT)
			{
				handleRightClick();
			}
			else if (button==SDL_BUTTON_LEFT)
			{
				if (event->button.x>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)
					handleMenuClick(event->button.x-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH, event->button.y, event->button.button);
				else
					handleMapClick(event->button.x, event->button.y, event->button.button);
			}
			else if (button==SDL_BUTTON_MIDDLE)
			{
				if ((selectionMode==BUILDING_SELECTION) && (globalContainer->gfx->getW()-event->button.x<RIGHT_MENU_WIDTH))
				{
					Building* selBuild=selection.building;
					assert (selBuild);
//					selBuild->verbose=(selBuild->verbose+1)%5;
//					printf("building gid=(%d)\n", selBuild->gid);
//					if (selBuild->verbose==0)
//						printf(" verbose off\n");
//					else if (selBuild->verbose==1 || selBuild->verbose==2)
//						printf(" verbose global [%d]\n", selBuild->verbose&1);
//					else if (selBuild->verbose==3 || selBuild->verbose==4)
//						printf(" verbose local [%d]\n", selBuild->verbose&1);
//					else
//						assert(false);
//					printf(" pos=(%d, %d)\n", selBuild->posX, selBuild->posY);
//					printf(" dirtyLocalGradient=[%d, %d]\n", selBuild->dirtyLocalGradient[0], selBuild->dirtyLocalGradient[1]);
//					printf(" globalGradient=[%p, %p]\n", selBuild->globalGradient[0], selBuild->globalGradient[1]);
//					printf(" locked=[%d, %d]\n", selBuild->locked[0], selBuild->locked[1]);
					
				}
				else
				{
					// Enable panning
					panPushed=true;
					panMouseX=event->button.x;
					panMouseY=event->button.y;
					panViewX=viewportX;
					panViewY=viewportY;
				}
			}
			else if (button==4)
			{
				scrollWheelChanges += 1;

			}
			else if (button==5)
			{
				scrollWheelChanges -= 1;
			}
		}
		else if (event->type==SDL_MOUSEBUTTONUP)
		{
			int button=event->button.button;
			if ((button==SDL_BUTTON_LEFT) && (event->button.x < globalContainer->gfx->getW()-RIGHT_MENU_WIDTH))
			{
				if ((selectionMode==BUILDING_SELECTION) && selectionPushed && selection.building->type->isVirtual)
				{
					// update flag
					moveFlag(event->button.x, event->button.y, true);
				}
				// We send the order
				else if (selectionMode==BRUSH_SELECTION || selectionMode==TOOL_SELECTION)
				{
					toolManager.handleMouseUp(event->button.x, event->button.y, localTeamNo, viewportX, viewportY);
				}
			}
			miniMapPushed=false;
			selectionPushed=false;
			panPushed=false;
			// showUnitWorkingToBuilding=false;
		}
		
	}

	if (event->type==SDL_MOUSEMOTION)
	{
		handleMouseMotion(event->motion.x, event->motion.y, event->motion.state);
	}
	else if (event->type==SDL_ACTIVEEVENT)
	{
		handleActivation(event->active.state, event->active.gain);
	}
	else if (event->type==SDL_QUIT)
	{
		exitGlobCompletely=true;
		orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
		flushOutgoingAndExit=true;
	}
	else if (event->type==SDL_VIDEORESIZE)
	{
		int newW=event->resize.w;
		int newH=event->resize.h;
		newW&=(~(0x1F));
		newH&=(~(0x1F));
		if (newW<640)
			newW=640;
		if (newH<480)
			newH=480;
		printf("New size : %dx%d\n", newW, newH);
		globalContainer->gfx->setRes(newW, newH);
	}
}

void GameGUI::handleActivation(Uint8 state, Uint8 gain)
{
	if (gain==0)
	{
		viewportSpeedX=viewportSpeedY=0;
	}
}

void GameGUI::handleRightClick(void)
{
	// We cycle between views:
	if (selectionMode==NO_SELECTION)
	{
		nextDisplayMode();
	}
	// We deselect all, we want no tools activated:
	else
	{
		clearSelection();
	}
}

void GameGUI::nextDisplayMode(void)
{
	if (globalContainer->replaying)
	{
		replayDisplayMode=ReplayDisplayMode((replayDisplayMode + 1) % RDM_NB_VIEWS);
		return;
	}

	int t=0;
	do
	{
		displayMode=DisplayMode((displayMode + 1) % NB_VIEWS);
		if ((t++)==4)
		{
			displayMode=NB_VIEWS;
			break;
		}
	} while ((1<<((int)displayMode)) & hiddenGUIElements);
}

void GameGUI::repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade)
{
	BuildingType *buildingType = building->type;
	
	// building site can't be repaired nor upgraded
	if (buildingType->isBuildingSite)
		return;
	// we can upgrade or repair only building from our team
	if (building->owner->teamNumber != localTeamNo)
		return;
	int typeNum = building->typeNum + 1; //determines type of updated building
	int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
	int repairUnitWorking = defaultAssign.getDefaultAssignedUnits(building->typeNum - 1);
	int unitWorkingFuture = defaultAssign.getDefaultAssignedUnits(typeNum+1);
	if ((building->hp < buildingType->hpMax) && repair)
	{
		// repair
		if ((building->type->regenerationSpeed == 0) &&
			(building->isHardSpaceForBuildingSite(Building::REPAIR)) &&
			(localTeam->maxBuildLevel() >= buildingType->level))
			orderQueue.push_back(shared_ptr<Order>(new OrderConstruction(building->gid, repairUnitWorking, building->maxUnitWorkingLocal)));
	}
	else if (upgrade)
	{
		// upgrade
		if ((buildingType->nextLevel != -1) &&
			(building->isHardSpaceForBuildingSite(Building::UPGRADE)) &&
			(localTeam->maxBuildLevel() > buildingType->level))
			orderQueue.push_back(shared_ptr<Order>(new OrderConstruction(building->gid, unitWorking, unitWorkingFuture)));
	}
}

void GameGUI::handleKey(SDL_keysym key, bool pressed)
{

	int modifier;

	if (pressed)
		modifier=1;
	else
		modifier=-1;

	if (typingInputScreen == NULL)
	{
		if(key.sym == SDLK_SPACE && pressed && swallowSpaceKey)
		{
			setIsSpaceSet(true);
		}
		else
		{
			Uint32 action_t = keyboardManager.getAction(KeyPress(key, pressed));
			switch(action_t)
			{
				case GameGUIKeyActions::DoNothing:
				{
				}
				break;
				case GameGUIKeyActions::ShowMainMenu:
				{
					if (inGameMenu==IGM_NONE)
					{
						gameMenuScreen=new InGameMainScreen(globalContainer->replaying);
						inGameMenu=IGM_MAIN;
					}
				}
				break;
				case GameGUIKeyActions::UpgradeBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild = selection.building;
						int typeNum = selBuild->typeNum; //determines type of updated building
						int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum - 1);
						if (selBuild->constructionResultState == Building::UPGRADE)
							orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
						else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
							repairAndUpgradeBuilding(selBuild, false, true);
					}
				}
				break;
				case GameGUIKeyActions::IncreaseUnitsWorking:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
						{
							int nbReq=std::min(20, selBuild->maxUnitWorkingLocal+1);
							selBuild->maxUnitWorkingLocal = nbReq;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
							defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
				case GameGUIKeyActions::DecreaseUnitsWorking:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal>0))
						{
							int nbReq=std::max(0, selBuild->maxUnitWorkingLocal-1);
							selBuild->maxUnitWorkingLocal = nbReq;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
							defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
				case GameGUIKeyActions::OpenChatBox:
				{
					typingInputScreen=new InGameTextInput(globalContainer->gfx);
					typingInputScreenInc=TYPING_INPUT_BASE_INC;
					typingInputScreenPos=0;
				}
				break;
				case GameGUIKeyActions::IterateSelection:
				{
					iterateSelection();
				}
				break;
				case GameGUIKeyActions::GoToEvent:
				{
					eventGoTypeIterator = eventGoType;
					int evX = eventGoPosX;
					int evY = eventGoPosY;
				
					int oldViewportX = viewportX;
					int oldViewportY = viewportY;
					
					int sw = globalContainer->gfx->getW();
					int sh = globalContainer->gfx->getH();
					viewportX = evX-((sw-RIGHT_MENU_WIDTH)>>6);
					viewportY = evY-(sh>>6);
					
					moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
				}
				break;
				case GameGUIKeyActions::GoToHome:
				{
					int evX = localTeam->startPosX;
					int evY = localTeam->startPosY;
					
					int oldViewportX = viewportX;
					int oldViewportY = viewportY;
					
				    int sw = globalContainer->gfx->getW();
					int sh = globalContainer->gfx->getH();
					viewportX = evX-((sw-RIGHT_MENU_WIDTH)>>6);
					viewportY = evY-(sh>>6);
					
					moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
				}
				break;
				case GameGUIKeyActions::PauseGame:
				{
					orderQueue.push_back(shared_ptr<Order>(new PauseGameOrder(!gamePaused)));
				}
				break;
				case GameGUIKeyActions::HardPause:
				{
					hardPause=!hardPause;
				}
				break;
				case GameGUIKeyActions::ToggleDrawUnitPaths:
				{
					drawPathLines=!drawPathLines;
				}
				break;
				case GameGUIKeyActions::DestroyBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selection.building;
						if (selBuild->owner->teamNumber==localTeamNo)
						{
							if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
							{
								orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(selBuild->gid)));
							}
							else if (selBuild->buildingState==Building::ALIVE)
							{
								orderQueue.push_back(shared_ptr<Order>(new OrderDelete(selBuild->gid)));
							}
						}
					}
				}
				break;
				case GameGUIKeyActions::RepairBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild = selection.building;
						int typeNum = selBuild->typeNum; //determines type of updated building
						int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
						if (selBuild->constructionResultState == Building::REPAIR)
							orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
						else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
							repairAndUpgradeBuilding(selBuild, true, false);
					}
				}
				break;
				case GameGUIKeyActions::ToggleDrawInformation:
				{
					drawHealthFoodBar=!drawHealthFoodBar;
				}
				break;
				case GameGUIKeyActions::ToggleDrawAccessibilityAids:
				{
					drawAccessibilityAids = !drawAccessibilityAids;
				}
				break;
				case GameGUIKeyActions::MarkMap:
				{
					putMark=true;
					globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_MARK);
				}
				break;
				case GameGUIKeyActions::ToggleRecordingVoice:
				{
					if (globalContainer->voiceRecorder->recordingNow)
						globalContainer->voiceRecorder->stopRecording();
					else
						globalContainer->voiceRecorder->startRecording();
				}
				break;
				case GameGUIKeyActions::ViewHistory:
				{
					if ( ! scrollableText)
						scrollableText = messageManager.createScrollableHistoryScreen();
					else 
					{
						delete scrollableText;
						scrollableText=NULL;
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructInn:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("inn")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("inn"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructSwarm:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("swarm")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("swarm"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructHospital:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("hospital")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("hospital"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructRacetrack:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("racetrack")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("racetrack"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructSwimmingPool:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("swimmingpool")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("swimmingpool"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructBarracks:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("barracks")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("barracks"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructSchool:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("school")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("school"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructDefenceTower:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("defencetower")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("defencetower"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructStoneWall:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("stonewall")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("stonewall"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructMarket:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("market")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("market"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceExplorationFlag:
				{
					clearSelection();
					if (isFlagEnabled(std::string("explorationflag")))
					{
						displayMode = FLAG_VIEW;
						setSelection(TOOL_SELECTION, (void*)("explorationflag"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceWarFlag:
				{
					clearSelection();
					if (isFlagEnabled(std::string("warflag")))
					{
						displayMode = FLAG_VIEW;
						setSelection(TOOL_SELECTION, (void*)("warflag"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceClearingFlag:
				{
					clearSelection();
					if (isFlagEnabled(std::string("clearingflag")))
					{
						displayMode = FLAG_VIEW;
						setSelection(TOOL_SELECTION, (void*)("clearingflag"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceForbiddenArea:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
				}
				break;
				case GameGUIKeyActions::SelectPlaceGuardArea:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool(GameGUIToolManager::Guard);
				}
				break;
				case GameGUIKeyActions::SelectPlaceClearingArea:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool(GameGUIToolManager::Clearing);
				}
				break;
				case GameGUIKeyActions::SwitchToAddingAreas:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setType(BrushTool::MODE_ADD);
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToRemovingAreas:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setType(BrushTool::MODE_DEL);
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush1:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(0);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush2:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(1);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush3:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(2);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush4:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(3);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush5:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(4);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush6:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(5);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush7:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(6);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush8:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(7);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
			}
		}
	}
}

void GameGUI::handleKeyDump(SDL_KeyboardEvent key)
{
	if (key.keysym.sym == SDLK_PRINT)
	{
		if ((key.keysym.mod & KMOD_SHIFT) != 0)
		{
			OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("glob2.dump.txt"));
			if (stream->isEndOfStream())
			{
				std::cerr << "Can't dump full game memory to file glob2.dump.txt" << std::endl;
			}
			else
			{
				std::cerr << "Dump full game memory" << std::endl;
				save(stream, "glob2.dump.txt");
			}
			delete stream;
		}
		else
		{
			globalContainer->gfx->printScreen("screenshot.bmp");
		}
	}
}

void GameGUI::handleKeyAlways(void)
{
	SDL_PumpEvents();
	Uint8 *keystate = SDL_GetKeyState(NULL);
	if (notmenu == false)
	{
		SDLMod modState = SDL_GetModState();
		int xMotion = 1;
		int yMotion = 1;
		/* We check that only Control is held to avoid accidentally
			matching window manager bindings for switching windows
			and/or desktops. */
		if (!(modState & (KMOD_ALT|KMOD_SHIFT)))
		{
			/* It violates good abstraction principles that I
				have to do the calculations in the next two
				lines.  There should be methods that abstract
				these computations. */
			if ((modState & KMOD_CTRL))
			{
				/* We move by half screens if Control is held while
					the arrow keys are held.  So we shift by 6
					instead of 5.  (If we shifted by 5, it would be
					good to subtract 1 so that there would be a small
					overlap between what is viewable both before and
					after the motion.) */
				xMotion = ((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
				yMotion = ((globalContainer->gfx->getH())>>6);
			}
			else
			{
				/* We move the screen by one square at a time if CTRL key
					is not being help */
				xMotion = 1;
				yMotion = 1;
			}
		}
		else if (modState)
		{
			/* Probably some keys held down as part of window
				manager operations. */
			xMotion = 0;
			yMotion = 0; 
		}
		
		if (keystate[SDLK_UP])
			viewportY -= yMotion;
		if (keystate[SDLK_KP8])
			viewportY -= yMotion;
		if (keystate[SDLK_DOWN])
			viewportY += yMotion;
		if (keystate[SDLK_KP2])
			viewportY += yMotion;
		if ((keystate[SDLK_LEFT]) && (typingInputScreen == NULL)) // we haave a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			viewportX -= xMotion;
		if (keystate[SDLK_KP4])
			viewportX -= xMotion;
		if ((keystate[SDLK_RIGHT]) && (typingInputScreen == NULL)) // we haave a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			viewportX += xMotion;
		if (keystate[SDLK_KP6])
			viewportX += xMotion;
		if (keystate[SDLK_KP7])
		{
			viewportX -= xMotion;
			viewportY -= yMotion;
		}
		if (keystate[SDLK_KP9])
		{
			viewportX += xMotion;
			viewportY -= yMotion;
		}
		if (keystate[SDLK_KP1])
		{
			viewportX -= xMotion;
			viewportY += yMotion;
		}
		if (keystate[SDLK_KP3])
		{
			viewportX += xMotion;
			viewportY += yMotion;
		}
	}
}

void GameGUI::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	minimap.convertToMap(mx, my, *cx, *cy);

	///when for the screen viewport, center
	if (forScreenViewport)
	{
		*cx-=((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}

}

void GameGUI::handleMouseMotion(int mx, int my, int button)
{
	const int scrollZoneWidth = 10;
	game.mouseX=mouseX=mx;
	game.mouseY=mouseY=my;

	int oldViewportX = viewportX;
	int oldViewportY = viewportY;
	
	if (miniMapPushed)
	{
		minimapMouseToPos(mx, my, &viewportX, &viewportY, true);
	}
	else
	{
		if (mx<scrollZoneWidth)
			viewportSpeedX=-1;
		else if ((mx>globalContainer->gfx->getW()-scrollZoneWidth) )
			viewportSpeedX=1;
		else
			viewportSpeedX=0;

		if (my<scrollZoneWidth)
			viewportSpeedY=-1;
		else if (my>globalContainer->gfx->getH()-scrollZoneWidth)
			viewportSpeedY=1;
		else
			viewportSpeedY=0;
	}
	
	if (panPushed)
	{
		// handle paning
		int dx = (mx-panMouseX)>>1;
		int dy = (my-panMouseY)>>1;
		viewportX = (panViewX+dx)&game.map.getMaskW();
		viewportY = (panViewY+dy)&game.map.getMaskH();
	}
	
	moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);

	dragStep(mx, my, button);
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
	if (selectionMode==TOOL_SELECTION)
	{
		toolManager.handleMouseDown(mx, my, localTeamNo, viewportX, viewportY);
		
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		toolManager.handleMouseDown(mx, my, localTeamNo, viewportX, viewportY);
	}
	else if (putMark)
	{
		int markx, marky;
		game.map.displayToMapCaseAligned(mx, my, &markx, &marky, viewportX, viewportY);
		orderQueue.push_back(shared_ptr<Order>(new MapMarkOrder(localTeamNo, markx, marky)));
		globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
		putMark = false;
	}
	else
	{
		int mapX, mapY;
		game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
		selectionPushedPosX=mapX;
		selectionPushedPosY=mapY;
		// check for flag first
		for (std::list<Building *>::iterator virtualIt=localTeam->virtualBuildings.begin();
				virtualIt!=localTeam->virtualBuildings.end(); ++virtualIt)
			{
				Building *b=*virtualIt;
				if ((b->posXLocal==mapX) && (b->posYLocal==mapY))
				{
					setSelection(BUILDING_SELECTION, b);
					selectionPushed=true;
					return;
				}
			}
		// then for unit
		if (game.mouseUnit)
		{
			// a unit is selected:
			setSelection(UNIT_SELECTION, game.mouseUnit);
			selectionPushed = true;
			// handle dump of unit characteristics
			if ((SDL_GetModState() & KMOD_SHIFT) != 0)
			{
				OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("unit.dump.txt"));
				if (stream->isEndOfStream())
				{
					std::cerr << "Can't dump unit to file unit.dump.txt" << std::endl;
				}
				else
				{
					std::cerr << "Dump unit " << game.mouseUnit->gid << " memory" << std::endl;
					game.mouseUnit->save(stream);
					game.mouseUnit->saveCrossRef(stream);
					if (game.mouseUnit->attachedBuilding)
					{
						game.mouseUnit->attachedBuilding->save(stream);
						game.mouseUnit->attachedBuilding->saveCrossRef(stream);
					}
				}
				delete stream;
			}
		}
		else 
		{
			// then for building
			Uint16 gbid=game.map.getBuilding(mapX, mapY);
			if (gbid != NOGBID)
			{
				int buildingTeam=Building::GIDtoTeam(gbid);
				// we can select for view buildings that are in shared vision
				if ((buildingTeam==localTeamNo)
					|| game.map.isFOWDiscovered(mapX, mapY, localTeam->me)
					|| (game.map.isMapDiscovered(mapX, mapY, localTeam->me)
						&& (game.teams[buildingTeam]->allies&(1<<localTeamNo))))
				{
					setSelection(BUILDING_SELECTION, gbid);
					selectionPushed=true;
					// showUnitWorkingToBuilding=true;
					// handle dump of building characteristics
					if ((SDL_GetModState() & KMOD_SHIFT) != 0)
					{
						OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("building.dump.txt"));
						if (stream->isEndOfStream())
						{
							std::cerr << "Can't dump unit to file building.dump.txt" << std::endl;
						}
						else
						{
							std::cerr << "Dump building " << selection.building->gid << " memory" << std::endl;
							selection.building->save(stream);
							selection.building->saveCrossRef(stream);
						}
						delete stream;
					}
				}
			}
			else
			{
				// and ressource
				if (game.map.isRessource(mapX, mapY) && game.map.isMapDiscovered(mapX, mapY, localTeam->me))
				{
					setSelection(RESSOURCE_SELECTION, mapY*game.map.getW()+mapX);
					selectionPushed=true;
				}
				else
				{
					if (selectionMode == RESSOURCE_SELECTION)
						clearSelection();
				}
			}
		}
	}
}

void GameGUI::handleMenuClick(int mx, int my, int button)
{
	// handle minimap
	if (my<128 && mx > (RIGHT_MENU_OFFSET) && mx < RIGHT_MENU_WIDTH - RIGHT_MENU_OFFSET)
	{
		if (putMark)
		{
			int markx, marky;
			minimapMouseToPos(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + mx, my, &markx, &marky, false);
			orderQueue.push_back(shared_ptr<Order>(new MapMarkOrder(localTeamNo, markx, marky)));
			globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
			putMark = false;
		}
		else
		{
			miniMapPushed=true;
			int oldViewportX = viewportX;
			int oldViewportY = viewportY;
			minimapMouseToPos(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + mx, my, &viewportX, &viewportY, true);
			moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
		}
	}
	// Check if one of the panel buttons has been clicked
	else if (my<YPOS_BASE_DEFAULT)
	{
		if (!globalContainer->replaying)
		{
			int dec = (RIGHT_MENU_WIDTH-128)/2;
			int dm=(mx-dec)/32;
			if (!((1<<dm) & hiddenGUIElements))
			{
				displayMode=DisplayMode(dm);
				clearSelection();
			}
		}
		else
		{
			int dec = (RIGHT_MENU_WIDTH-96)/2;
			int dm=(mx-dec)/32;
			
			replayDisplayMode=ReplayDisplayMode(dm);
			clearSelection();
		}
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		assert (selBuild);
		if (selBuild->owner->teamNumber!=localTeamNo)
			return;
		int ypos = YPOS_BASE_BUILDING +  YOFFSET_NAME + YOFFSET_ICON + YOFFSET_B_SEP;
		BuildingType *buildingType = selBuild->type;
		int lmx = mx - RIGHT_MENU_OFFSET; // local mx
	
		// working bar
		if (selBuild->type->maxUnitWorking)
		{
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& my>ypos+YOFFSET_TEXT_BAR
				&& my<ypos+YOFFSET_TEXT_BAR+16
				&& selBuild->buildingState==Building::ALIVE
				&& lmx < 128)
			{
				int nbReq;
				if (lmx<18)
				{
					if(selBuild->maxUnitWorkingLocal>0)
					{
						nbReq=(selBuild->maxUnitWorkingLocal-=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
				        defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
					}
				}
				else if (lmx<(128-18))
				{
					nbReq=selBuild->maxUnitWorkingLocal=((lmx-18)*MAX_UNIT_WORKING)/(128-36);
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
		        	defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
				}
				else
				{
					if(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)
					{
						nbReq=(selBuild->maxUnitWorkingLocal+=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
			        	defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
					}
				}
			}
			ypos += YOFFSET_BAR + YOFFSET_B_SEP;
		}
		
		// priorities
		if(selBuild->type->maxUnitWorking)
		{
			ypos += YOFFSET_B_SEP;
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& my>ypos+16
				&& my<ypos+16+12
				&& selBuild->buildingState==Building::ALIVE)
			{
				int width = (128 - 8)/3;

				if(lmx>=0 && lmx<=12)
				{
					orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, -1)));
					selBuild->priorityLocal = -1;
				}
				else if(lmx>=(width) && lmx<(width+12))
				{
					orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 0)));
					selBuild->priorityLocal = 0;
				}
				else if(lmx>=(width*2) && lmx<=(width*2+12))
				{
					orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 1)));
					selBuild->priorityLocal = 1;
				}
			}
			ypos += YOFFSET_BAR+YOFFSET_B_SEP;
		}
		
		// flag range bar
		if (buildingType->defaultUnitStayRange)
		{
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& (my>ypos+YOFFSET_TEXT_BAR)
				&& (my<ypos+YOFFSET_TEXT_BAR+16)
				&& (lmx < 128))
			{
				int nbReq;
				if (lmx<18)
				{
					if(selBuild->unitStayRangeLocal>0)
					{
						nbReq=(selBuild->unitStayRangeLocal-=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
					}
				}
				else if (lmx<RIGHT_MENU_WIDTH-18)
				{
					nbReq=selBuild->unitStayRangeLocal=((lmx-18)*(unsigned)selBuild->type->maxUnitStayRange)/(128-36);
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
				}
				else
				{
					// TODO : check in orderQueue to avoid useless orders.
					if (selBuild->unitStayRangeLocal < selBuild->type->maxUnitStayRange)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
					}
				}
			}
			ypos += YOFFSET_BAR+YOFFSET_B_SEP;
		}
		
		// flags specific options:
		if (((selBuild->owner->allies)&(1<<localTeamNo))
			&& lmx>10
			&& lmx<22)
		{
			
			// cleared ressources for clearing flags:
			if (buildingType->type == "clearingflag")
			{
				ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
				int j=0;
				for (int i=0; i<BASIC_COUNT; i++)
					if (i!=STONE)
					{
						if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
						{
							selBuild->clearingRessourcesLocal[i]=!selBuild->clearingRessourcesLocal[i];
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyClearingFlag(selBuild->gid, selBuild->clearingRessourcesLocal)));
						}
						
						ypos+=YOFFSET_TEXT_PARA;
						j++;
					}
			}
			
			if (buildingType->type == "warflag")
			{
				ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
				for (int i=0; i<4; i++)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->minLevelToFlagLocal=i;
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal)));
					}
					
					ypos+=YOFFSET_TEXT_PARA;
				}
			
			}
				
			if (buildingType->type == "explorationflag")
			{
				// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
				// must be able to do to be accepted at this flag
				// 0 == any explorer
				// 1 == must be able to attack ground
				ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
				for (int i=0; i<2; i++)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->minLevelToFlagLocal=i;
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal)));
					}
					
					ypos+=YOFFSET_TEXT_PARA;
				}
			}
		}

		if (buildingType->armor)
			ypos+=YOFFSET_TEXT_LINE;
		if (buildingType->maxUnitInside)
			ypos += YOFFSET_INFOS;
		if (buildingType->shootDamage)
			ypos += YOFFSET_TOWER;
		ypos += YOFFSET_B_SEP;

		//Exchannge building
		//Exchanging as a feature is broken
		/*
		if (selBuild->type->canExchange && ((selBuild->owner->allies)&(1<<localTeamNo)))
		{
			int startY = ypos+YOFFSET_TEXT_PARA;
			int endY = startY+HAPPYNESS_COUNT*YOFFSET_TEXT_PARA;
			if ((my>startY) && (my<endY))
			{
				int r = (my-startY)/YOFFSET_TEXT_PARA;
				if ((lmx>92) && (lmx<104))
				{
					if (selBuild->receiveRessourceMask & (1<<r))
					{
						selBuild->receiveRessourceMaskLocal &= ~(1<<r);
					}
					else
					{
						selBuild->receiveRessourceMaskLocal |= (1<<r);
						selBuild->sendRessourceMaskLocal &= ~(1<<r);
					}
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal)));
				}

				if ((lmx>110) && (lmx<122))
				{
					if (selBuild->sendRessourceMask & (1<<r))
					{
						selBuild->sendRessourceMaskLocal &= ~(1<<r);
					}
					else
					{
						selBuild->receiveRessourceMaskLocal &= ~(1<<r);
						selBuild->sendRessourceMaskLocal |= (1<<r);
					}
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal)));
				}
			}
		}
		*/
		// ressources in
		unsigned j = 0;
		for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
		{
			if (buildingType->maxRessource[i])
			{
				j++;
				ypos += 11;
			}
		}
		if (buildingType->maxBullets)
		{
			j++;
			ypos += 11;
		}
		ypos+=5;

		if (selBuild->type->unitProductionTime)
		{
			ypos+=15;
			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				if ((my>ypos+(i*20))&&(my<ypos+(i*20)+16)&&(lmx<128))
				{
					if (lmx<18)
					{
						if (selBuild->ratioLocal[i]>0)
						{
							selBuild->ratioLocal[i]--;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
						}
					}
					else if (lmx<(128-18))
					{
						selBuild->ratioLocal[i]=((lmx-18)*MAX_RATIO_RANGE)/(128-36);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
					}
					else
					{
						if (selBuild->ratioLocal[i]<MAX_RATIO_RANGE)
						{
							selBuild->ratioLocal[i]++;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
						}
					}
					//printf("ratioLocal[%d]=%d\n", i, selBuild->ratioLocal[i]);
				}
			}
		}

		if ((my>globalContainer->gfx->getH()-48) && (my<globalContainer->gfx->getH()-32))
		{
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
				orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum - 1);
				orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
			{
				repairAndUpgradeBuilding(selBuild, true, true);
			}
		}

		if ((my>globalContainer->gfx->getH()-24) && (my<globalContainer->gfx->getH()-8))
		{
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(selBuild->gid)));
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderDelete(selBuild->gid)));
			}
		}
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		Unit* selUnit=selection.unit;
		assert(selUnit);
		selUnit->verbose=!selUnit->verbose;
		printf("unit gid=(%d) verbose %d\n", selUnit->gid, selUnit->verbose);
		printf(" pos=(%d, %d)\n", selUnit->posX, selUnit->posY);
		printf(" needToRecheckMedical=%d\n", selUnit->needToRecheckMedical);
		printf(" medical=%d\n", selUnit->medical);
		printf(" activity=%d\n", selUnit->activity);
		printf(" displacement=%d\n", selUnit->displacement);
		printf(" movement=%d\n", selUnit->movement);
		printf(" action=%d\n", selUnit->action);

		if (selUnit->attachedBuilding)
			printf(" attachedBuilding bgid=%d\n", selUnit->attachedBuilding->gid);
		else
			printf(" attachedBuilding NULL\n");
		printf(" destinationPurprose=%d\n", selUnit->destinationPurprose);
		printf(" caryedRessource=%d\n", selUnit->caryedRessource);
	}
	else if ((displayMode==CONSTRUCTION_VIEW && !globalContainer->replaying))
	{
		int xNum=mx/(RIGHT_MENU_WIDTH/2);
		int yNum=(my-YPOS_BASE_CONSTRUCTION)/46;
		int id=yNum*2+xNum;
		if (id<(int)buildingsChoiceName.size())
			if (buildingsChoiceState[id])
				setSelection(TOOL_SELECTION, (void *)buildingsChoiceName[id].c_str());
	}
	else if ((displayMode==FLAG_VIEW && !globalContainer->replaying))
	{
		int dec = (RIGHT_MENU_WIDTH - 128)/2;
		my -= YPOS_BASE_FLAG;
		int nmx = mx - dec;
		if (my > YOFFSET_BRUSH)
		{
			// change the brush type (forbidden, guard, clear) if necessary
			if (my < YOFFSET_BRUSH+40)
			{
				if (nmx < 44)
					toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
				else if (nmx < 84)
					toolManager.activateZoneTool(GameGUIToolManager::Guard);
				else if(nmx < 124)
					toolManager.activateZoneTool(GameGUIToolManager::Clearing);
			}
			// anyway, update the tool
			brush.handleClick(mx-dec, my-YOFFSET_BRUSH-40);
			// set the selection
			setSelection(BRUSH_SELECTION);
			toolManager.activateZoneTool();
		}
		else
		{
			int xNum=mx / (RIGHT_MENU_WIDTH/3);
			int yNum=my / 46;
			int id=yNum*3+xNum;
			if (id<(int)flagsChoiceName.size())
				if (flagsChoiceState[id])
					setSelection(TOOL_SELECTION, (void*)flagsChoiceName[id].c_str());
		}
	}
	else if ((displayMode==STAT_GRAPH_VIEW && !globalContainer->replaying) || (replayDisplayMode==RDM_STAT_GRAPH_VIEW && globalContainer->replaying))
	{
		if(mx > 8 && mx < 24)
		{
			// In replays, this menu bar is 15 pixels lower than usual to show "Watching: player-name"
			int inc;

			if (globalContainer->replaying) inc = 15;
			else inc = 0;

			if(my > YPOS_BASE_STAT+140+inc+64 && my < YPOS_BASE_STAT+140+inc+80)
			{
				showDamagedMap=false;
				showDefenseMap=false;
				showFertilityMap=false;
				showStarvingMap=!showStarvingMap;
				overlay.compute(game, OverlayArea::Starving, localTeamNo);
			}

			if(my > YPOS_BASE_STAT+140+inc+88 && my < YPOS_BASE_STAT+140+inc+104)
			{
				showDamagedMap=!showDamagedMap;
				showStarvingMap=false;
				showDefenseMap=false;
				showFertilityMap=false;
				overlay.compute(game, OverlayArea::Damage, localTeamNo);
			}

			if(my > YPOS_BASE_STAT+140+inc+112 && my < YPOS_BASE_STAT+140+inc+128)
			{
				showDefenseMap=!showDefenseMap;
				showStarvingMap=false;
				showDamagedMap=false;
				showFertilityMap=false;
				overlay.compute(game, OverlayArea::Defence, localTeamNo);
			}

			if(my > YPOS_BASE_STAT+140+inc+136 && my < YPOS_BASE_STAT+140+inc+152)
			{
				showFertilityMap=!showFertilityMap;
				showDefenseMap=false;
				showStarvingMap=false;
				showDamagedMap=false;
				overlay.compute(game, OverlayArea::Fertility, localTeamNo);
			}
		}
	}
	else if (replayDisplayMode==RDM_REPLAY_VIEW && globalContainer->replaying)
	{
		int x = REPLAY_PANEL_XOFFSET;
		int y = REPLAY_PANEL_YOFFSET;
		int inc = REPLAY_PANEL_SPACE_BETWEEN_OPTIONS;

		if (mx > x && mx < x+20 && my > y+1*inc && my < y+1*inc + 20)
		{
			// Disable/show fog of war
			globalContainer->replayShowFog = !globalContainer->replayShowFog;

			if (globalContainer->replayShowFog) minimap.setMinimapMode( Minimap::ShowFOW );
			else minimap.setMinimapMode( Minimap::HideFOW );
		}
		if (mx > x && mx < x+20 && my > y+2*inc && my < y+2*inc + 20)
		{
			// Disable/enable combined vision
			if (globalContainer->replayVisibleTeams == 0xFFFFFFFF)
			{
				globalContainer->replayVisibleTeams = localTeam->me;
			}
			else
			{
				globalContainer->replayVisibleTeams = 0xFFFFFFFF;
			}
		}
		if (mx > x && mx < x+20 && my > y+3*inc && my < y+3*inc + 20)
		{
			// Show/hide player's areas
			globalContainer->replayShowAreas = !globalContainer->replayShowAreas;
		}
		if (mx > x && mx < x+20 && my > y+4*inc && my < y+4*inc + 20)
		{
			// Show/hide flags
			globalContainer->replayShowFlags = !globalContainer->replayShowFlags;
		}

		for (int i = 0; i < game.teamsCount(); i++)
		{
			if (mx > x && mx < x+20 && my > y+REPLAY_PANEL_PLAYERLIST_YOFFSET+(i+1)*inc && my < y+REPLAY_PANEL_PLAYERLIST_YOFFSET+(i+1)*inc + 20)
			{
				localTeamNo = i;
				localTeam = game.teams[i];

				// Update localPlayer to the first player of this team
				for (int j=0; j<game.gameHeader.getNumberOfPlayers(); j++)
				{
					if (game.players[j]->teamNumber == localTeamNo)
					{
						localPlayer = j;
						break;
					}
				}

				// Update the visible players unless all players are visible
				if (globalContainer->replayVisibleTeams != 0xFFFFFFFF)
				{
					globalContainer->replayVisibleTeams = localTeam->me;
				}
			}
		}
	}
}

boost::shared_ptr<Order> GameGUI::getOrder(void)
{
	boost::shared_ptr<Order> order;
	if (orderQueue.size()==0)
		order=shared_ptr<Order>(new NullOrder());
	else
	{
		order=orderQueue.front();
		orderQueue.pop_front();
	}
	return order;
}

void GameGUI::drawParticles(void)
{
	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); )
	{
		Particle* p = *it;
		
		// delete old particles
		if (p->age >= p->lifeSpan)
		{
			ParticleSet::iterator oldIt = it;
			++it;
			
			delete *oldIt;
			particles.erase(oldIt);
			
			continue;
		}
		else
			p->age++;
		
		// do stupid physics
		p->x += p->vx;
		p->y += p->vy;
		p->vx += p->ax;
		p->vy += p->ay;
		
		// get image
		float img = (float)p->startImg + (float)((p->endImg - p->startImg) * p->age) / ((float)p->lifeSpan + 1);
		Uint8 alpha = (Uint8)(255.f * (img - truncf(img)));
		int imgA = (int)img;
		
		globalContainer->particles->setBaseColor(p->color);
		
		// first image
		int w = globalContainer->particles->getW(imgA);
		int h = globalContainer->particles->getH(imgA);
		globalContainer->gfx->drawSprite(p->x - 0.5f * w, p->y - 0.5f * h, globalContainer->particles, imgA, 255-alpha);
		
		// second image
		int imgB = imgA + 1;
		if (imgB < p->endImg)
		{
			w = globalContainer->particles->getW(imgA);
			h = globalContainer->particles->getH(imgA);
			globalContainer->gfx->drawSprite(p->x - 0.5f * w, p->y - 0.5f * h, globalContainer->particles, imgB, alpha);
		}
		
		++it;
	}
}

void GameGUI::drawPanelButtons(int y)
{
	if (!globalContainer->replaying)
	{
		int numButtons = 4;

		if (!(hiddenGUIElements & HIDABLE_BUILDINGS_LIST))
		{
			if (((selectionMode==NO_SELECTION) || (selectionMode==TOOL_SELECTION)) && (displayMode==CONSTRUCTION_VIEW))
				drawPanelButton(y, 0, numButtons, 1);
			else
				drawPanelButton(y, 0, numButtons, 0);
		}

		if (!(hiddenGUIElements & HIDABLE_FLAGS_LIST))
		{
			if (((selectionMode==NO_SELECTION) || (selectionMode==TOOL_SELECTION) || (selectionMode==BRUSH_SELECTION)) && (displayMode==FLAG_VIEW))
				drawPanelButton(y, 1, numButtons, 29);
			else
				drawPanelButton(y, 1, numButtons, 28);
		}

		if (!(hiddenGUIElements & HIDABLE_TEXT_STAT))
		{
			if ((selectionMode==NO_SELECTION) && (displayMode==STAT_TEXT_VIEW))
				drawPanelButton(y, 2, numButtons, 3);
			else
				drawPanelButton(y, 2, numButtons, 2);
		}

		if (!(hiddenGUIElements & HIDABLE_GFX_STAT))
		{
			if ((selectionMode==NO_SELECTION) && (displayMode==STAT_GRAPH_VIEW))
				drawPanelButton(y, 3, numButtons, 5);
			else
				drawPanelButton(y, 3, numButtons, 4);
		}
	}
	else
	{
		int numButtons = 3;

		if (replayDisplayMode==RDM_REPLAY_VIEW)
			drawPanelButton(y, 0, numButtons, 1);
		else
			drawPanelButton(y, 0, numButtons, 0);

		if (replayDisplayMode==RDM_STAT_TEXT_VIEW)
			drawPanelButton(y, 1, numButtons, 3);
		else
			drawPanelButton(y, 1, numButtons, 2);

		if (replayDisplayMode==RDM_STAT_GRAPH_VIEW)
			drawPanelButton(y, 2, numButtons, 5);
		else
			drawPanelButton(y, 2, numButtons, 4);
	}

	if(hilights.find(HilightUnderMinimapIcon) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, y, 38));
	}
}

void GameGUI::drawPanelButton(int y, int pos, int numButtons, int sprite)
{
	int dec = (RIGHT_MENU_WIDTH - numButtons*32)/2;

	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + dec + pos*32, y, globalContainer->gamegui, sprite);
}

void GameGUI::drawChoice(int pos, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine)
{
	assert(numberPerLine >= 2);
	assert(numberPerLine <= 3);
	int sel=-1;
	int width = (RIGHT_MENU_WIDTH/numberPerLine);
	size_t i;

	for (i=0; i<types.size(); i++)
	{
		std::string &type = types[i];

		if ((selectionMode==TOOL_SELECTION) && (type == toolManager.getBuildingName()))
			sel = i;

		if (states[i])
		{
			BuildingType *bt = globalContainer->buildingsTypes.getByType(type.c_str(), 0, false);
			assert(bt);
			int imgid = bt->miniSpriteImage;
			int x, y;

			x=((i % numberPerLine)*width)+globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
			y=((i / numberPerLine)*46)+YPOS_BASE_BUILDING;
			globalContainer->gfx->setClipRect(x, y, 64, 46);

			Sprite *buildingSprite;
			if (imgid >= 0)
			{
				buildingSprite = bt->miniSpritePtr;
			}
			else
			{
				buildingSprite = bt->gameSpritePtr;
				imgid = bt->gameSpriteImage;
			}
			
			int decX = (width-buildingSprite->getW(imgid))>>1;
			int decY = (46-buildingSprite->getW(imgid))>>1;

			buildingSprite->setBaseColor(localTeam->color);
			globalContainer->gfx->drawSprite(x+decX, y+decY, buildingSprite, imgid);
			
			globalContainer->gfx->setClipRect();
			if(hilights.find(HilightBuildingOnPanel+IntBuildingType::shortNumberFromType(type)) != hilights.end())
			{
				arrowPositions.push_back(HilightArrowPosition(x+decX-36, y-6+decX, 38));
			}
		}
	}
	int count = i;

	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 128, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128);

	// draw building selection if needed
	if (selectionMode == TOOL_SELECTION)
	{
		int sw;
		if (numberPerLine == 2)
			sw = globalContainer->gamegui->getW(8);
		else
			sw = globalContainer->gamegui->getW(23);
			
	  
		assert(sel>=0);
		int x=((sel  % numberPerLine)*width)+globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
		int y=((sel / numberPerLine)*46)+YPOS_BASE_BUILDING;
		
		int decX = (width - sw) / 2;
		
		if (numberPerLine == 2)
			globalContainer->gfx->drawSprite(x+decX, y+1, globalContainer->gamegui, 8);
		else
			globalContainer->gfx->drawSprite(x+decX, y+4, globalContainer->gamegui, 23);
	}
	
	int toDrawInfoFor = -1;
	if (mouseX>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)
	{
		if (mouseY>pos)
		{
			int xNum=(mouseX-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH) / width;
			int yNum=(mouseY-pos)/46;
			int id=yNum*numberPerLine+xNum;
			if (id<count)
			{
				toDrawInfoFor = id;
			}
		}
	}
	if(toDrawInfoFor == -1)
	{
		if(toolManager.getBuildingName() != "")
		{
			std::vector<std::string>::iterator i = std::find(types.begin(), types.end(), toolManager.getBuildingName());
			if(i != types.end())
			{
				toDrawInfoFor = i - types.begin();	
			}
		}
	}

	// draw infos
	if (toDrawInfoFor != -1)
	{
		int id = toDrawInfoFor;

		std::string &type = types[id];
		if (states[id])
		{
			int buildingInfoStart=globalContainer->gfx->getH()-50;

			std::string key = "[" + type + "]";
			drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, key.c_str());
			
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 128, 128, 128));
			key = "[" + type + " explanation]";
			drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-20, key.c_str());
			key = "[" + type + " explanation 2]";
			drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-8, key.c_str());
			globalContainer->littleFont->popStyle();
			BuildingType *bt = globalContainer->buildingsTypes.getByType(type, 0, true);
			if (bt)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+6, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Wood]")).arg(bt->maxRessource[0]).c_str());
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+17, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Stone]")).arg(bt->maxRessource[3]).c_str());

				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+64+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+6, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Alga]")).arg(bt->maxRessource[4]).c_str());
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+64+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+17, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Corn]")).arg(bt->maxRessource[1]).c_str());

				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+28, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Papyrus]")).arg(bt->maxRessource[2]).c_str());
			}
		}
	}
}


void GameGUI::drawUnitInfos(void)
{
	Unit* selUnit=selection.unit;
	assert(selUnit);
	int ypos = YPOS_BASE_UNIT;
	Uint8 r, g, b;

	// draw "unit" of "player"
	std::string title;
	title += getUnitName(selUnit->typeNum);
	title += " (";

	std::string textT=selUnit->owner->getFirstPlayerName();
	if (textT.empty())
		textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
	title += textT;
	title += ")";

	if (localTeam->teamNumber == selUnit->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selUnit->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos+5, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_NAME;

	// draw unit's image
	Unit* unit=selUnit;
	int imgid;
	UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	if (dir==8)
	{
		imgid+=8*(delta>>5);
	}
	else
	{
		imgid+=8*dir;
		imgid+=(delta>>5);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(unit->owner->color);
	int decX = (32-unitSprite->getW(imgid))>>1;
	int decY = (32-unitSprite->getH(imgid))>>1;
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+12+decX, ypos+7+4+decY, unitSprite, imgid);
	
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[hp]")).c_str());

	if (selUnit->hp<=selUnit->trigHP)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selUnit->hp).arg(selUnit->performance[HP]).c_str());
	globalContainer->littleFont->popStyle();

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[food]")).c_str());

	// draw food
	if (selUnit->isUnitHungry())
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+2*YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0 % (%1)").arg(((float)selUnit->hungry*100.0f)/(float)Unit::HUNGRY_MAX, 0, 0).arg(selUnit->fruitCount).c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_ICON+10;

	int rdec = (RIGHT_MENU_WIDTH-128)/2;

	if (selUnit->performance[HARVEST])
	{
		if (selUnit->caryedRessource>=0)
		{
			const RessourceType* r = globalContainer->ressourcesTypes.get(selUnit->caryedRessource);
			unsigned resImg = r->gfxId + r->sizesCount - 1;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[carry]"));
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32-8-rdec, ypos, globalContainer->ressources, resImg);
		}
		else
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[don't carry anything]"));
		}
	}
	ypos += YOFFSET_CARYING+10;

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[current speed]")).arg(selUnit->speed).c_str());
	ypos += YOFFSET_TEXT_PARA+10;
	
	if (selUnit->performance[ARMOR])
	{
		int armorReductionPerHappyness = selUnit->race->getUnitType(selUnit->typeNum, selUnit->level[ARMOR])->armorReductionPerHappyness;
		int realArmor = selUnit->performance[ARMOR] - selUnit->fruitCount * armorReductionPerHappyness;
		if (realArmor < 0)
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1 = %2 - %3 * %4").arg(Toolkit::getStringTable()->getString("[armor]")).arg(realArmor).arg(selUnit->performance[ARMOR]).arg(selUnit->fruitCount).arg(armorReductionPerHappyness).c_str());
		if (realArmor < 0)
			globalContainer->littleFont->popStyle();
	}
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->typeNum!=EXPLORER)
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[levels]")).c_str());
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->performance[WALK])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Walk]")).arg((1+selUnit->level[WALK])).arg(selUnit->performance[WALK]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[SWIM])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Swim]")).arg(selUnit->level[SWIM]).arg(selUnit->performance[SWIM]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[BUILD])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Build]")).arg(1+selUnit->level[BUILD]).arg(selUnit->performance[BUILD]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[HARVEST])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Harvest]")).arg(1+selUnit->level[HARVEST]).arg(selUnit->performance[HARVEST]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_SPEED])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[At. speed]")).arg(1+selUnit->level[ATTACK_SPEED]).arg(selUnit->performance[ATTACK_SPEED]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_STRENGTH])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[At. strength]")).arg(1+selUnit->level[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).arg(selUnit->performance[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).c_str());
		
		ypos += YOFFSET_TEXT_PARA + 2;
	}
	
	if (selUnit->performance[MAGIC_ATTACK_AIR])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Air]")).arg(1+selUnit->level[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).c_str());
		
		ypos += YOFFSET_TEXT_PARA + 2;
	}
	
	if (selUnit->performance[MAGIC_ATTACK_GROUND])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Ground]")).arg(1+selUnit->level[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).c_str());
		
		ypos += YOFFSET_TEXT_PARA + 2;
	}
	
	if (selUnit->performance[ATTACK_STRENGTH] || selUnit->performance[MAGIC_ATTACK_AIR] || selUnit->performance[MAGIC_ATTACK_GROUND])
		drawXPProgressBar(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selUnit->experience, selUnit->getNextLevelThreshold());
}

void GameGUI::drawValueAlignedRight(int y, int v)
{
	FormatableString s("%0");
	s.arg(v);
	int len = globalContainer->littleFont->getStringWidth(s.c_str());
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-len-2, y, globalContainer->littleFont, s.c_str());
}

void GameGUI::drawCosts(int ressources[BASIC_COUNT], Font *font)
{
	for (int i=0; i<BASIC_COUNT; i++)
	{
		int y = i>>1;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(i&0x1)*64, 256+172-42+y*12,
			font,
			FormatableString("%0: %1").arg(getRessourceName(i)).arg(ressources[i]).c_str());
	}
}

void GameGUI::drawCheckButton(int x, int y, const char* caption, bool isSet)
{
	globalContainer->gfx->drawRect(x, y, 16, 16, Color::white);
	if(isSet)
	{
		globalContainer->gfx->drawLine(x+4, y+4, x+12, y+12, Color::white);
		globalContainer->gfx->drawLine(x+12, y+4, x+4, y+12, Color::white);
	}
	globalContainer->gfx->drawString(x+20, y, globalContainer->littleFont, caption); 
}


void GameGUI::drawRadioButton(int x, int y, bool isSet)
{
	if(isSet)
	{
		globalContainer->gfx->drawSprite(x, y, globalContainer->gamegui, 20);
	}
	else
	{
		globalContainer->gfx->drawSprite(x, y, globalContainer->gamegui, 19);
	}
}

void GameGUI::drawBuildingInfos(void)
{
	Building* selBuild = selection.building;
	assert(selBuild);
	BuildingType *buildingType = selBuild->type;
	int ypos = YPOS_BASE_BUILDING;
	Uint8 r, g, b;
	unsigned unitInsideBarYDec = 0;

	// draw "building" of "player"
	std::string title;
	std::string key ="[" + buildingType->type + "]";
	title += Toolkit::getStringTable()->getString(key.c_str());
	{
		title += " (";
		std::string textT=selBuild->owner->getFirstPlayerName();
		if (textT.empty())
			textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
		title += textT;
		title += ")";
	}

	if (localTeam->teamNumber == selBuild->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selBuild->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	// building text
	title = "";
	if ((buildingType->nextLevel>=0) ||  (buildingType->prevLevel>=0))
	{
		const char *textT = Toolkit::getStringTable()->getString("[level]");
		title += FormatableString("%0 %1").arg(textT).arg(buildingType->level+1);
	}
	if (buildingType->isBuildingSite)
	{
		title += " (";
		title += Toolkit::getStringTable()->getString("[building site]");
		title += ")";
	}
	if (buildingType->prestige)
	{
		title += " - ";
		title += Toolkit::getStringTable()->getString("[Prestige]");
	}
	titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 200));
	globalContainer->gfx->drawString(titlePos, ypos+YOFFSET_TEXT_PARA-1, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_NAME;


	// building icon
	Sprite *miniSprite;
	int imgid;
	if (buildingType->miniSpriteImage >= 0)
	{
		miniSprite = buildingType->miniSpritePtr;
		imgid = buildingType->miniSpriteImage;
	}
	else
	{
		miniSprite = buildingType->gameSpritePtr;
		imgid = buildingType->gameSpriteImage;
	}
	int dx = (56-miniSprite->getW(imgid))>>1;
	int dy = (46-miniSprite->getH(imgid))>>1;
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	miniSprite->setBaseColor(selBuild->owner->color);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+dx, ypos+4+dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	if (buildingType->hpMax)
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[hp]"));
		globalContainer->littleFont->popStyle();

		if (selBuild->hp <= buildingType->hpMax/5)
			{ r=255; g=0; b=0; }
		else
			{ r=0; g=255; b=0; }

		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->hp).arg(buildingType->hpMax).c_str());
		globalContainer->littleFont->popStyle();
	}

	// inside
	if (buildingType->maxUnitInside && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE, globalContainer->littleFont, Toolkit::getStringTable()->getString("[inside]"));
		globalContainer->littleFont->popStyle();
		if (selBuild->buildingState==Building::ALIVE)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->unitsInside.size()).arg(selBuild->maxUnitInside).c_str());
		}
		else
		{
			if (selBuild->unitsInside.size()>1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0%1").arg(Toolkit::getStringTable()->getString("[Still (i)]")).arg(selBuild->unitsInside.size()).c_str());
			}
			else if (selBuild->unitsInside.size()==1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont,
					Toolkit::getStringTable()->getString("[Still one]") );
			}
		}
	}

	// it is a unit ranged attractor (aka flag)
	if (buildingType->defaultUnitStayRange && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		// get flag stat
		int goingTo, onSpot;
		selBuild->computeFlagStatLocal(&goingTo, &onSpot);
		// display flag stat
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormatableString("%0").arg(Toolkit::getStringTable()->getString("[In way]")).c_str());
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(goingTo).c_str());
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE,
		globalContainer->littleFont, FormatableString(Toolkit::getStringTable()->getString("[On the spot]")).c_str());
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-+RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(onSpot).c_str());
	}

	ypos += YOFFSET_ICON+YOFFSET_B_SEP;

	// working bar
	if (buildingType->maxUnitWorking)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if (selBuild->buildingState==Building::ALIVE)
			{
				const char *working = Toolkit::getStringTable()->getString("[working]");
				const int len = globalContainer->littleFont->getStringWidth(working)+4;
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, working);
				globalContainer->littleFont->popStyle();
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0/%1").arg((int)selBuild->unitsWorking.size()).arg(selBuild->maxUnitWorkingLocal).c_str());
				drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, selBuild->maxUnitWorkingLocal, selBuild->maxUnitWorkingLocal, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
			}
			else
			{
				if (selBuild->unitsWorking.size()>1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0%1%2").arg(Toolkit::getStringTable()->getString("[still (w)]")).arg(selBuild->unitsWorking.size()).arg(Toolkit::getStringTable()->getString("[units working]")).c_str());
				}
				else if (selBuild->unitsWorking.size()==1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
						Toolkit::getStringTable()->getString("[still one unit working]") );
				}
			}
		}
		if(hilights.find(HilightUnitsAssignedBar) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, ypos+6, 38));
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}
	
	// priority buttons
	if(buildingType->maxUnitWorking)
	{
		if((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if(selBuild->buildingState==Building::ALIVE)
			{
				ypos += YOFFSET_B_SEP;
				
				int width = 128/3;
				const char *prioritystr = Toolkit::getStringTable()->getString("[priority]");
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, prioritystr);

				const char *lowstr = Toolkit::getStringTable()->getString("[low priority]");
				const char *medstr = Toolkit::getStringTable()->getString("[medium priority]");
				const char *highstr = Toolkit::getStringTable()->getString("[high priority]");
				
				drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+12+4, (selBuild->priorityLocal==-1));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14, ypos+12+2, globalContainer->littleFont, lowstr);
				
				drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+width, ypos+12+4, (selBuild->priorityLocal==0));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14+width, ypos+12+2, globalContainer->littleFont, medstr);
				
				drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+width*2, ypos+12+4, (selBuild->priorityLocal==1));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14+width*2, ypos+12+2, globalContainer->littleFont, highstr);
				
				ypos += YOFFSET_BAR+YOFFSET_B_SEP;
			}
		}
	}
	
	// flag range bar
	if (buildingType->defaultUnitStayRange)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			const char *range = Toolkit::getStringTable()->getString("[range]");
			const int len = globalContainer->littleFont->getStringWidth(range)+4;
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, range);
			globalContainer->littleFont->popStyle();
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0").arg(selBuild->unitStayRange).c_str());
			drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, selBuild->type->maxUnitStayRange);
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}
	
	// flag control of team and allies
	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// cleared ressources for clearing flags:
		if (buildingType->type == "clearingflag")
		{
			ypos += YOFFSET_B_SEP;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Clearing:]"));
			ypos += YOFFSET_TEXT_PARA;
			int j=0;
			for (int i=0; i<BASIC_COUNT; i++)
				if (i!=STONE)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,
						getRessourceName(i));
					int spriteId;
					if (selBuild->clearingRessourcesLocal[i])
						spriteId=20;
					else
						spriteId=19;
					globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);
					
					ypos+=YOFFSET_TEXT_PARA;
					j++;
				}
		}
		// min war level for war flags:
		else if (buildingType->type == "warflag")
		{
			ypos += YOFFSET_B_SEP;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Min required level:]"));
			ypos += YOFFSET_TEXT_PARA;
			for (int i=0; i<4; i++)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont, 1+i);
				int spriteId;
				if (i==selBuild->minLevelToFlagLocal)
					spriteId=20;
				else
					spriteId=19;
				globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);
				
				ypos+=YOFFSET_TEXT_PARA;
			}
		}
		else if (buildingType->type == "explorationflag")
		{
			int spriteId;
			
			ypos += YOFFSET_B_SEP;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Min required level:]"));
			ypos += YOFFSET_TEXT_PARA;
			
			// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
			// must be able to do to be accepted at this flag
			// 0 == any explorer
			// 1 == must be able to attack ground
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,Toolkit::getStringTable()->getString("[any explorer]"));
			if (selBuild->minLevelToFlagLocal == 0)
				spriteId = 20;
			else
				spriteId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);
			
			ypos += YOFFSET_TEXT_PARA;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,Toolkit::getStringTable()->getString("[ground attack]"));
			if (selBuild->minLevelToFlagLocal == 1)
				spriteId = 20;
			else
				spriteId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);
			ypos += YOFFSET_TEXT_PARA;
		}
	}

	// other infos
	if (buildingType->armor)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[armor]")).arg(buildingType->armor).c_str());
		ypos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	if (buildingType->shootDamage)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+1, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[damage]")).arg(buildingType->shootDamage).c_str());
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+12, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[range]")).arg(buildingType->shootingRange).c_str());
		ypos += YOFFSET_TOWER;
	}

	// There is unit inside, show time to leave
	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// we select food buildings, heal buildings, and upgrade buildings:
		int maxTimeTo=0;
		if (buildingType->timeToFeedUnit)
			maxTimeTo=buildingType->timeToFeedUnit;
		else if (buildingType->timeToHealUnit)
			maxTimeTo=buildingType->timeToHealUnit;
		else
			for (int i=0; i<NB_ABILITY; i++)
				if (buildingType->upgradeTime[i])
					maxTimeTo=std::max(maxTimeTo, buildingType->upgradeTime[i]);
		int dec = (RIGHT_MENU_RIGHT_OFFSET-128);
		if (maxTimeTo)
		{
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, 128, 7, 168, 150, 90);
			for (std::list<Unit *>::iterator it=selBuild->unitsInside.begin(); it!=selBuild->unitsInside.end(); ++it)
			{
				Unit *u=*it;
				assert(u);
				if (u->displacement==Unit::DIS_INSIDE)
				{
					int dividend=-u->insideTimeout*128+128-u->delta/2;
					int divisor=1+maxTimeTo;
					int left=dividend/divisor;
					int alpha=((dividend%divisor)*255)/divisor;
					
					if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
					{
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1-dec, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-dec, ypos, 7, 63, 111, 149);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1-dec, ypos, 7, 17, 30, 64);
					}
					else
					{
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-2-dec, ypos, 7, 17, 30, 64, alpha);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1-dec, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-dec, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1-dec, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+2-dec, ypos, 7, 17, 30, 64, 255-alpha);
						
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1-dec, ypos, 7, 63, 111, 149, alpha);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-dec, ypos, 7, 63, 111, 149);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1-dec, ypos, 7, 63, 111, 149, 255-alpha);
					}
				}
			}
			
			ypos += YOFFSET_PROGRESS_BAR;
			unitInsideBarYDec = YOFFSET_PROGRESS_BAR;
		}
	}
	
	ypos += YOFFSET_B_SEP;

	// exchange building
	if (buildingType->canExchange && ((selBuild->owner->sharedVisionExchange)&(1<<localTeamNo)))
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[market]"));
		globalContainer->littleFont->popStyle();
		//globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36-3, ypos+1, globalContainer->gamegui, EXCHANGE_BUILDING_ICONS);
		ypos += YOFFSET_TEXT_PARA;
		for (unsigned i=0; i<HAPPYNESS_COUNT; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1/%2)").arg(getRessourceName(i+HAPPYNESS_BASE)).arg(selBuild->ressources[i+HAPPYNESS_BASE]).arg(buildingType->maxRessource[i+HAPPYNESS_BASE]).c_str());

			/*
			int inId, outId;
			if (selBuild->receiveRessourceMaskLocal & (1<<i))
				inId = 20;
			else
				inId = 19;
			if (selBuild->sendRessourceMaskLocal & (1<<i))
				outId = 20;
			else
				outId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36, ypos+2, globalContainer->gamegui, inId);
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-18, ypos+2, globalContainer->gamegui, outId);
			*/

			ypos += YOFFSET_TEXT_PARA;
		}
	}

	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// ressorces for every building except exchange building
		if (!buildingType->canExchange)
		{

			// ressources in
			unsigned j = 0;
			for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
			{
				if (buildingType->maxRessource[i])
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(getRessourceName(i)).arg(selBuild->ressources[i]).arg(buildingType->maxRessource[i]).c_str());
					j++;
					ypos += 11;
				}
			}
			if (buildingType->maxBullets)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[Bullets]")).arg(selBuild->bullets).arg(buildingType->maxBullets).c_str());
				j++;
				ypos += 11;
			}
			ypos+=5;
		}
		//Unit production ratios and unit production
		if (buildingType->unitProductionTime) // swarm
		{
			int left=(selBuild->productionTimeout*128)/buildingType->unitProductionTime;
			int elapsed=128-left;
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, elapsed, 7, 100, 100, 255);
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+elapsed, ypos, left, 7, 128, 128, 128);

			ypos+=15;
			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selBuild->ratio[i], selBuild->ratioLocal[i], 0, MAX_RATIO_RANGE);
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+24, ypos, globalContainer->littleFont, getUnitName(i));
				
				if(i==1 && hilights.find(HilightRatioBar) != hilights.end())
				{
					arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET-36, ypos-8, 38));
				}
				
				ypos+=20;
			}
			
		}
		
		// data on whether or not the building is recieving units
		bool otherFailure=true;
		for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
		{
			int n = selBuild->unitsFailingRequirements[j];
			if(j!=0 && n>0)
				otherFailure=true;
		}
		if(otherFailure)
		{
			for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
			{
				int n = selBuild->unitsFailingRequirements[j];
				if(n>0 && (int)selBuild->unitsWorking.size() < selBuild->desiredMaxUnitWorking)
				{
					std::string s;
					if(j == Building::UnitNotAvailable)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units not available]")).arg(n);
					if(j == Building::UnitTooLowLevel)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too low level]")).arg(n);
					else if(j == Building::UnitCantAccessBuilding)
					{
						if (buildingType->isVirtual)
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access flag]")).arg(n);
						else
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access building]")).arg(n);
					}
					else if(j == Building::UnitTooFarFromBuilding)
					{
						if (buildingType->isVirtual)
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from flag]")).arg(n);
						else
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from building]")).arg(n);
					}
					else if(j == Building::UnitCantAccessResource)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access resource]")).arg(n);
					else if(j == Building::UnitCantAccessFruit)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from resource]")).arg(n);
					else if(j == Building::UnitTooFarFromResource)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access fruit]")).arg(n);
					else if(j == Building::UnitTooFarFromFruit)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from fruit]")).arg(n);
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+10, ypos, globalContainer->littleFont, s.c_str());
					ypos+=11;
				}
			}
		}

		// repair and upgrade
		if(selBuild->owner == localTeam)
		{ 
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				if (buildingType->isBuildingSite)
					assert(buildingType->nextLevel!=-1);
				drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[cancel repair]");
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				assert(buildingType->nextLevel!=-1);
				if (buildingType->isBuildingSite)
					assert(buildingType->prevLevel!=-1);
				drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[cancel upgrade]");
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
			{
				if (selBuild->hp<buildingType->hpMax)
				{
					// repair
					if (selBuild->type->regenerationSpeed==0 && selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && localTeam->maxBuildLevel()>=buildingType->level)
					{
						drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[repair]");
						if ( mouseX>globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+12 && mouseX<globalContainer->gfx->getW()-12
							&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
							{
								globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
								int ressources[BASIC_COUNT];
								selBuild->getRessourceCountToRepair(ressources);
								drawCosts(ressources, globalContainer->littleFont);
								globalContainer->littleFont->popStyle();
							}
					}
				}
				else if (buildingType->nextLevel!=-1)
				{
					// upgrade
					if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
					{
						drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[upgrade]");
						if ( mouseX>globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+12 && mouseX<globalContainer->gfx->getW()-12
							&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
							{
								globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));

								// We draw the ressources cost.
								int typeNum=buildingType->nextLevel;
								BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
								drawCosts(bt->maxRessource, globalContainer->littleFont);

								// We draw the new abilities:
								int blueYpos = YPOS_BASE_BUILDING + YOFFSET_NAME;

								bt=globalContainer->buildingsTypes.get(bt->nextLevel);

								if (bt->hpMax)
									drawValueAlignedRight(blueYpos+YOFFSET_TEXT_LINE, bt->hpMax);
								if (bt->maxUnitInside)
									drawValueAlignedRight(blueYpos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, bt->maxUnitInside);
								blueYpos += YOFFSET_ICON+YOFFSET_B_SEP;

								if (buildingType->maxUnitWorking)
									blueYpos += YOFFSET_BAR+YOFFSET_B_SEP;

								if (bt->armor)
								{
									if (!buildingType->armor)
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, blueYpos-1, globalContainer->littleFont, Toolkit::getStringTable()->getString("[armor]"));
									drawValueAlignedRight(blueYpos-1, bt->armor);
									blueYpos+=YOFFSET_TEXT_LINE;
								}
								if (buildingType->maxUnitInside)
									blueYpos += YOFFSET_INFOS;
								if (bt->shootDamage)
								{
									drawValueAlignedRight(blueYpos+1, bt->shootDamage);
									drawValueAlignedRight(blueYpos+12, bt->shootingRange);
									blueYpos += YOFFSET_TOWER;
								}
								blueYpos += unitInsideBarYDec;
								blueYpos += YOFFSET_B_SEP;

								unsigned j = 0;
								for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
								{
									if (buildingType->maxRessource[i])
									{
										drawValueAlignedRight(blueYpos+(j*11), bt->maxRessource[i]);
										j++;
									}
								}
								
								if (bt->maxBullets)
								{
									drawValueAlignedRight(blueYpos+(j*11), bt->maxBullets);
									j++;
								}

								globalContainer->littleFont->popStyle();
							}
					}
				}
			}

			// building destruction
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				drawRedButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-24, "[cancel destroy]");
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				drawRedButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-24, "[destroy]");
			}
		}
	}
}

void GameGUI::drawRessourceInfos(void)
{
	const Ressource &r = game.map.getRessource(selection.ressource);
	int ypos = YPOS_BASE_RESSOURCE;
	if (r.type!=NO_RES_TYPE)
	{
		// Draw ressource name
		const std::string &ressourceName = getRessourceName(r.type);
		int titleLen = globalContainer->littleFont->getStringWidth(ressourceName.c_str());
		int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
		globalContainer->gfx->drawString(titlePos, ypos+(YOFFSET_TEXT_PARA>>1), globalContainer->littleFont, ressourceName.c_str());
		ypos += 2*YOFFSET_TEXT_PARA;
		
		// Draw ressource image
		const RessourceType* rt = globalContainer->ressourcesTypes.get(r.type);
		unsigned resImg = rt->gfxId + r.variety*rt->sizesCount + r.amount;
		if (!rt->eternal)
			resImg--;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+16, ypos, globalContainer->ressources, resImg);
		
		// Draw ressource count
		if (rt->granular)
		{
			int sizesCount=rt->sizesCount;
			int amount=r.amount;
			const std::string amountS = FormatableString("%0/%1").arg(amount).arg(sizesCount);
			int amountSH = globalContainer->littleFont->getStringHeight(amountS.c_str());
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-64, ypos+((32-amountSH)>>1), globalContainer->littleFont, amountS.c_str());
		}
	}
	else
	{
		clearSelection();
	}
}

void GameGUI::drawReplayPanel(void)
{
	Font *font=globalContainer->littleFont;

	int x = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + REPLAY_PANEL_XOFFSET;
	int y = REPLAY_PANEL_YOFFSET;
	int inc = REPLAY_PANEL_SPACE_BETWEEN_OPTIONS;

	globalContainer->gfx->drawString(x, y, font, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[Options]")));

	drawCheckButton(x, y + 1*inc, Toolkit::getStringTable()->getString("[fog of war]"), globalContainer->replayShowFog);
	drawCheckButton(x, y + 2*inc, Toolkit::getStringTable()->getString("[combined vision]"), (globalContainer->replayVisibleTeams == 0xFFFFFFFF));
	drawCheckButton(x, y + 3*inc, Toolkit::getStringTable()->getString("[show areas]"), (globalContainer->replayShowAreas));
	drawCheckButton(x, y + 4*inc, Toolkit::getStringTable()->getString("[show flags]"), (globalContainer->replayShowFlags));

	globalContainer->gfx->drawString(x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET, font, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[players]")));

	for (int i = 0; i < game.teamsCount(); i++)
	{
		// I know this is a matter of taste, but I prefer checkboxes here. Radio buttons are a totally different style
		//drawRadioButton(x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc, game.teams[i]->getFirstPlayerName().c_str(), localTeamNo == i);
		drawRadioButton(x + 1, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc + 1, localTeamNo == i);
		globalContainer->gfx->drawString(x + 20, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc, font, game.teams[i]->getFirstPlayerName().c_str());
	}
}

void GameGUI::drawReplayProgressBar(void)
{
	// set the clipping rectangle
	globalContainer->gfx->setClipRect( 0, globalContainer->settings.screenHeight-30, globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH, 30);

	// draw menu background, black if low speed graphics, transparent otherwise
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect( 0, globalContainer->settings.screenHeight-28, globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH, 28, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect( 0, globalContainer->settings.screenHeight-28, globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH, 28, 0, 0, 40, 180);

	// Draw the actual progress bar
	Style::style->drawProgressBar(globalContainer->gfx, 3, 
		globalContainer->settings.screenHeight-23, 
		globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH-10, 
		globalContainer->replayStepsProcessed, 
		globalContainer->replayStepsTotal);
		
	// Calculate the time
	// This is based on default speed 25 fps, not the actual Engine's speed
	// because if we fast-forward we still want to see the old time
	unsigned int time1_sec = (globalContainer->replayStepsProcessed/25)%60;
	unsigned int time1_min = (globalContainer->replayStepsProcessed/(25*60))%60;
	unsigned int time1_hour = (globalContainer->replayStepsProcessed/(25*3600));
	
	unsigned int time2_sec = (globalContainer->replayStepsTotal/25)%60;
	unsigned int time2_min = (globalContainer->replayStepsTotal/(25*60))%60;
	unsigned int time2_hour = (globalContainer->replayStepsTotal/(25*3600));

	// Draw the time
	if (time2_hour <= 99)
	{
		globalContainer->gfx->drawString(10, globalContainer->settings.screenHeight-20, globalContainer->littleFont,
			FormatableString("%0:%1:%2 / %3:%4:%5")
			.arg(time1_hour)
			.arg(time1_min,2,10,'0')
			.arg(time1_sec,2,10,'0')
			.arg(time2_hour)
			.arg(time2_min,2,10,'0')
			.arg(time2_sec,2,10,'0')
			.c_str());
	}
	else
	{
		// Time did not get saved properly, don't show it
		globalContainer->gfx->drawString(10, globalContainer->settings.screenHeight-20, globalContainer->littleFont,
			FormatableString("%0:%1:%2")
			.arg(time1_hour)
			.arg(time1_min,2,10,'0')
			.arg(time1_sec,2,10,'0')
			.c_str());
	}

	// Draw the border
	for (int i=0; i<globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH; i+=32)
	{
		globalContainer->gfx->drawSprite(i, globalContainer->settings.screenHeight-30, globalContainer->gamegui, 16);
	}
}

void GameGUI::drawPanel(void)
{
	// ensure we have a valid selection and associate pointers
	checkSelection();

	// set the clipping rectangle
	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 128, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128);

	// draw menu background, black if low speed graphics, transparent otherwise
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 133, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 133, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	if(hilights.find(HilightRightSidePanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, globalContainer->gfx->getH()/2, 38));
	}

	// draw the panel selection buttons
	drawPanelButtons(YPOS_BASE_DEFAULT-32);

	switch(selectionMode)
	{
	case BUILDING_SELECTION:
		drawBuildingInfos();
		break;
	case UNIT_SELECTION:
		drawUnitInfos();
		break;
	case RESSOURCE_SELECTION:
		drawRessourceInfos();
		break;
	default:
		if (!globalContainer->replaying)
		{
			switch(displayMode)
			{
			case CONSTRUCTION_VIEW:
				drawChoice(YPOS_BASE_CONSTRUCTION, buildingsChoiceName, buildingsChoiceState);
				break;
			case FLAG_VIEW:
				drawFlagView();
				break;
			case STAT_TEXT_VIEW:
				teamStats->drawText(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT);
				break;
			case STAT_GRAPH_VIEW:
				teamStats->drawStat(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+64, Toolkit::getStringTable()->getString("[Starving Map]"), showStarvingMap);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+88, Toolkit::getStringTable()->getString("[Damaged Map]"), showDamagedMap);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+112, Toolkit::getStringTable()->getString("[Defense Map]"), showDefenseMap);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+136, Toolkit::getStringTable()->getString("[Fertility Map]"), showFertilityMap);
				break;
			default:
				std::cout << "Was not expecting displayMode" << displayMode;
				assert(false);
			}
		}
		else
		{
			switch(replayDisplayMode)
			{
			case RDM_REPLAY_VIEW:
				drawReplayPanel();
				break;
			case RDM_STAT_TEXT_VIEW:
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+15, YPOS_BASE_STAT+5, globalContainer->littleFont, FormatableString("%0 %1").arg(Toolkit::getStringTable()->getString("[watching:]")).arg(localTeam->getFirstPlayerName()).c_str());
				teamStats->drawText(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT+15);
				break;
			case RDM_STAT_GRAPH_VIEW:
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+15, YPOS_BASE_STAT+5, globalContainer->littleFont, FormatableString("%0 %1").arg(Toolkit::getStringTable()->getString("[watching:]")).arg(localTeam->getFirstPlayerName()).c_str());
				teamStats->drawStat(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT+15);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+64, Toolkit::getStringTable()->getString("[Starving Map]"), showStarvingMap);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+88, Toolkit::getStringTable()->getString("[Damaged Map]"), showDamagedMap);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+112, Toolkit::getStringTable()->getString("[Defense Map]"), showDefenseMap);
				drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+136, Toolkit::getStringTable()->getString("[Fertility Map]"), showFertilityMap);
				break;
			default:
				std::cout << "Was not expecting replayDisplayMode" << replayDisplayMode;
				assert(false);
			}
		}
	}
}

void GameGUI::drawFlagView(void)                                                                                                                                                        
{
	int dec = (RIGHT_MENU_WIDTH - 128)/2;
	// draw flags
	drawChoice(YPOS_BASE_FLAG, flagsChoiceName, flagsChoiceState, 3);
	
	// draw choice of area
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 13);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+48+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 14);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+88+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 25);
	if (brush.getType() != BrushTool::MODE_NONE)
	{
		int decX = 8 + ((int)toolManager.getZoneType()) * 40 + dec;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 22);
	}
	if(hilights.find(HilightForbiddenZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+8+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	if(hilights.find(HilightGuardZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+48+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	if(hilights.find(HilightClearingZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+88+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	
	// draw brush
	brush.draw(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH+40);
	
	if(hilights.find(HilightBrushSelector) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH+40+30, 38));
	}
	
	// draw brush help text
	if ((mouseX>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+dec) && (mouseY>YPOS_BASE_FLAG+YOFFSET_BRUSH))
	{
		int buildingInfoStart = globalContainer->gfx->getH()-50;
		if (mouseY<YPOS_BASE_FLAG+YOFFSET_BRUSH+40)
		{
			int panelMouseX = mouseX - globalContainer->gfx->getW() + RIGHT_MENU_WIDTH;
			if (panelMouseX < 44)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[forbidden area]");
			else if (panelMouseX < 84)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[guard area]");
			else
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[clear area]");
		}
		else
		{
			if (toolManager.getZoneType() == GameGUIToolManager::Forbidden)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[forbidden area]");
			else if (toolManager.getZoneType() == GameGUIToolManager::Guard)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[guard area]");
			else if (toolManager.getZoneType() == GameGUIToolManager::Clearing)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[clear area]");
			else
				assert(false);
		}
	}
}

void GameGUI::drawTopScreenBar(void)
{
	// bar background 
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 16, 0, 0, 40, 180);

	// draw unit stats
	Uint8 redC[]={200, 0, 0};
	Uint8 greenC[]={0, 200, 0};
	Uint8 whiteC[]={200, 200, 200};
	Uint8 yellowC[]={200, 200, 0};
	Uint8 actC[3];
	int free, tot;

	int dec = (globalContainer->gfx->getW()-640)>>2;
	dec += 10;

	globalContainer->unitmini->setBaseColor(localTeam->color);
	for (int i=0; i<3; i++)
	{
		free = teamStats->getFreeUnits(i);
		// worker is a special case
		if (i==0)
			free -= teamStats->getWorkersNeeded();
		tot = teamStats->getTotalUnits(i);
		if (free<0)
			memcpy(actC, redC, sizeof(redC));
		else if (free>0)
			memcpy(actC, greenC, sizeof(greenC));
		else
			memcpy(actC, whiteC, sizeof(whiteC));

		globalContainer->gfx->drawSprite(dec+2, -1, globalContainer->unitmini, i);
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, actC[0], actC[1], actC[2]));
		globalContainer->gfx->drawString(dec+22, 0, globalContainer->littleFont, FormatableString("%0 / %1").arg(free).arg(tot).c_str());
		globalContainer->littleFont->popStyle();
		
		if(i==WORKER && hilights.find(HilightWorkersWorkingFreeStat) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(dec+22, 32, 39));
		}
		
		else if(i==WARRIOR && hilights.find(HilightExplorersWorkingFreeStat) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(dec+22, 32, 39));
		}
		
		else if(i==EXPLORER && hilights.find(HilightWarriorsWorkingFreeStat) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(dec+22, 32, 39));
		}

		dec += 70;
	}

	// draw prestige stats
	globalContainer->gfx->drawString(dec+0, 0, globalContainer->littleFont, FormatableString("%0 / %1 / %2").arg(localTeam->prestige).arg(game.totalPrestige).arg(game.prestigeToReach).c_str());
	
	dec += 90;
	
	// draw unit conversion stats
	globalContainer->gfx->drawString(dec, 0, globalContainer->littleFont, FormatableString("+%0 / -%1").arg(localTeam->unitConversionGained).arg(localTeam->unitConversionLost).c_str());
	
	// draw CPU load
	dec += 70;
	int cpuLoad=0;
	for (unsigned i=0; i<SMOOTHED_CPU_SIZE; i++)
		cpuLoad += smoothedCPULoad[i];

	cpuLoad /= SMOOTHED_CPU_SIZE;

	if (cpuLoad<50)
		memcpy(actC, greenC, sizeof(greenC));
	else if (cpuLoad<75)
		memcpy(actC, yellowC, sizeof(yellowC));
	else
		memcpy(actC, redC, sizeof(redC));

	int cpuLength = int(float(cpuLoad) / 100.0 * 40.0);

	globalContainer->gfx->drawFilledRect(dec, 4, cpuLength, 8, actC[0], actC[1], actC[2]);
	globalContainer->gfx->drawVertLine(dec, 2, 12, 200, 200, 200);
	globalContainer->gfx->drawVertLine(dec+40, 2, 12, 200, 200, 200);
	
	// draw window bar
	int pos=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-16;
	for (int i=0; i<pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	for (int i=16; i<globalContainer->gfx->getH(); i+=32)
	{
		globalContainer->gfx->drawSprite(pos+12, i, globalContainer->gamegui, 17);
	}

	// draw main menu button
	if (inGameMenu==IGM_MAIN)
		globalContainer->gfx->drawSprite(pos, 0, globalContainer->gamegui, 7);
	else
		globalContainer->gfx->drawSprite(pos, 0, globalContainer->gamegui, 6);

	// draw alliance button
	if (inGameMenu==IGM_ALLIANCE)
		globalContainer->gfx->drawSprite(pos, 36, globalContainer->gamegui, 44);
	else
		globalContainer->gfx->drawSprite(pos, 36, globalContainer->gamegui, 45);

	// draw objectives button
	if (inGameMenu==IGM_OBJECTIVES)
		globalContainer->gfx->drawSprite(pos, 72, globalContainer->gamegui, 46);
	else
		globalContainer->gfx->drawSprite(pos, 72, globalContainer->gamegui, 47);
	
	if(hilights.find(HilightMainMenuIcon) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(pos-32, 32, 43));
	}
}

void GameGUI::drawOverlayInfos(void)
{
	if (selectionMode==TOOL_SELECTION)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		toolManager.drawTool(mouseX, mouseY, localTeamNo, viewportX, viewportY);
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		toolManager.drawTool(mouseX, mouseY, localTeamNo, viewportX, viewportY);
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		int centerX, centerY;
		game.map.buildingPosToCursor(selBuild->posXLocal, selBuild->posYLocal,  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
		if (selBuild->owner->teamNumber==localTeamNo)
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 0, 0, 190);
		else if ((localTeam->allies) & (selBuild->owner->me))
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 255, 196, 0);
		else if (!selBuild->type->isVirtual)
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 190, 0, 0);

		// draw a white circle around units that are working at building
		if ((showUnitWorkingToBuilding)
			&& ((selBuild->owner->allies) &(1<<localTeamNo)))
		{
			for (std::list<Unit *>::iterator unitsWorkingIt=selBuild->unitsWorking.begin(); unitsWorkingIt!=selBuild->unitsWorking.end(); ++unitsWorkingIt)
			{
				Unit *unit=*unitsWorkingIt;
				int px, py;
				game.map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, viewportX, viewportY);
				int deltaLeft=255-unit->delta;
				if (unit->action<BUILD)
				{
					px-=(unit->dx*deltaLeft)>>3;
					py-=(unit->dy*deltaLeft)>>3;
				}
				globalContainer->gfx->drawCircle(px+16, py+16, 16, 255, 255, 255, 180);
			}
		}
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		int rx = selection.ressource & game.map.getMaskW();
		int ry = selection.ressource >> game.map.getShiftW();
		int px, py;
		game.map.mapCaseToDisplayable(rx, ry, &px, &py, viewportX, viewportY);
		globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 190);
	}

	// draw message List
	if (game.anyPlayerWaited && game.maskAwayPlayer && game.anyPlayerWaitedTimeFor>2)
	{
		int nbap=0; // Number of away players
		Uint32 pm=1;
		Uint32 apm=game.maskAwayPlayer;
		for(int pi=0; pi<game.gameHeader.getNumberOfPlayers(); pi++)
		{
			if (pm&apm)
				nbap++;
			pm=pm<<1;
		}

		globalContainer->gfx->drawFilledRect(32, 32, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64, 22+nbap*20, 0, 0, 140, 127);
		globalContainer->gfx->drawRect(32, 32, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64, 22+nbap*20, 255, 255, 255);
		pm=1;
		int pnb=0;
		for(int pi2=0; pi2<game.gameHeader.getNumberOfPlayers(); pi2++)
		{
			if (pm&apm)
			{
				globalContainer->gfx->drawString(44, 44+pnb*20, globalContainer->standardFont, FormatableString(Toolkit::getStringTable()->getString("[waiting for %0]")).arg(game.players[pi2]->name).c_str());
				pnb++;
			}
			pm=pm<<1;
		}
	}
	else
	{
		int ymesg = 32;
		int yinc = 0;

		// TODO: die with SGSL
		// show script text
		if (game.script.isTextShown)
		{
			std::vector<std::string> lines;
			setMultiLine(game.script.textShown, &lines);
			globalContainer->gfx->drawFilledRect(24, ymesg-8, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64+16, lines.size()*20+16, 0,0,0,128);
			for (unsigned i=0; i<lines.size(); i++)
			{
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, lines[i].c_str());
				yinc += 20;
			}
		
			if (swallowSpaceKey)
			{
				globalContainer->gfx->drawFilledRect(24, ymesg+yinc+8, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64+16, 20, 0,0,0,128);
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, Toolkit::getStringTable()->getString("[press space]"));
				yinc += 20;
			}
			yinc += 8;
		}
		
		// show script text
		if (!scriptText.empty())
		{
			std::vector<std::string> lines;
			setMultiLine(scriptText, &lines);
			globalContainer->gfx->drawFilledRect(24, ymesg-8, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64+16, lines.size()*20+16, 0,0,0,128);
			for (unsigned i=0; i<lines.size(); i++)
			{
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, lines[i].c_str());
				yinc += 20;
			}
		}

		// show script counter
		if (game.script.getMainTimer())
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-165, ymesg, globalContainer->standardFont, FormatableString("%0").arg(game.script.getMainTimer()).c_str());
			yinc = std::max(yinc, 32);
		}

		ymesg += yinc+2;
		
		messageManager.drawAllGameMessages(32, ymesg);
	}

	// display map mark
	globalContainer->gfx->setClipRect();
	markManager.drawAll(localTeamNo, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+20, 10, 128, viewportX, viewportY, game);

	// display text if placing a building 
	if(selectionMode == TOOL_SELECTION && toolManager.getBuildingName() != "")
	{
		globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255,255,255)));
		globalContainer->gfx->drawString(10, globalContainer->gfx->getH()-100, globalContainer->standardFont,  Toolkit::getStringTable()->getString("[Building Tool Line Explanation]"), 0, 75);
		globalContainer->gfx->drawString(10, globalContainer->gfx->getH()-100+12, globalContainer->standardFont,  Toolkit::getStringTable()->getString("[Building Tool Box Explanation]"), 0, 75);
		globalContainer->standardFont->popStyle();
	}

	// Draw icon if trasmitting
	if (globalContainer->voiceRecorder->recordingNow)
		globalContainer->gfx->drawSprite(5, globalContainer->gfx->getH()-50, globalContainer->gamegui, 24);

	// Draw which players are transmitting voice
	int xinc = 42;
	for(int p=0; p<Team::MAX_COUNT; ++p)
	{
		if(globalContainer->mix->isPlayerTransmittingVoice(p))
		{
			if(xinc==42)
			{
				globalContainer->gamegui->setBaseColor(game.teams[game.players[p]->teamNumber]->color);
				globalContainer->gfx->drawSprite(42, globalContainer->gfx->getH()-55, globalContainer->gamegui, 30);
				xinc += 47;
			}
			int height = globalContainer->standardFont->getStringHeight(game.players[p]->name.c_str());
			
			globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_NORMAL, game.teams[game.players[p]->teamNumber]->color));
			globalContainer->gfx->drawString(xinc, globalContainer->gfx->getH()-35-height/2, globalContainer->standardFont, game.players[p]->name);
			xinc += globalContainer->standardFont->getStringWidth(game.players[p]->name.c_str()) + 5;
			globalContainer->standardFont->popStyle();
		}
	}
	
	if(!scrollableText)
		messageManager.drawAllChatMessages(32, globalContainer->gfx->getH() - 165);

	// Draw the bar contining number of units, CPU load, etc...
	drawTopScreenBar();
}

void GameGUI::drawInGameMenu(void)
{
	gameMenuScreen->dispatchPaint();
	globalContainer->gfx->drawSurface((int)gameMenuScreen->decX, (int)gameMenuScreen->decY, gameMenuScreen->getSurface());
	
	// Draw a-la-aqua drop shadows
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		int x = gameMenuScreen->decX;
		int y = gameMenuScreen->decY;
		int w = gameMenuScreen->getSurface()->getW();
		int h = gameMenuScreen->getSurface()->getH();
		
		globalContainer->gfx->drawSprite(x-8, y+h, globalContainer->terrainShader, 17);
		globalContainer->gfx->drawSprite(x+w, y+h, globalContainer->terrainShader, 18);
		globalContainer->gfx->setClipRect(x, y+h, w, 16);
		for (int i=0; i<w+31; i+=32)
		{
			globalContainer->gfx->drawSprite(x+i, y+h, globalContainer->terrainShader, 16);
		}
		globalContainer->gfx->setClipRect(x-8, y, w+16, h);
		for (int i=0; i<h+31; i+=32)
		{
			globalContainer->gfx->drawSprite(x-8, y+i, globalContainer->terrainShader, 19);
			globalContainer->gfx->drawSprite(x+w, y+i, globalContainer->terrainShader, 20);
		}
	}
}

void GameGUI::drawInGameTextInput(void)
{
	typingInputScreen->decX=(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-492)/2;
	typingInputScreen->decY=globalContainer->gfx->getH()-typingInputScreenPos;
	typingInputScreen->dispatchPaint();
	globalContainer->gfx->drawSurface((int)typingInputScreen->decX, (int)typingInputScreen->decY, typingInputScreen->getSurface());
	if (typingInputScreenInc>0)
	{
		if (typingInputScreenPos<TYPING_INPUT_MAX_POS-TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			typingInputScreenPos=TYPING_INPUT_MAX_POS;
		}
	}
	else if (typingInputScreenInc<0)
	{
		if (typingInputScreenPos>TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			delete typingInputScreen;
			typingInputScreen=NULL;
		}
	}
}

void GameGUI::drawInGameScrollableText(void)
{
	scrollableText->decX=28;
	scrollableText->decY=globalContainer->gfx->getH() - 165;
	scrollableText->dispatchPaint();
	globalContainer->gfx->drawSurface(scrollableText->decX, scrollableText->decY, scrollableText->getSurface());
}

void GameGUI::drawAll(int team)
{

	// draw the map
	Uint32 drawOptions =	(drawHealthFoodBar ? Game::DRAW_HEALTH_FOOD_BAR : 0) |
								(drawPathLines ?  Game::DRAW_PATH_LINE : 0) |
								(drawAccessibilityAids ? Game::DRAW_ACCESSIBILITY : 0 ) |
								((selectionMode==TOOL_SELECTION) ? Game::DRAW_BUILDING_RECT : 0) |
								((showStarvingMap) ? Game::DRAW_OVERLAY : 0) |
								((showDamagedMap) ? Game::DRAW_OVERLAY : 0) |
								((showDefenseMap) ? Game::DRAW_OVERLAY : 0) |
								((showFertilityMap) ? Game::DRAW_OVERLAY : 0) |
								((globalContainer->replaying && !globalContainer->replayShowFog) ? Game::DRAW_WHOLE_MAP : 0) |
								Game::DRAW_AREA;
	
	updateHilightInGame();
	arrowPositions.clear();
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
	{
		globalContainer->gfx->setClipRect(0, 16, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-16);
		game.drawMap(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH(),viewportX, viewportY, localTeamNo, drawOptions);
	}
	else
	{
		std::set<Building*> visibleBuildings;
		
		globalContainer->gfx->setClipRect();
		
		game.drawMap(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH(),viewportX, viewportY, localTeamNo, drawOptions, &visibleBuildings);
		
		// generate and draw particles
		generateNewParticles(&visibleBuildings);
		drawParticles();
	}

	///Draw ghost buildings
	if (!globalContainer->replaying) ghostManager.drawAll(viewportX, viewportY, localTeamNo);
	
	// if paused, tint the game area
	if (gamePaused)
	{
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH(), 0, 0, 0, 20);
		const char *s = Toolkit::getStringTable()->getString("[Paused]");
		int x = (globalContainer->gfx->getW()-globalContainer->menuFont->getStringWidth(s))>>1;
		globalContainer->gfx->drawString(x, globalContainer->gfx->getH()-80, globalContainer->menuFont, s);
	}

	// draw the panel
	globalContainer->gfx->setClipRect();
	drawPanel();

	// draw the minimap
	drawOptions = 0;
	//globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 0, 128, 128);
	//game.drawMiniMap(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 0, 128, 128, viewportX, viewportY, team, drawOptions);

	globalContainer->gfx->setClipRect();
	minimap.draw(localTeamNo, viewportX, viewportY, (globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)/32, globalContainer->gfx->getH()/32 );

	// draw the progress bar if this is a replay
	if (globalContainer->replaying) drawReplayProgressBar();
	
	// draw the top bar and other infos
	globalContainer->gfx->setClipRect();
	drawOverlayInfos();

	// draw menu if any
	if (inGameMenu)
	{
		globalContainer->gfx->setClipRect();
		drawInGameMenu();
	}

	// draw input box if any
	if (typingInputScreen)
	{
		globalContainer->gfx->setClipRect();
		drawInGameTextInput();
	}
	if (scrollableText)
		drawInGameScrollableText();
		
	// draw the hilight arrows
	for(int i=0; i<(int)arrowPositions.size(); ++i)
	{
		globalContainer->gfx->drawSprite(arrowPositions[i].x, arrowPositions[i].y, globalContainer->gamegui, arrowPositions[i].sprite);
		
	}
}

void GameGUI::checkWonConditions(void)
{
	if (hasEndOfGameDialogBeenShown || globalContainer->replaying)
		return;
	
	if (game.totalPrestigeReached && game.isPrestigeWinCondition())
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[Total prestige reached]"), true);
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->hasLost==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have lost]"), true);
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->hasWon==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			if(campaign!=NULL)
			{
				campaign->setCompleted(missionName);
			}
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have won]"), true);
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
}

void GameGUI::showEndOfReplayScreen()
{
	gamePaused = true;

	globalContainer->replaying = false;
	hasEndOfGameDialogBeenShown = true;
	
	minimap.setMinimapMode( Minimap::ShowFOW );

	inGameMenu=IGM_END_OF_GAME;
	gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[replay ended]"), true);
	miniMapPushed=false;
}

void GameGUI::executeOrder(boost::shared_ptr<Order> order)
{
	switch (order->getOrderType())
	{
		case ORDER_TEXT_MESSAGE :
		{
			boost::shared_ptr<MessageOrder> mo=static_pointer_cast<MessageOrder>(order);
			int sp=mo->sender;
			Uint32 messageOrderType=mo->messageOrderType;

			if (messageOrderType==MessageOrder::NORMAL_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(Color(230, 230, 230), FormatableString("%0 : %1").arg(game.players[sp]->name).arg(mo->getText()), true);
			}
			else if (messageOrderType==MessageOrder::PRIVATE_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(Color(99, 255, 242), FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[from:]")).arg(game.players[sp]->name).arg(mo->getText()), true);
				else if (sp==localPlayer)
				{
					Uint32 rm=mo->recepientsMask;
					int k;
					for (k=0; k<Team::MAX_COUNT; k++)
						if (rm==1)
						{
							addMessage(Color(99, 255, 242), FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[to:]")).arg(game.players[k]->name).arg(mo->getText()), true);
							break;
						}
						else
							rm=rm>>1;
					assert(k<Team::MAX_COUNT);
				}
			}
			else
				assert(false);
			
			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_VOICE_DATA:
		{
			boost::shared_ptr<OrderVoiceData> ov = static_pointer_cast<OrderVoiceData>(order);
			if (ov->recepientsMask & (1<<localPlayer))
				globalContainer->mix->addVoiceData(ov);
			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_PLAYER_QUIT_GAME :
		{
			int qp=order->sender;
			if (qp==localPlayer)
				isRunning=false;
			addMessage(Color(200, 200, 200), FormatableString(Toolkit::getStringTable()->getString("[%0 has left the game]")).arg(game.players[qp]->name), true);
			game.executeOrder(order, localPlayer);
		}
		break;
		
		case ORDER_MAP_MARK:
		{
			boost::shared_ptr<MapMarkOrder> mmo=static_pointer_cast<MapMarkOrder>(order);

			assert(game.teams[mmo->teamNumber]->teamNumber<game.mapHeader.getNumberOfTeams());
			if (game.teams[mmo->teamNumber]->allies & (game.teams[localTeamNo]->me))
				addMark(mmo);
		}
		break;
		case ORDER_PAUSE_GAME:
		{
			boost::shared_ptr<PauseGameOrder> pgo=static_pointer_cast<PauseGameOrder>(order);
			gamePaused=pgo->pause;
		}
		break;
		case ORDER_CREATE:
		{
			boost::shared_ptr<OrderCreate> pgo=static_pointer_cast<OrderCreate>(order);
			if(pgo->teamNumber == localTeamNo)
				ghostManager.removeBuilding(pgo->posX, pgo->posY);
			game.executeOrder(order, localPlayer);
		}
		break;
		default:
		{
			game.executeOrder(order, localPlayer);
		}
	}
}

bool GameGUI::loadFromHeaders(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI)
{
	init();
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName()));
	if (stream->isEndOfStream())
	{
		delete stream;
		stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName(true)));
		if(stream->isEndOfStream())
		{
			delete stream;
			stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName(false,true)));
			if(stream->isEndOfStream())
			{
				std::cerr << "GameGUI::loadFromHeaders() : error, can't open file " << mapHeader.getFileName() << ", " << mapHeader.getFileName(true) << " or " << mapHeader.getFileName(false,true) << std::endl;
				delete stream;
				return false;
			}
		}
	}
	
	bool res = load(stream, ignoreGUIData);
	delete stream;
	if (!res)
		return false;

    //Use the map header from the file, the one sent across the network is in the latest format version, where as the actual map
    //may be an older file version.
	//game.setMapHeader(mapHeader);
	if(setGameHeader)
		game.setGameHeader(gameHeader, saveAI);

	return true;
}

bool GameGUI::load(GAGCore::InputStream *stream, bool ignoreGUIData)
{
	init();

	bool result = game.load(stream);

	if (result == false)
	{
		std::cerr << "GameGUI::load : can't load game" << std::endl;
		return false;
	}
	defualtGameSaveName = game.mapHeader.getMapName();
	if (game.mapHeader.getIsSavedGame())
	{
		// load gui's specific infos
		stream->readEnterSection("GameGUI");

		///Load the data, but don't store it in local variables
		if(ignoreGUIData)
		{
			stream->readUint32("chatMask");
			stream->readSint32("localPlayer");
			stream->readSint32("localTeamNo");
			stream->readSint32("viewportX");
			stream->readSint32("viewportY");
			stream->readUint32("hiddenGUIElements");
			stream->readUint32("buildingsChoiceMask");
			stream->readUint32("flagsChoiceMask");
		}
		else
		{	
			chatMask = stream->readUint32("chatMask");

			localPlayer = stream->readSint32("localPlayer");
			localTeamNo = stream->readSint32("localTeamNo");

			viewportX = stream->readSint32("viewportX");
			viewportY = stream->readSint32("viewportY");

			hiddenGUIElements = stream->readUint32("hiddenGUIElements");
			Uint32 buildingsChoiceMask = stream->readUint32("buildingsChoiceMask");
			Uint32 flagsChoiceMask = stream->readUint32("flagsChoiceMask");
			
			// invert value if hidden
			for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
			{
				int id = IntBuildingType::shortNumberFromType(buildingsChoiceName[i]);
				buildingsChoiceState[i] = ((1<<id) & buildingsChoiceMask) != 0;
			}
			for (unsigned i=0; i<flagsChoiceState.size(); ++i)
			{
				int id = IntBuildingType::shortNumberFromType(flagsChoiceName[i]);
				flagsChoiceState[i] = ((1<<id) & flagsChoiceMask) != 0;
			}
		}
		
		if(game.mapHeader.getVersionMinor() >= 69)
			defaultAssign.load(stream, game.mapHeader.getVersionMinor());
		stream->readLeaveSection();
	}
	
	minimap.setGame(game);

	return true;
}

void GameGUI::save(GAGCore::OutputStream *stream, const char *name)
{
	// Game is can't be no more automatically generated
	game.save(stream, false, name);
	
	stream->writeEnterSection("GameGUI");
	stream->writeUint32(chatMask, "chatMask");
	stream->writeSint32(localPlayer, "localPlayer");
	stream->writeSint32(localTeamNo, "localTeamNo");
	stream->writeSint32(viewportX, "viewportX");
	stream->writeSint32(viewportY, "viewportY");
	stream->writeUint32(hiddenGUIElements, "hiddenGUIElements");
	Uint32 buildingsChoiceMask = 0;
	Uint32 flagsChoiceMask = 0;
	// save one if visible
	for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
	{
		if (buildingsChoiceState[i])
		{
			int id = IntBuildingType::shortNumberFromType(buildingsChoiceName[i]);
			buildingsChoiceMask |= (1<<id);
		}
	}
	for (unsigned i=0; i<flagsChoiceState.size(); ++i)
	{
		if (flagsChoiceState[i])
		{
			int id = IntBuildingType::shortNumberFromType(flagsChoiceName[i]);
			flagsChoiceMask |= (1<<id);
		}
	}
	stream->writeUint32(buildingsChoiceMask, "buildingsChoiceMask");
	stream->writeUint32(flagsChoiceMask, "flagsChoiceMask");
	defaultAssign.save(stream);
	stream->writeLeaveSection();
}

void GameGUI::drawButton(int x, int y, const char *caption, int r, int g, int b, bool doLanguageLookup)
{
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 12);
	globalContainer->gfx->drawFilledRect(x+17, y+3, 94, 10, r, g, b);

	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=Toolkit::getStringTable()->getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+17+((94-len)>>1), y+((16-h)>>1), globalContainer->littleFont, textToDraw);
}

void GameGUI::drawBlueButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	drawButton(x,y,caption,128,128,192,doLanguageLookup);
}

void GameGUI::drawRedButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	drawButton(x,y,caption,192,128,128,doLanguageLookup);
}

void GameGUI::drawTextCenter(int x, int y, const char *caption)
{
	const char *text;

	text=Toolkit::getStringTable()->getString(caption);
	int dec=(RIGHT_MENU_WIDTH-globalContainer->littleFont->getStringWidth(text))>>1;
	globalContainer->gfx->drawString(x+dec, y, globalContainer->littleFont, text);
}

void GameGUI::drawScrollBox(int x, int y, int value, int valueLocal, int act, int max)
{
	//scrollbar borders
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 9);

	//localBar
	int size=(valueLocal*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);
	
	//actualBar
	size=(act*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);
	
	globalContainer->gfx->setClipRect();
}

void GameGUI::drawXPProgressBar(int x, int y, int act, int max)
{
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);
	
	globalContainer->gfx->setClipRect(x+18, y, 92, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);
	
	globalContainer->gfx->setClipRect(x+18, y, (act*92)/max, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);
	
	globalContainer->gfx->setClipRect();
}

void GameGUI::cleanOldSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		game.selectedBuilding=NULL;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		game.selectedUnit=NULL;
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		toolManager.deactivateTool();
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		toolManager.deactivateTool();
	}
}

void GameGUI::setSelection(SelectionMode newSelMode, unsigned newSelection)
{
	if (selectionMode!=newSelMode)
	{
		cleanOldSelection();
		selectionMode=newSelMode;
	}

	if (selectionMode==BUILDING_SELECTION)
	{
		int id=Building::GIDtoID(newSelection);
		int team=Building::GIDtoTeam(newSelection);
		selection.building=game.teams[team]->myBuildings[id];
		game.selectedBuilding=selection.building;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		int id=Unit::GIDtoID(newSelection);
		int team=Unit::GIDtoTeam(newSelection);
		selection.unit=game.teams[team]->myUnits[id];
		game.selectedUnit=selection.unit;
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		selection.ressource=newSelection;
	}
}

void GameGUI::setSelection(SelectionMode newSelMode, void* newSelection)
{
	if (selectionMode!=newSelMode)
	{
		cleanOldSelection();
		selectionMode=newSelMode;
	}

	if (selectionMode==BUILDING_SELECTION)
	{
		selection.building=(Building*)newSelection;
		game.selectedBuilding=selection.building;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		selection.unit=(Unit*)newSelection;
		game.selectedUnit=selection.unit;
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		toolManager.activateBuildingTool((char*)(newSelection));
	}
}

void GameGUI::checkSelection(void)
{
	if ((selectionMode==BUILDING_SELECTION) && (game.selectedBuilding==NULL))
	{
		clearSelection();
	}
	else if ((selectionMode==UNIT_SELECTION) && (game.selectedUnit==NULL))
	{
		clearSelection();
	}
}


void GameGUI::iterateSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		Uint16 selectionGBID=selBuild->gid;
		assert(selBuild);
		assert(selectionGBID!=NOGBID);
		int pos=Building::GIDtoID(selectionGBID);
		int team=Building::GIDtoTeam(selectionGBID);
		int i=pos;
		if (team==localTeamNo)
		{
			while (i<pos+Building::MAX_COUNT)
			{
				i++;
				Building *b=game.teams[team]->myBuildings[i % Building::MAX_COUNT];
				if (b && b->typeNum==selBuild->typeNum)
				{
					setSelection(BUILDING_SELECTION, b);
					centerViewportOnSelection();
					break;
				}
			}
		}
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(toolManager.getBuildingName(), 0, false);
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=game.teams[localTeamNo]->myBuildings[i];
			if (b && b->typeNum==typeNum)
			{
				setSelection(BUILDING_SELECTION, b);
				centerViewportOnSelection();
				break;
			}
		}
	}
	else if (selectionMode == UNIT_SELECTION)
	{
		Unit * selUnit = selection.unit;
		assert(selUnit);
		Uint16 gid = selUnit->gid;
		/* to be safe should check if gid is valid here? */
		/* if looking at one of our pieces, continue with the next
			one of our pieces of same type, otherwise start at the
			beginning of our pieces of that type. */
		Sint32 id = ((Unit::GIDtoTeam(gid) == localTeamNo) ? Unit::GIDtoID(gid) : 0);
		id %= Unit::MAX_COUNT; /* just in case! */
		// std::cerr << "starting id: " << id << std::endl;
		Sint32 i = id;
		while (1)
		{
			i = ((i + 1) % Unit::MAX_COUNT);
			if (i == id) break;
			// std::cerr << "trying id: " << i << std::endl;
			Unit * u = game.teams[localTeamNo]->myUnits[i];
			if (u && (u->typeNum == selUnit->typeNum))
			{
				// std::cerr << "found id: " << i << std::endl;
				setSelection(UNIT_SELECTION, u);
				centerViewportOnSelection();
				break;
			}
		}
	}
}

void GameGUI::centerViewportOnSelection(void)
{
	if ((selectionMode==BUILDING_SELECTION) || (selectionMode==UNIT_SELECTION))
	{
		Sint32 posX, posY;
		if (selectionMode==BUILDING_SELECTION)
		{
			Building* b=selection.building;
			//assert (selBuild);
			//Building *b=game.teams[Building::GIDtoTeam(selectionGBID)]->myBuildings[Building::GIDtoID(selectionGBID)];
			assert(b);
			posX = b->getMidX();
			posY = b->getMidY();
		}
		else if (selectionMode==UNIT_SELECTION)
		{
			Unit * u = selection.unit;
			assert (u);
			posX = u->posX;
			posY = u->posY;
		}
		
		/* It violates good abstraction principles that we know here
			that the size of the right panel is RIGHT_MENU_WIDTH pixels, and that each
			map cell is 32 pixels.  This information should be
			abstracted. */
		
		int oldViewportX = viewportX;
		int oldViewportY = viewportY;
		
		viewportX = posX - ((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
		viewportY = posY - ((globalContainer->gfx->getH())>>6);
		viewportX = viewportX & game.map.getMaskW();
		viewportY = viewportY & game.map.getMaskH();
		
		moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
	}
}


void GameGUI::dumpUnitInformation(void)
{
	if(game.selectedUnit != NULL)
	{
		Unit* unit = game.selectedUnit;
		std::cout<<"unit->posx = "<<unit->posX<<std::endl;
		std::cout<<"unit->posy = "<<unit->posY<<std::endl;
		std::cout<<"unit->gid = "<<unit->gid<<std::endl;
		std::cout<<"unit->medical = "<<unit->medical<<std::endl;
		std::cout<<"unit->activity = "<<unit->activity<<std::endl;
		std::cout<<"unit->displacement = "<<unit->displacement<<std::endl;
		std::cout<<"unit->movement = "<<unit->movement<<std::endl;
		std::cout<<"unit->action = "<<unit->action<<std::endl;
		if(unit->targetBuilding)
			std::cout<<"unit->targetBuilding->gid = "<<unit->targetBuilding->gid<<std::endl;
	}
}


void GameGUI::enableBuildingsChoice(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
			buildingsChoiceState[i] = true;
	}
}

void GameGUI::disableBuildingsChoice(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
			buildingsChoiceState[i] = false;
	}
}

bool GameGUI::isBuildingEnabled(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
                  return buildingsChoiceState[i];
	}
	assert (false);
}

void GameGUI::enableFlagsChoice(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
			flagsChoiceState[i] = true;
	}
}

void GameGUI::disableFlagsChoice(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
			flagsChoiceState[i] = false;
	}
}

bool GameGUI::isFlagEnabled(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
                  return flagsChoiceState[i];
	}
        assert (false);
}

void GameGUI::enableGUIElement(int id)
{
	hiddenGUIElements &= ~(1<<id);
}

void GameGUI::disableGUIElement(int id)
{
	if (globalContainer->replaying) return;

	hiddenGUIElements |= (1<<id);
	if (displayMode==id)
		nextDisplayMode();
}

void GameGUI::showScriptText(const std::string &text)
{
	scriptText = text;
	scriptTextUpdated = true;
}

void GameGUI::showScriptTextTr(const std::string &text, const std::string &lang)
{
	if (lang == globalContainer->settings.language)
		showScriptText(text);
}

void GameGUI::hideScriptText()
{
	scriptText.clear();
}

void GameGUI::setCpuLoad(int s)
{
	smoothedCPULoad[smoothedCPUPos]=s;
	smoothedCPUPos=(smoothedCPUPos+1) % SMOOTHED_CPU_SIZE;
}



void GameGUI::setCampaignGame(Campaign& campaign, const std::string& missionName)
{
	this->campaign=&campaign;
	this->missionName=missionName;
}



void GameGUI::updateHilightInGame()
{
	game.hilightUnitType = 0;
	if(hilights.find(HilightWorkers) != hilights.end())
	{
		game.hilightUnitType |= 1<<WORKER;
	}
	if(hilights.find(HilightExplorers) != hilights.end())
	{
		game.hilightUnitType |= 1<<EXPLORER;
	}
	if(hilights.find(HilightWarriors) != hilights.end())
	{
		game.hilightUnitType |= 1<<WARRIOR;
	}
	
	game.hilightBuildingType = 0;
	
	for(int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		if(hilights.find(HilightBuildingOnMap + i) != hilights.end())
		{
			game.hilightBuildingType |= 1<<(i);
		}
	}
}



void GameGUI::setMultiLine(const std::string &input, std::vector<std::string> *output, std::string indent)
{
	unsigned pos = 0;
	int length = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64;
	
	std::string lastWord;
	std::string lastLine;
	std::string ninput=input;
	if(ninput[ninput.length()-1] != ' ')
		ninput += " ";
	
	while (pos<ninput.length())
	{
		if (ninput[pos] == ' ')
		{
			int actLineLength = globalContainer->standardFont->getStringWidth(lastLine.c_str());
			int actWordLength = globalContainer->standardFont->getStringWidth(lastWord.c_str());
			int spaceLength = globalContainer->standardFont->getStringWidth(" ");
			if (actWordLength+actLineLength+spaceLength < length)
			{
				if (lastLine.length())
					lastLine += " ";
				lastLine += lastWord;
				lastWord.clear();
			}
			else
			{
				output->push_back(lastLine);
				lastLine = indent+lastWord;
				lastWord.clear();
			}
		}
		else
		{
			lastWord += ninput[pos];
		}
		pos++;
	}
	if (lastLine.length())
		lastLine += " ";
	lastLine += lastWord;
	if (lastLine.length())
		output->push_back(lastLine);
}

void GameGUI::addMessage(const GAGCore::Color& color, const std::string &msgText, bool chat)
{	
	//Split into one per line
	std::vector<std::string> messages;
	globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_BOLD, 255, 255, 255));
	setMultiLine(msgText, &messages);
	globalContainer->standardFont->popStyle();

	///Add each line as a seperate message to the message manager.
	///Must be done backwards to appear in the right order
	for (int i=messages.size()-1; i>=0; i--)
	{
		if(!chat)
			messageManager.addGameMessage(InGameMessage(messages[i], color));
		else
			messageManager.addChatMessage(InGameMessage(messages[i], color, 16000));
	}
}

void GameGUI::addMark(shared_ptr<MapMarkOrder>mmo)
{
	markManager.addMark(Mark(mmo->x, mmo->y, game.teams[mmo->teamNumber]->color));
}


void GameGUI::flushScrollWheelOrders()
{
	SDLMod modState = SDL_GetModState();
	if (scrollWheelChanges!=0 && selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		if ((selBuild->owner->teamNumber==localTeamNo) &&
			(selBuild->buildingState==Building::ALIVE))
		{
			if ((selBuild->type->maxUnitWorking) &&
                                            (!globalContainer->settings.scrollWheelEnabled ? (modState & KMOD_CTRL) : !(SDL_GetModState()&KMOD_SHIFT)))
			{
				selBuild->maxUnitWorkingLocal+=scrollWheelChanges;
				int nbReq=selBuild->maxUnitWorkingLocal=std::min(20, std::max(0, (selBuild->maxUnitWorkingLocal)));
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
				defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
			}
			else if ((selBuild->type->defaultUnitStayRange) &&
				(SDL_GetModState()&KMOD_SHIFT))
			{
				selBuild->unitStayRangeLocal+=scrollWheelChanges;
				int nbReq=selBuild->unitStayRangeLocal=std::min(selBuild->type->maxUnitStayRange, std::max(0, (selBuild->unitStayRangeLocal)));
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
			}
		}
	}
	scrollWheelChanges=0;
}

void GameGUI::generateNewParticles(std::set<Building*> *visibleBuildings)
{
	for (std::set<Building*>::iterator it = visibleBuildings->begin(); it != visibleBuildings->end(); ++it)
	{
		Building* building = *it;
		BuildingType* type = building->type;
		int x, y;
		game.map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);
		
		if (!type->isBuildingSite)
		{
			// damaged building smoke
			float hpRatio = (float)building->hp / (float)type->hpMax;
			if (
				(hpRatio < 0.2 && ((game.stepCounter & 0x1) == 0)) ||
				(hpRatio < 0.5 && ((game.stepCounter & 0x3) == 0))
			)
			{
				Particle* p = new Particle;
				p->x = x + type->width * 16;
				p->y = y + type->height * 16;
				if (hpRatio < 0.2)
				{
					p->vx = 0.5f - (float)rand() / (float)RAND_MAX;
					p->vy = - 3.f * (float)rand() / (float)RAND_MAX;
				}
				else
				{
					p->vx = 0.3f - (float)rand() / (float)RAND_MAX;
					p->vy = - 1.8f * (float)rand() / (float)RAND_MAX;
				}
				p->ax = 0.f;
				p->ay = -0.01f;
				p->age = 0;
				p->lifeSpan = 50;
				p->startImg = 0;
				p->endImg = 2;
				p->color = building->owner->color;
				particles.insert(p);
			}
			
			// turret firing
			if (building->lastShootStep != 0xFFFFFFFF)
			{
				if ((game.stepCounter - building->lastShootStep < 6) && (game.stepCounter % 2 == 0))
				{
					float norm = building->lastShootSpeedX * building->lastShootSpeedX + building->lastShootSpeedY * building->lastShootSpeedY;
					float w2 = type->width * 16;
					float h2 = type->height * 16;
					float dx = (building->lastShootSpeedX * w2) / sqrt(norm);
					float dy = (building->lastShootSpeedY * h2) / sqrt(norm);
					Particle* p = new Particle;
					p->x = x + w2 + dx;
					p->y = y + h2 + dy;
					p->vx = 0.3f - (float)rand() / (float)RAND_MAX;
					p->vy = - 1.2f * (float)rand() / (float)RAND_MAX;
					p->ax = 0.f;
					p->ay = -0.02f;
					p->age = 0;
					p->lifeSpan = 30;
					p->startImg = 0;
					p->endImg = 2;
					p->color = building->owner->color;
					particles.insert(p);
				}
			}
		}
	}
}

void GameGUI::moveParticles(int oldViewportX, int viewportX, int oldViewportY, int viewportY)
{
	if ((viewportX==oldViewportX) && (viewportY==oldViewportY))
		return;
	
	int dx = viewportX - oldViewportX;
	if (dx > game.map.getW() / 2)
		dx -= game.map.getW();
	else if (dx < -game.map.getW() / 2)
		dx += game.map.getW();
	
	int dy = viewportY - oldViewportY;
	if (dy > game.map.getH() / 2)
		dy -= game.map.getH();
	else if (dy < -game.map.getH() / 2)
		dy += game.map.getH();
	
	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); ++it)
	{
		Particle* p = *it;
		p->x -= dx * 32;
		p->y -= dy * 32;
	}
}
