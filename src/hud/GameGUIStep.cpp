// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "PowerOfTwo.h"
#include <PerformanceTelemetry.h>
#include <EventQueue.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <iostream>
#include <algorithm>
#include <optional>

#include <BackgroundFileWriter.h>
#include <ChunkedStreamBackend.h>
#include <FileManager.h>
#include <SDL3/SDL.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>

#include "EngineTiming.h"
#include "Game.h"
#include "SaveSnapshot.h"
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

#include <SDL3/SDL_keycode.h>

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::moveFlag(int mx, int my, bool drop)
{
	if (globalContainer->isViewingGame()) return;

	int posX, posY;
	auto selBuild=inputBuilding(std::get<BuildingRef>(selection));
	if (!selBuild) return;
	drawnScene().map.cursorToBuildingPos(mx, my, drawnScene().entities.type(*selBuild)->width, drawnScene().entities.type(*selBuild)->height, &posX, &posY, viewportX, viewportY);
	if ((displayedPosX(*selBuild)!=posX)
		||(displayedPosY(*selBuild)!=posY)
		||(drop && (selectionPushedPosX!=posX || selectionPushedPosY!=posY)))
		queueFlagMove(*selBuild, posX, posY, drop);
}

void GameGUI::queueFlagMove(const SnapshotBuilding &flag, int x, int y, bool drop)
{ queueFlagMove(flag.gid,x,y,drop); }

void GameGUI::queueFlagMove(Uint16 gid, int x, int y, bool drop)
{
	shared_ptr<OrderMoveFlag> oms(new OrderMoveFlag(gid, x, y, drop));
	stampClientOrder(oms);
	orderQueue.moveFlag(oms);
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
	if ((button&SDL_BUTTON_MASK(1)) && (torusView.active() || mx<globalContainer->gfx->getW()-RIGHT_MENU_WIDTH))
	{
		if (!torusView.active() && (!camera.contains(mx,my) || my<16)) return;
		if (!torusView.active()) {mx=mapMouseX(mx);my=mapMouseY(my);}
		// Update flag
		if (selectionMode == BUILDING_SELECTION)
		{
			auto selBuild=inputBuilding(std::get<BuildingRef>(selection));
			if (selBuild && selectionPushed && (drawnScene().entities.type(*selBuild)->semantics.relocatable))
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
    GAGCore::EventQueue events;
    SDL_Event event;
    while (GAGCore::GraphicContext::pollEvent(&event)) events.push_back(event);
    step(events.events(), SDL_GetTicks());
}

void GameGUI::step(const std::vector<SDL_Event>& events, Uint64 now)
{
    if(autosaveWriter) autosaveWriter->poll();
    if (simulationThreaded && globalContainer->settings.autosaveGames && autosavePending && (!autosaveWriter || !autosaveWriter->busy()))
        parkForClient([&] {
            if (autosavePending.exchange(false)) { lastAutosaveStep=game.stepCounter; autosave(); }
        });
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
        poll.type = SDL_EVENT_USER;
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
		if (event.type==SDL_EVENT_MOUSE_MOTION)
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
				drawnScene().map.cursorToBuildingPos (mapMouseX(lastMouseX), mapMouseY(lastMouseY), 1, 1, &mouseMapX, &mouseMapY, viewportX, viewportY);
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
				&& (lastMouseButtonState & SDL_BUTTON_MASK(1)) // are we dragging? (should not be hard-coding this condition but should be abstract somehow)
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
		else if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_GUI))
		{
			isRunning=false;
			exitGlobCompletely=true;
		}
#		endif
#		ifdef USE_WIN32
		else if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F4 && (event.key.mod & SDL_KMOD_ALT))
		{
			isRunning=false;
			exitGlobCompletely=true;
		}
