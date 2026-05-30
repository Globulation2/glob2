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

void GameGUI::moveFlag(int mx, int my, bool drop)
{
	if (globalContainer->replaying) return;

	int posX, posY;
	Building* selBuild=selectionBuilding();
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
			Building* selBuild=selectionBuilding();
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
		publishMessageHistoryLines(game.sgslScript.textShown, HistoryList::Chat,
			Color(255, 255, 255), kHistoryOnlyTimeoutMs, kScriptTextContinuationIndent);
		previousSGSLText = game.sgslScript.textShown;
	}

	// Check if the text being displayed has changed, and if it has, add it to the history box
	if (scriptTextUpdated)
	{
		publishMessageHistoryLines(scriptText, HistoryList::Chat,
			Color(255, 255, 255), kHistoryOnlyTimeoutMs, kScriptTextContinuationIndent);
		scriptTextUpdated = false;
	}

	// music step
	GameMusicEvents musicEvents;
	musicEvents.unitUnderAttack       = localTeam->wasRecentEvent(GEUnitUnderAttack);
	musicEvents.unitLostConversion    = localTeam->wasRecentEvent(GEUnitLostConversion);
	musicEvents.unitGainedConversion  = localTeam->wasRecentEvent(GEUnitGainedConversion);
	musicEvents.buildingUnderAttack   = localTeam->wasRecentEvent(GEBuildingUnderAttack);
	musicEvents.buildingCompleted     = localTeam->wasRecentEvent(GEBuildingCompleted);
	if (auto nextTrack = musicController.tick(musicEvents))
		globalContainer->mix->setNextTrack(*nextTrack, true);

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
			inGameMenu=IGM_OBJECTIVES;
			gameMenuScreen.reset(new InGameObjectivesScreen(this, true));
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

void GameGUI::checkWonConditions(void)
{
	if (hasEndOfGameDialogBeenShown || globalContainer->replaying)
		return;

	if (game.totalPrestigeReached && game.isPrestigeWinCondition())
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen.reset(new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[Total prestige reached]"), true));
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->hasLost==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen.reset(new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have lost]"), true));
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
			gameMenuScreen.reset(new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have won]"), true));
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
		gameMenuScreen.reset(new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[replay ended]"), true));
		miniMapPushed=false;
	}
}

void GameGUI::flushScrollWheelOrders()
{
	SDL_Keymod modState = SDL_GetModState();
	if (scrollWheelChanges!=0 && selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selectionBuilding();
		if ((selBuild->owner->teamNumber==localTeamNo) &&
			(selBuild->buildingState==Building::ALIVE))
		{
			if ((selBuild->type->maxUnitWorking) &&
                                            (!globalContainer->settings.scrollWheelEnabled ? (modState & KMOD_CTRL) : !(SDL_GetModState()&KMOD_SHIFT)))
			{
				const int requested = std::min((int)MAX_UNIT_WORKING, std::max(0, displayedMaxUnitWorking(*selBuild) + scrollWheelChanges));
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
