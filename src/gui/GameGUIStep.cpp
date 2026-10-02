// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <iostream>
#include <algorithm>
#include <optional>

#include <BackgroundFileWriter.h>
#include <FileManager.h>
#include <SDLCompat.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>

#include "EngineTiming.h"
#include "Game.h"
#include "GameGUI.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "LoadSaveDialog.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "WinningConditions.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "SoundMixer.h"
#include "VoiceRecorder.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include <glob2/BuildConfig.h>
#include "Order.h"

#include <SDL_keycode.h>

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::moveFlag(int mx, int my, bool drop)
{
	if (globalContainer->isViewingGame()) return;

	int posX, posY;
	Building* selBuild=selectionBuilding();
	game.map.cursorToBuildingPos(mx, my, selBuild->type->width, selBuild->type->height, &posX, &posY, viewportX, viewportY);
	if ((displayedPosX(*selBuild)!=posX)
		||(displayedPosY(*selBuild)!=posY)
		||(drop && (selectionPushedPosX!=posX || selectionPushedPosY!=posY)))
		queueFlagMove(*selBuild, posX, posY, drop);
}

void GameGUI::queueFlagMove(Building &flag, int x, int y, bool drop)
{
	Uint16 gid=flag.gid;
	shared_ptr<OrderMoveFlag> oms(new OrderMoveFlag(gid, x, y, drop));
	// First, we check if another move of the same flag is already in the "orderQueue".
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
	s.pendingPosX = x;
	s.pendingPosY = y;
}

void GameGUI::dragStep(int mx, int my, int button)
{
    if (mapPanPushed) return;
    if (torusView.active()) {
        if (!torusPointerDown || !torusMapPointer(mx, my, mx, my)) return;
    }

	if(zoomControlPushed)return;
	/* We used to use SDL_GetMouseState, like the following
		commented-out code, but that was buggy and prevented
		dragging from correctly going through intermediate cells.
		It is vital to use the mouse position and button status as
		it was at the time in the middle of the event stream, not
		as it is now.  So instead we make sure the correct data is
		passed to us as a parameter. */
	if ((button&SDL_BUTTON(1)) && (torusView.active() || mx<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH))
	{
		if (!torusView.active() && (!camera.contains(mx,my) || my<16)) return;
		if (!torusView.active()) {mx=mapMouseX(mx);my=mapMouseY(my);}
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
}

/* We need to keep track of the last recorded mouse position for use
   in drag steps.  We can't simply use SDL_GetMouseState to get this
   information, because we need the information as it was in the
   middle of the event stream.  (There may be many later events we
   have not yet processed.) */
void GameGUI::step(void)
{
    std::vector<SDL_Event> events;
    SDL_Event event;
    while (GAGCore::GraphicContext::pollEvent(&event)) events.push_back(event);
    step(events, SDL_GetTicks64());
}

void GameGUI::step(const std::vector<SDL_Event>& events, Uint64 now)
{
    consumeClientEvents();
    if (inGameMenu == IGM_SAVE && gameMenuScreen &&
        static_cast<LoadSaveDialog*>(gameMenuScreen.get())->pollPersistence())
        closeDialog();
    if (auto *dialog = activeDialog())
        dialog->update(Uint32(now));
    // A dialog can finish without an SDL event (browser-native text editing
    // submits through the host bridge); act on its result every frame.
    if (gameMenuScreen && gameMenuScreen->finished())
        processGameMenu(nullptr);
    if (typingInputScreen && typingInputScreen->finished())
    {
        SDL_Event poll{};
        poll.type = SDL_USEREVENT;
        processTypingInput(&poll);
    }
	PERF_SCOPE_TIME(GUI);
	SDL_Event mouseMotionEvent;
	bool wasMouseMotion=false;

	int oldMouseMapX = -1, oldMouseMapY = -1; // hopefully the values here will never matter
	// we get all pending events but for mouse motion we only keep the last one
	for (auto event : events)
	{
		GAGCore::GraphicContext::translateMouseEvent(&event);
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
				game.map.cursorToBuildingPos (mapMouseX(lastMouseX), mapMouseY(lastMouseY), 1, 1, &mouseMapX, &mouseMapY, viewportX, viewportY);
			}
			else
			{
				/* We interpret all locations outside the
					viewport as being equivalent, and
					distinct from any map location. */
				mouseMapX = -1;
				mouseMapY = -1;
			}
			/* Make sure dragging does not skip over map cells by
				processing the old stored event rather than throwing
				it away. */
			if (wasMouseMotion
				&& (lastMouseButtonState & SDL_BUTTON(1)) // are we dragging? (should not be hard-coding this condition but should be abstract somehow)
				&& (mapPanPushed || (mouseMapX != oldMouseMapX)
					|| (mouseMapY != oldMouseMapY))
			)
			{
				processEvent(&mouseMotionEvent);
			}
			oldMouseMapX = mouseMapX;
			oldMouseMapY = mouseMapY;
			mouseMotionEvent=event;
			wasMouseMotion=true;
		}
#		ifdef USE_OSX
		else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q && (event.key.keysym.mod & KMOD_GUI))
		{
			isRunning=false;
			exitGlobCompletely=true;
		}