#		endif
		else if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) || (event.type == SDL_EVENT_MOUSE_BUTTON_UP))
		{
            if (wasMouseMotion) { processEvent(&mouseMotionEvent); wasMouseMotion = false; }
            if (event.button.button > 0 && event.button.button <= 32) {
                const Uint32 mask = SDL_BUTTON_MASK(event.button.button);
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) lastMouseButtonState |= mask;
                else lastMouseButtonState &= ~mask;
            }
			lastMouseX = event.button.x;
			lastMouseY = event.button.y;
			processEvent (&event);
		}
		else if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
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

	viewportX += drawnScene().map.getW();
	viewportY += drawnScene().map.getH();
	// Continuous scrolling keeps its normal 25 Hz cadence at every game speed.

	if (now < lastViewportStep) lastViewportStep = now;
	// Camera speed is a presentation preference, independent of simulation TPS.
	constexpr unsigned cameraStepMs = 40;
	const unsigned viewportSteps=std::min<Uint64>((now-lastViewportStep)/cameraStepMs, 5);
	if(viewportSteps)
		lastViewportStep=now-(now-lastViewportStep)%cameraStepMs;
	for(unsigned i=0; i<viewportSteps; ++i)
	{
		handleKeyAlways();
        updateCamera();
        if (globalContainer->settings.edgeScrollingEnabled(
                globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN))
        {
            camera.originX+=viewportSpeedX*32/camera.zoom;
            camera.originY+=viewportSpeedY*32/camera.zoom;
        }
        camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
	}
	if (touch) touch->advanceScroll(now);
	if (drawnScene().map.getW() && drawnScene().map.getH())
	{
		viewportX &= drawnScene().map.getMaskW();
		viewportY &= drawnScene().map.getMaskH();
	}

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
	const int viewedTeam = localTeamNo;
	stepEventFeed(viewedTeam);

	// voice step
	std::shared_ptr<OrderVoiceData> orderVoiceData;
	while ((orderVoiceData = globalContainer->voiceRecorder->getNextOrder()) != NULL)
	{
		orderVoiceData->recipientsMask = chatMask ^ (chatMask & (Team::teamNumberToMask(localPlayer)));
		enqueueOrder(orderVoiceData);
	}

	// TODO: die with SGSL
	// Check if the text being displayed has changed, and if it has, add it to the history box
	const auto& legacyText=drawnScene().panels.hud.state().legacyScriptText;
    const bool legacyShown=drawnScene().panels.hud.state().legacyScriptTextShown;
	if(legacyShown && legacyText != previousSGSLText)
	{
		publishMessageHistoryLines(legacyText, HistoryList::Chat,
			Color(255, 255, 255), kHistoryOnlyTimeoutMs, kScriptTextContinuationIndent);
		previousSGSLText = legacyText;
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
		enqueueOrder(order);
		order = toolManager.getOrder();
	}

	// The briefing belongs to the displayed world, like the objective dialog.
    if (drawnScene().tick == 12 && drawnScene().world.session &&
        drawnScene().world.session->missionBriefing && !drawnScene().world.session->missionBriefing->empty())
        openDialog(IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(this, true));

	// Overlay maps are computed during scene extraction (SceneExtractor), from the
	// overlay drawAll publishes in clientRequests.

	// do we have won or lost conditions
	checkWonConditions();

	if (drawnScene().panels.hud.state().anyPlayerWaited)
		anyPlayerWaitedTimeFor++;
	else
		anyPlayerWaitedTimeFor = 0;
}

void GameGUI::stepEventFeed(int viewedTeam)
{
	if (eventFeed.team() != viewedTeam)
		eventFeed.clear(viewedTeam);
	if (viewedTeam < 0 || viewedTeam >= Team::MAX_COUNT || !drawnScene().map.getW() || !drawnScene().map.getH())
		return;

	// The simulation forwards GameEvents through ClientEvents; coalesce the
	// ones for the team being viewed, oldest first.
	const Uint64 nowMs = SDL_GetTicks();
	const auto distanceSquared = [this](int px, int py, int qx, int qy) -> std::int64_t
	{
        const int width = drawnScene().map.getW(), height = drawnScene().map.getH();
        const int x = std::abs(powerOfTwoRemainder(px-qx, width)), y = std::abs(powerOfTwoRemainder(py-qy, height));
        const int dx = std::min(x, width-x), dy = std::min(y, height-y);
        return dx * dx + dy * dy;
    };
	auto &teamEvents = pendingTeamEvents[viewedTeam];
	while (!teamEvents.empty())
	{
		const GameEvent gevent = std::move(teamEvents.front());
		teamEvents.pop_front();
		const GameEventType type = gevent.getEventType();
		const bool conversion = type == GEUnitLostConversion || type == GEUnitGainedConversion;
        std::string message;
        const auto format=[&]{message=gevent.formatMessage(game);};
        if(!conversion || !parkForClient(format)) format();
		eventFeed.ingest({type, conversion ? gevent.getOtherTeamNumber() : gevent.getTypeNum(), gevent.getStep(),
						  gevent.getX(), gevent.getY(), message, gevent.formatColor()},
						 nowMs, distanceSquared);
		eventGoPosX = gevent.getX();
		eventGoPosY = gevent.getY();
		eventGoType = type;
		// In the strategic view an attack is too small to notice on the map
		// itself, so it raises the same pulsing mark a player's ping does.
		if (view.render.detail.strategic > 0 && (type == GEUnitUnderAttack || type == GEBuildingUnderAttack))
			markManager.addMark(Mark(gevent.getX(), gevent.getY(), GAGCore::Color(255, 48, 32)));
	}

	const ClientEvents::TickPulse pulse = clientEvents.pulse();
	if (pulse.valid)
		eventFeed.expire(pulse.tick, nowMs);
}

