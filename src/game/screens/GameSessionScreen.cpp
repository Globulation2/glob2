#include "tutorial/TutorialModel.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameSessionScreen.h"
#include "Engine.h"
#include "scripting/javascript/ScriptRuntime.h"
#include "GameLoadScreen.h"
#include "MessageScreen.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include "SettingsScreen.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <stdexcept>
#include <ApplicationHost.h>

namespace
{
// Milliseconds of extra ticks a frame may run while a turn game catches up.
constexpr Uint64 CATCH_UP_FRAME_BUDGET_MS = 30;
#ifdef __EMSCRIPTEN__
constexpr Uint32 BROWSER_TICK_BUDGET_MS = 6;
constexpr Uint64 PAUSED_REDRAW_INTERVAL_MS = 40;
#endif
} // namespace

GameSessionScreen::GameSessionScreen(GAGGUI::ScreenStack &stack, std::unique_ptr<Engine> engine)
	: stack(stack), engine(std::move(engine))
{
	if (!this->engine)
		throw std::invalid_argument("A game screen requires an initialized engine");
}
GameSessionScreen::~GameSessionScreen()
{
    cancelScreenInputReceipt();
	if (started && engine)
		engine->restoreCursor();
}

void GameSessionScreen::updateExecution(Uint32 tick)
{
	try
	{
		updateExecutionImpl(tick);
	}
	catch (const Script::SessionFailure &failure)
	{
        GAGCore::ApplicationHost::studioError(failure.what());
		engine->abortSession();
		engine->restoreCursor();
		started = false;
		finished = true;
		cancelScreenInputReceipt(); input.clear();
		if (globalContainer->mix)
			globalContainer->mix->setNextTrack(MusicTrack::Menu, true);
		stack.push(std::make_unique<MessageScreen>(std::string("Game stopped: ") + failure.what() +
													   "\nReview the embedded JavaScript and its "
													   "resource use before restarting this game.",
												   std::vector<std::string>{"OK"}),
				   [this](GAGGUI::Screen &, int) { endExecute(0); });
	}
}

bool GameSessionScreen::hasPendingExecutionInput() const
{
    return !input.events().empty() || (engine && engine->hasPendingClientInput());
}
void GameSessionScreen::beginAdditionalInput(std::shared_ptr<GAGCore::InputDispatchReceipt> receipt)
{
	inputReceipts.push_back({std::move(receipt), input.events().size(), 0});
}
void GameSessionScreen::endAdditionalInput(std::size_t enqueued)
{
	auto &tracked = inputReceipts.back();
	tracked.count = enqueued;
	if (!enqueued)
	{
		tracked.receipt->consumed = 0;
		inputReceipts.pop_back();
	}
}
void GameSessionScreen::pruneCancelledScreenInput()
{
	for (std::size_t i = 0; i < inputReceipts.size();)
	{
		const auto &tracked = inputReceipts[i];
		if (!tracked.receipt->isCancelled())
		{
			++i;
			continue;
		}
		const auto first = tracked.first, count = tracked.count;
		input.erase(first, count);
		tracked.receipt->consumed = 0;
		inputReceipts.erase(inputReceipts.begin() + i);
		for (auto &remaining : inputReceipts)
			if (remaining.first >= first + count)
				remaining.first -= count;
	}
}
void GameSessionScreen::cancelScreenInputReceipt()
{
	for (auto i = inputReceipts.rbegin(); i != inputReceipts.rend(); ++i)
	{
		input.erase(i->first, i->count);
		i->receipt->consumed = 0;
	}
	inputReceipts.clear();
}
void GameSessionScreen::transferInputReceipt()
{
	for (auto &tracked : inputReceipts)
		engine->trackInputDispatch(std::move(tracked.receipt), tracked.first, tracked.count);
	inputReceipts.clear();
}