#		endif
#		ifdef USE_WIN32
		else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F4 && (event.key.keysym.mod & KMOD_ALT))
		{
			isRunning=false;
			exitGlobCompletely=true;
		}
#		endif
		else if ((event.type == SDL_MOUSEBUTTONDOWN) || (event.type == SDL_MOUSEBUTTONUP))
		{
            if (wasMouseMotion) { processEvent(&mouseMotionEvent); wasMouseMotion = false; }
            if (event.button.button > 0 && event.button.button <= 32) {
                const Uint32 mask = SDL_BUTTON(event.button.button);
                if (event.type == SDL_MOUSEBUTTONDOWN) lastMouseButtonState |= mask;
                else lastMouseButtonState &= ~mask;
            }
			lastMouseX = event.button.x;
			lastMouseY = event.button.y;
			processEvent (&event);
		}
		else if (event.type==SDL_WINDOWEVENT)
		{
            if (wasMouseMotion) { processEvent(&mouseMotionEvent); wasMouseMotion = false; }
            processEvent(&event);
		}
		else
		{
			processEvent(&event);
		}
	}
	if (wasMouseMotion)
		processEvent(&mouseMotionEvent);


	int oldViewportX = viewportX;
	int oldViewportY = viewportY;

	viewportX += game.map.getW();
	viewportY += game.map.getH();
	// Continuous scrolling keeps its normal 25 Hz cadence at every game speed.

	if (now < lastViewportStep) lastViewportStep = now;
	const unsigned viewportSteps=std::min<Uint64>((now-lastViewportStep)/GAME_TICK_MS, 5);
	if(viewportSteps)
		lastViewportStep=now-(now-lastViewportStep)%GAME_TICK_MS;
	for(unsigned i=0; i<viewportSteps; ++i)
	{
		handleKeyAlways();
        updateCamera();
        camera.originX+=viewportSpeedX*32/camera.zoom;
        camera.originY+=viewportSpeedY*32/camera.zoom;
        camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
	}
	if (touch) touch->advanceScroll(now);
	viewportX &= game.map.getMaskW();
	viewportY &= game.map.getMaskH();

	updateCamera();
	// Pushed every frame rather than at press and release: several paths clear
	// panPushed, and this way the two cannot drift apart.
	torusView.setPanHeld(panPushed);
	if ((viewportX!=oldViewportX) || (viewportY!=oldViewportY))
	{
		if (inputState.hasFocus()) dragStep(lastMouseX, lastMouseY, lastMouseButtonState);
		viewportChanged(oldViewportX, viewportX, oldViewportY, viewportY);
	}

	assert(localTeam);
	// The simulation forwards GameEvents through ClientEvents; show the ones
	// for the team being viewed, oldest first.
	const int viewedTeam = localTeam->teamNumber;
	if (viewedTeam >= 0 && viewedTeam < Team::MAX_COUNT)
	{
		auto &teamEvents = pendingTeamEvents[viewedTeam];
		while (!teamEvents.empty())
		{
			const GameEvent gevent = std::move(teamEvents.front());
			teamEvents.pop_front();
			addMessage(gevent.formatColor(), gevent.formatMessage(game), false);
			eventGoPosX = gevent.getX();
			eventGoPosY = gevent.getY();
			eventGoType = gevent.getEventType();
		}
	}

	// voice step
	std::shared_ptr<OrderVoiceData> orderVoiceData;
	while ((orderVoiceData = globalContainer->voiceRecorder->getNextOrder()) != NULL)
	{
		orderVoiceData->recipientsMask = chatMask ^ (chatMask & (Team::teamNumberToMask(localPlayer)));
		orderQueue.push_back(orderVoiceData);
	}

	// TODO: die with SGSL
	// Check if the text being displayed has changed, and if it has, add it to the history box
	if(game.legacyScriptActive() && game.sgslScript.isTextShown && game.sgslScript.textShown != previousSGSLText)
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
	// Team::wasRecentEvent as of the last simulated tick (ClientEvents pulse).
	const ClientEvents::TickPulse pulse = clientEvents.pulse();
	auto recent = [&](GameEventType type)
	{
		return pulse.valid && viewedTeam >= 0 && viewedTeam < Team::MAX_COUNT
			&& pulse.recentEvents[viewedTeam][type];
	};
	GameMusicEvents musicEvents;
	musicEvents.unitUnderAttack       = recent(GEUnitUnderAttack);
	musicEvents.unitLostConversion    = recent(GEUnitLostConversion);
	musicEvents.unitGainedConversion  = recent(GEUnitGainedConversion);
	musicEvents.buildingUnderAttack   = recent(GEBuildingUnderAttack);
	musicEvents.buildingCompleted     = recent(GEBuildingCompleted);
	if (auto nextTrack = musicController.tick(musicEvents))
		globalContainer->mix->setNextTrack(*nextTrack, true);

	std::shared_ptr<Order> order = toolManager.getOrder();
	while(order)
	{
		orderQueue.push_back(order);
		order = toolManager.getOrder();
	}

	///This shows the mission briefing at the beginning of the mission
	if(game.stepCounter == 12)
	{
		if(game.missionBriefing != "")
		{
			openDialog(IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(this, true));
		}
	}

	// Overlay maps are computed during scene extraction (SceneExtractor), from the
	// overlay drawAll publishes in clientRequests.

	// do we have won or lost conditions
	checkWonConditions();

	if (game.anyPlayerWaited)
		anyPlayerWaitedTimeFor++;
	else
		anyPlayerWaitedTimeFor = 0;
}

