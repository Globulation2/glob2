// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <sstream>
#include <iostream>
#include <algorithm>
#include <optional>

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
#include "GameGUIInternal.h"
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
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "config.h"
#include "Order.h"

#include <SDL_keycode.h>

using std::shared_ptr;
using std::static_pointer_cast;

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
	         (globalContainer->runNoX ? 0 : globalContainer->gfx->getW()), // width of the screen
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

Sint32 GameGUI::displayedPosX(const Building& b) const { return ::displayedPosX(buildingGuiState, b); }
Sint32 GameGUI::displayedPosY(const Building& b) const { return ::displayedPosY(buildingGuiState, b); }
Sint32 GameGUI::displayedMaxUnitWorking(const Building& b) const { return ::displayedMaxUnitWorking(buildingGuiState, b); }
Sint32 GameGUI::displayedUnitStayRange(const Building& b) const { return ::displayedUnitStayRange(buildingGuiState, b); }

void GameGUI::reconcileBuildingGuiState(const std::shared_ptr<Order>& order)
{
	// When an order executes that updates the authoritative Building state,
	// drop the corresponding pending shadow so the display falls back to
	// authoritative. For the LOCAL player's own orders during live play we
	// leave pending alone — the user may have already queued a newer change
	// past the one that just landed, and we want the display to track the
	// latest user intent, not flicker back to the now-stale authoritative
	// value. Replays clear pending unconditionally because every order
	// represents the authoritative timeline.
	const bool replaying = globalContainer->replaying;
	switch (order->getOrderType())
	{
		case ORDER_MOVE_FLAG:
		{
			auto omf = std::static_pointer_cast<OrderMoveFlag>(order);
			if (omf->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omf->gid);
				if (it != buildingGuiState.end())
				{
					it->second.pendingPosX.reset();
					it->second.pendingPosY.reset();
				}
			}
			break;
		}
		case ORDER_MODIFY_BUILDING:
		{
			auto omb = std::static_pointer_cast<OrderModifyBuilding>(order);
			if (omb->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omb->gid);
				if (it != buildingGuiState.end())
					it->second.pendingMaxUnitWorking.reset();
			}
			break;
		}
		case ORDER_MODIFY_FLAG:
		{
			auto omf = std::static_pointer_cast<OrderModifyFlag>(order);
			if (omf->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omf->gid);
				if (it != buildingGuiState.end())
					it->second.pendingUnitStayRange.reset();
			}
			break;
		}
		default:
			break;
	}
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
	assert(game.gameHeader.getNumberOfPlayers()<=Team::MAX_COUNT);
	assert(localTeamNo<game.mapHeader.getNumberOfTeams());

	localTeam = game.teams[localTeamNo];
	assert(localTeam);
	teamStats = &localTeam->stats;

	// Mirror the local-team identity onto Map so sim code can consult it without
	// reaching into the GUI layer.
	game.map.setLocalTeam(localTeamNo);

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
	if ((displayedPosX(*selBuild)!=posX)
		||(displayedPosY(*selBuild)!=posY)
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
		BuildingGuiState& s = pendingFor(gid);
		s.pendingPosX = posX;
		s.pendingPosY = posY;
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
		else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q && SDL_GetModState() & KMOD_GUI)
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
		else if (event.type==SDL_WINDOWEVENT)
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
	while(std::optional<GameEvent> gevent = localTeam->getEvent())
	{
		addMessage(gevent->formatColor(), gevent->formatMessage(game), false);
		eventGoPosX = gevent->getX();
		eventGoPosY = gevent->getY();
		eventGoType = gevent->getEventType();
	}

	// voice step
	std::shared_ptr<OrderVoiceData> orderVoiceData;
	while ((orderVoiceData = globalContainer->voiceRecorder->getNextOrder()) != NULL)
	{
		orderVoiceData->recepientsMask = chatMask ^ (chatMask & (1<<localPlayer));
		orderQueue.push_back(orderVoiceData);
	}

	// TODO: die with SGSL
	// Check if the text being displayed has changed, and if it has, add it to the history box
	if(game.sgslScript.isTextShown && game.sgslScript.textShown != previousSGSLText)
	{
		//Split into one per line
		std::vector<std::string> messages;
		setMultiLine(game.sgslScript.textShown, &messages, "    ");

		///Add each line as a seperate message to the message manager.
		///Must be done backwards to appear in the right order
		for (int i=messages.size()-1; i>=0; i--)
		{
			messageManager.addChatMessage(InGameMessage(messages[i], Color(255, 255, 255), 0));
		}

		previousSGSLText = game.sgslScript.textShown;
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

	std::shared_ptr<Order> order = toolManager.getOrder();
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
		const std::string name = Toolkit::getStringTable()->getString("[auto save]");
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

std::shared_ptr<Order> GameGUI::getOrder(void)
{
	std::shared_ptr<Order> order;
	if (orderQueue.size()==0)
		order=shared_ptr<Order>(new NullOrder());
	else
	{
		order=orderQueue.front();
		orderQueue.pop_front();
	}
	return order;
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

	if (!hasEndOfGameDialogBeenShown)
	{
		hasEndOfGameDialogBeenShown = true;

		inGameMenu=IGM_END_OF_GAME;
		gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[replay ended]"), true);
		miniMapPushed=false;
	}
}

void GameGUI::executeOrder(std::shared_ptr<Order> order)
{
	switch (order->getOrderType())
	{
		case ORDER_TEXT_MESSAGE :
		{
			std::shared_ptr<MessageOrder> mo=static_pointer_cast<MessageOrder>(order);
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
			std::shared_ptr<OrderVoiceData> ov = static_pointer_cast<OrderVoiceData>(order);
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
			std::shared_ptr<MapMarkOrder> mmo=static_pointer_cast<MapMarkOrder>(order);

			assert(game.teams[mmo->teamNumber]->teamNumber<game.mapHeader.getNumberOfTeams());
			if (game.teams[mmo->teamNumber]->allies & (game.teams[localTeamNo]->me))
				addMark(mmo);
		}
		break;
		case ORDER_PAUSE_GAME:
		{
			std::shared_ptr<PauseGameOrder> pgo=static_pointer_cast<PauseGameOrder>(order);
			gamePaused=pgo->pause;
		}
		break;
		case ORDER_CREATE:
		{
			std::shared_ptr<OrderCreate> pgo=static_pointer_cast<OrderCreate>(order);
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
	reconcileBuildingGuiState(order);
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

void GameGUI::save(GAGCore::OutputStream *stream, const std::string name)
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

void GameGUI::setMultiLine(const std::string &input, std::vector<std::string> *output, std::string indent)
{
	unsigned pos = 0;
	int length = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64;

	std::string lastWord;
	std::string lastLine;
	std::string ninput=input;
	if(!ninput.empty() && ninput.back() != ' ')
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
	SDL_Keymod modState = SDL_GetModState();
	if (scrollWheelChanges!=0 && selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		if ((selBuild->owner->teamNumber==localTeamNo) &&
			(selBuild->buildingState==Building::ALIVE))
		{
			if ((selBuild->type->maxUnitWorking) &&
                                            (!globalContainer->settings.scrollWheelEnabled ? (modState & KMOD_CTRL) : !(SDL_GetModState()&KMOD_SHIFT)))
			{
				const int requested = std::min(20, std::max(0, displayedMaxUnitWorking(*selBuild) + scrollWheelChanges));
				pendingFor(selBuild->gid).pendingMaxUnitWorking = requested;
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, requested)));
				defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, requested);
			}
			else if ((selBuild->type->defaultUnitStayRange) &&
				(SDL_GetModState()&KMOD_SHIFT))
			{
				const int requested = std::min((int)selBuild->type->maxUnitStayRange, std::max(0, displayedUnitStayRange(*selBuild) + scrollWheelChanges));
				pendingFor(selBuild->gid).pendingUnitStayRange = requested;
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, requested)));
			}
		}
	}
	scrollWheelChanges=0;
}