void GameSessionScreen::updateExecutionImpl(Uint32 tick)
{
	pruneCancelledScreenInput();
	if (!isExecutionRunning() || finished)
		return;
#ifdef __EMSCRIPTEN__
	presentationDirty |= !input.events().empty();
#endif
	if (!started)
	{
		clock = lastTick = tick;
		engine->prepareRun();
		engine->beginSession(clock);
		nextTick = clock;
		started = true;
		finishingSession = false;
		// The simulation runs on its own thread where threads exist; otherwise
		// (the browser build, thread creation failure) this host steps it serially.
		engine->startSimulationThread(clock);
	}
	else
	{
		if (resetClock)
		{
			lastTick = tick;
			nextTick = clock;
			resetClock = false;
		}
		clock += static_cast<Uint32>(tick - lastTick);
		lastTick = tick;
	}
	bool running = false;
	if (finishingSession)
	{
		transferInputReceipt();
		const bool pending = engine->advancePendingSave(input.events());
		cancelScreenInputReceipt(); input.clear();
		if (pending) return;
	}
	else if (engine->simulationThreaded())
	{
		// Input and GUI logic every frame, with the simulation parked between ticks;
		// the simulation thread paces itself.
		engine->resumeSimulation(clock);
		transferInputReceipt();
		try { running = engine->threadedClientFrame(clock, input.events()); }
		catch (...) { engine->abortSession(); throw; }
		cancelScreenInputReceipt(); input.clear();
	}
	else
	{
#ifdef __EMSCRIPTEN__
        if (!engine->turnLockstep())
        {
            // Local matches service input every display frame and batch due ticks.
            transferInputReceipt();
            try { running = engine->serialClientFrame(clock, input.events(), BROWSER_TICK_BUDGET_MS); }
            catch (...) { engine->abortSession(); throw; }
            cancelScreenInputReceipt(); input.clear();
        }
        else
#endif
        {
			if (clock < nextTick)
			{
				// A turn game reads its relay connection between steps.
				engine->pollTurnSession(clock);
				return;
			}
			transferInputReceipt();
			running = engine->stepSession(clock, input.events());
			cancelScreenInputReceipt(); input.clear();
			// Catching up in a turn game: the host calls this once per frame, so one tick per
			// call caps the replay at the frame rate (in the browser, below real time on slow
			// devices: the client falls further behind). Run more ticks within a frame
			// budget; none of them is drawn (the catch-up draw ratio) and the frame still
			// returns to the host in time for input and the card.
			if (running && engine->turnFastForwarding())
			{
				const Uint64 deadline = SDL_GetTicks() + CATCH_UP_FRAME_BUDGET_MS;
				while (running && engine->turnFastForwarding() && SDL_GetTicks() < deadline)
					running = engine->stepSession(clock, {});
			}
			nextTick = clock + engine->sessionDelay(clock);
        }
	}
	if (running && engine->takeSettingsRequest()) {
        engine->suspendInput(); cancelScreenInputReceipt(); input.clear();
        stack.push(std::make_unique<SettingsScreen>(&stack));
        return;
    }
	// Disk completion is only one stage: retain the dialog for queued capture,
	// browser persistence, and retry/export after a persistence failure.
	if (!running && !finishingSession)
	{
		finishingSession = true;
		if (engine->advancePendingSave({})) return;
	}
	if (!running)
	{
		if (auto request = engine->finishSessionForHost())
		{
			engine->restoreCursor();
			started = false;
			finished = true;
			stack.push(std::make_unique<GameLoadScreen>(
						   std::move(engine),
						   [request = *request](Engine &next)
						   {
							   return request.replay ? next.loadReplayTask(request.filename)
													 : request.campaign ? next.initCampaignTask(request.filename)
                                                                : next.initCustomTask(request.filename);
						   }),
					   [this](GAGGUI::Screen &loading, int result)
					   {
						   if (result == 1)
						   {
							   engine = static_cast<GameLoadScreen &>(loading).takeEngine();
							   finished = false;
							   resetClock = false;
							   cancelScreenInputReceipt(); input.clear();
						   }
						   else
						   {
							   if (globalContainer->mix)
								   globalContainer->mix->setNextTrack(MusicTrack::Menu, true);
							   if (result == 2)
							   {
								   auto &strings = *GAGCore::Toolkit::getStringTable();
								   stack.push(
									   std::make_unique<MessageScreen>(
										   static_cast<GameLoadScreen &>(loading).failureMessage(),
										   std::vector<std::string>{strings.getString("[ok]")}),
									   [this](GAGGUI::Screen &, int choice)
									   { endExecute(choice); });
							   }
							   else
								   endExecute(result);
						   }
					   });
			return;
		}
		finished = true;
        if (engine->tutorialNavigation()) {
            if (globalContainer->mix) globalContainer->mix->setNextTrack(MusicTrack::Menu,true);
            endExecute(engine->tutorialNavigation());
            return;
        }
		auto endScreen = engine->endRunScreen();
		if (!endScreen)
			endExecute(QUIT_APPLICATION);
		else
			stack.push(std::move(endScreen),
					   [this](GAGGUI::Screen &, int result) { endExecute(result); });
	}
}