void GameGUI::syncStep(void)
{
	assert(localTeam);

	// Faster presets run more ticks per second, so they wait proportionally more ticks.
	int stepMs = GAME_TICK_MS;
	if (canChangeGameSpeed())
		stepMs = (globalContainer->replaying && globalContainer->replayFastForward)
			? REPLAY_FAST_FORWARD_MS : globalContainer->settings.getGameSpeedStepDuration();
	const Sint64 autosaveInterval = AUTOSAVE_INTERVAL_TICKS * GAME_TICK_MS / std::max(stepMs, 1);
	// Counting from the last save also keeps a paused game from saving every frame.
	const bool autosaveDue = globalContainer->settings.autosaveGames && (lastAutosaveStep < 0
		? game.stepCounter % AUTOSAVE_INTERVAL_TICKS == AUTOSAVE_PHASE_TICKS
		: static_cast<Sint64>(game.stepCounter) - lastAutosaveStep >= autosaveInterval);
	if (autosaveDue)
	{
		lastAutosaveStep = game.stepCounter;
		autosave();
	}
}

void GameGUI::autosave()
{
	const std::string name = Toolkit::getStringTable()->getString("[auto save]");
	// Serialize between ticks into memory sized from the previous autosave;
	// autosaveWriter's thread hashes the snapshot and does the disk write.
	auto *memory = new MemoryStreamBackend();
	memory->reserve(lastAutosaveSize + lastAutosaveSize / 8);
	BinaryOutputStream stream(memory);
	DeferredGameSHA1 sha1;
	save(&stream, name, &sha1);
	std::string contents = memory->takeContents();
	lastAutosaveSize = contents.size();
	if (!autosaveWriter)
		autosaveWriter = std::make_unique<BackgroundFileWriter>(Toolkit::getFileManager());
	autosaveWriter->write(glob2GzipWritePath(glob2NameToFilename("games", name, "game")), std::move(contents),
		[sha1 = std::move(sha1)](std::string& bytes) { sha1.apply(bytes); }, true);
}

void GameGUI::waitForAutosave()
{
	if (autosaveWriter)
		autosaveWriter->waitUntilIdle();
}

void GameGUI::checkWonConditions(void)
{
	if (hasEndOfGameDialogBeenShown || globalContainer->replaying)
		return;

    if(globalContainer->liveSpectating) {
        for(int i=0;i<game.teamsCount();++i) if(game.teams[i]->hasWon && inGameMenu==IGM_NONE) {
            // A tie at the top across alliances is a draw, not this team's win.
            const bool drawn = isGameDrawn(&game);
            openDialog(IGM_END_OF_GAME, std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString(drawn ? "[game draw]" : "[Match finished]"), true, game.teams[i]->color, !drawn));
            hasEndOfGameDialogBeenShown=true;
            miniMapPushed=false;
            break;
        }
        return;
    }

	if (game.totalPrestigeReached && game.isPrestigeWinCondition())
	{
		if (inGameMenu==IGM_NONE)
		{
			const bool drawn = classifyTeamOutcome(&game, localTeamNo) == TeamOutcome::Draw;
			openDialog(IGM_END_OF_GAME, std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString(drawn ? "[game draw]" : "[Total prestige reached]"), true, localTeam->color, localTeam->hasWon && !drawn));
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->hasLost==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			openDialog(IGM_END_OF_GAME, std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString("[you have lost]"), true, localTeam->color, false));
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->hasWon==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			// Campaign progression follows the engine's won flag; a draw only
			// changes the words the player sees.
			if(campaign!=NULL)
			{
				campaign->setCompleted(missionName);
			}
			const bool drawn = classifyTeamOutcome(&game, localTeamNo) == TeamOutcome::Draw;
			openDialog(IGM_END_OF_GAME, std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString(drawn ? "[game draw]" : "[you have won]"), true, localTeam->color, !drawn));
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

		openDialog(IGM_END_OF_GAME, std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString("[replay ended]"), true));
		miniMapPushed=false;
	}
}