void GameGUI::syncStep(void)
{
	assert(localTeam);

	// Faster presets run more ticks per second, so they wait proportionally more ticks.
	Uint64 stepNs = GAME_TICK_NS;
	if (canChangeGameSpeed())
		stepNs = (globalContainer->replaying && globalContainer->replayFastForward)
			? 0 : globalContainer->settings.getGameSpeedStepDurationNs();
	const Sint64 autosaveInterval = AUTOSAVE_INTERVAL_TICKS * GAME_TICK_NS / (stepNs ? stepNs : 1000000);
	// Counting from the last save also keeps a paused game from saving every frame.
	const bool autosaveDue = globalContainer->settings.autosaveGames && (lastAutosaveStep < 0
		? game.stepCounter % AUTOSAVE_INTERVAL_TICKS == AUTOSAVE_PHASE_TICKS
		: static_cast<Sint64>(game.stepCounter) - lastAutosaveStep >= autosaveInterval);
	if(autosaveDue) autosavePending=true;
	if (!simulationThreaded && autosavePending && globalContainer->settings.autosaveGames && (!autosaveWriter || !autosaveWriter->busy()))
	{
        autosavePending=false;
		lastAutosaveStep = game.stepCounter;
		autosave();
	}
}

void GameGUI::autosave()
{
    // Do not capture or wait while an earlier save is being encoded.
    if(autosaveWriter && autosaveWriter->busy()) return;
    try
    {
        const std::string name = Toolkit::getStringTable()->getString("[auto save]");
        if (!autosaveWriter)
            autosaveWriter = std::make_unique<BackgroundFileWriter>(Toolkit::getFileManager());
        auto encode=captureSave([&](OutputStream* stream,DeferredGameSHA1* sha){save(stream,name,sha);});
        autosaveWriter->submit(glob2GzipWritePath(glob2NameToFilename("games", name, "game")),std::move(encode));
    }
    catch (const std::exception& error)
    { std::cerr << "Autosave failed; previous file retained: " << error.what() << std::endl; }
}

bool GameGUI::savePending()
{
    // A failed save remains actionable until retry succeeds or the user cancels.
    // This also includes capture waiting behind an earlier autosave.
    const bool dialog = inGameMenu == IGM_SAVE && gameMenuScreen;
    const bool writing = autosaveWriter && autosaveWriter->busy();
    return dialog || writing;
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
    {
        const auto& scene=drawnScene();
        const auto& hud=scene.panels.hud;
        const char* message=nullptr;
        bool won=false;
        auto color=presentationColor(scene.panels.local.state().color);
        if (globalContainer->liveSpectating)
        {
            if (hud.winningTeam<0) return;
            message=hud.drawn ? "[game draw]" : "[Match finished]";
            won=!hud.drawn; color=presentationColor(scene.entities.teams[hud.winningTeam].color);
        }
        else if (networkMatch.active && hud.localState().won)
        {
            hasEndOfGameDialogBeenShown=true;
            if (inGameMenu!=IGM_NONE) closeDialog();
            isRunning=false; return;
        }
        else if (hud.state().totalPrestigeReached && hud.state().prestigeWinCondition)
        { message=hud.localDraw ? "[game draw]" : "[Total prestige reached]"; won=hud.localState().won && !hud.localDraw; }
        else if (hud.localState().lost) message="[you have lost]";
        else if (hud.localState().won)
        {
            message=hud.localDraw ? "[game draw]" : "[you have won]"; won=!hud.localDraw;
            if (inGameMenu==IGM_NONE && campaign) campaign->setCompleted(missionName);
        }
        if (message && inGameMenu==IGM_NONE)
        {
            openDialog(IGM_END_OF_GAME,std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString(message),true,color,won));
            hasEndOfGameDialogBeenShown=true; miniMapPushed=false;
        }
        return;
    }

}

void GameGUI::showEndOfReplayScreen(bool client)
{
    if(simulationThreaded && !client) { gamePaused=true; clientEvents.push(ClientEvent::ReplayEnded{}); return; }
	gamePaused = true;

	if (!hasEndOfGameDialogBeenShown)
	{
		hasEndOfGameDialogBeenShown = true;

		openDialog(IGM_END_OF_GAME, std::make_unique<InGameEndOfGameScreen>(Toolkit::getStringTable()->getString("[replay ended]"), true));
		miniMapPushed=false;
	}
}