void GameSessionScreen::handleExecutionEvent(SDL_Event event)
{
	// Engine/GameGUI translates native coordinates once, at consumption.
	if (isExecutionRunning() && !finished)
		input.push_back(event);
}

void GameSessionScreen::drawExecution()
{
	// The engine owns presentation, including nextFrame; don't also present
	// through Screen::dispatchPaint.
	if (started && !finished && isExecutionRunning())
    {
#ifdef __EMSCRIPTEN__
        // Keep input responsive while paused, without continuously redrawing
        // a stationary map at display rate. Timed UI changes still repaint.
        const auto now = SDL_GetTicks();
        if (!engine->turnLockstep() && engine->presentationPaused() && !presentationDirty &&
            now - lastDraw < PAUSED_REDRAW_INTERVAL_MS)
            return;
        lastDraw = now;
#endif
#ifdef __EMSCRIPTEN__
        engine->drawSession(true);
        presentationDirty = false;
#else
        engine->drawSession();
#endif
    }
}

Uint32 GameSessionScreen::executionDelay(Uint32 now, Uint32 fallback)
{
	if (!started || finished)
		return 0;
#ifdef __EMSCRIPTEN__
    // Relay matches retain their polling/catch-up cadence. Local matches use
    // display pacing; ordinary zero-delay loading jobs still use timers.
    if (!engine->turnLockstep()) return GAGCore::ApplicationHost::AnimationFrameDelay;
#endif
	// Keep client/network updates responsive between capped drawing frames.
    // Unlimited removes the drawing wait; no fixed 120 FPS ceiling remains.
    if (engine->simulationThreaded())
        return std::min(Uint32(8), globalContainer->gfx->renderFrameWait());
	return engine->sessionPollDelay(clock + static_cast<Uint32>(now - lastTick));
}

GAGGUI::Screen::ExecutionWait GameSessionScreen::executionWait() const
{
	if (!started || finished || !engine)
		return ExecutionWait::Untimed;
	// The simulation thread owns the session state; its frame waits are pacing.
	if (engine->simulationThreaded())
		return ExecutionWait::Pacing;
	return engine->waitingOnNetwork() ? ExecutionWait::Network : ExecutionWait::Pacing;
}

void GameSessionScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    if (engine)
    {
        engine->cancelInputDispatches();
        engine->viewportResized(oldWidth, oldHeight, width, height);
    }
	cancelScreenInputReceipt(); input.clear();
	resetClock = true;
#ifdef __EMSCRIPTEN__
	presentationDirty = true;
#endif
}

void GameSessionScreen::suspendExecution()
{
	if (engine)
	{
		engine->suspendInput();
		// No frames run in the background; the simulation thread waits too, as the
		// serial loop did. updateExecution resumes it.
		engine->suspendSimulation();
	}
	cancelScreenInputReceipt(); input.clear();
	resetClock = true;
}
