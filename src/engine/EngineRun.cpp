// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GameplayRecording.h>
#include <PerformanceTelemetry.h>
#include <EventQueue.h>
#include <ApplicationHost.h>
#include <FormatableString.h>

#include "AINames.h"
#include "AIThreading.h"
#include "ChecksumSidecar.h"
#include "ConnectionOverlay.h"
#include "DatasetWriter.h"
#include "Engine.h"
#include "hive/HiveClient.h"
#include "GameDiagnostics.h"
#include <utility>
#include "EngineTiming.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include <SDL3/SDL.h>
#include "TurnLockstep.h"
#include "team/Team.h"
#include "TeamStat.h"
#include "building/IntBuildingType.h"
#include "unit/Unit.h"
#include "unit/UnitConsts.h"

#include <functional>
#include <chrono>
#include <thread>
#include <iostream>
#include <array>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <chrono>
#include "Version.h"
#include "sim/SimulationRunner.h"
#include "scripting/javascript/ScriptRuntime.h"
#include <stdexcept>

using std::shared_ptr;


namespace
{
// Every host preserves the same failure cleanup and user-facing error contract.
template<class Step>
bool guardedSessionStep(Engine& engine, Step&& step)
{
    try { return step(); }
    catch (const Script::SessionFailure&) { engine.abortSession(); throw; }
    catch (const std::bad_alloc&)
    {
        engine.abortSession();
        throw Script::HostFailure("Native allocation failed during the game session");
    }
}
} // namespace

void Engine::updateTickSpeedAndDrawCadence(MainLoopState& st, Uint64 now)
{
	const int previousSpeed = st.speed;
	int renderInterval = st.adjustableGameSpeed ? globalContainer->settings.getGameSpeedRenderInterval() : 1;
	st.speed = st.adjustableGameSpeed ? globalContainer->settings.getGameSpeedStepDuration() : GAME_TICK_MS;

	// Replay fast-forward uses the uncapped preset.
	if (globalContainer->replaying && globalContainer->replayFastForward
		&& !gui.gamePaused && !gui.hardPause)
	{
		st.speed = REPLAY_FAST_FORWARD_MS;
		renderInterval = REPLAY_FAST_FORWARD_DRAW_RATIO;
	}

	// Pausing must not turn an uncapped preset into a busy loop, and GUI
	// input should be rendered on every paused frame.
	if (gui.gamePaused || gui.hardPause)
	{
		st.speed = GAME_TICK_MS;
		renderInterval = 1;
	}

	// Turn games run at the session's pace, even while paused (the relay's clock
	// keeps going). A zero interval is catch-up: uncapped ticks with rendering
	// skipped, the replay fast-forward preset.
	if (turn)
	{
		const std::uint64_t interval = turn->turn().tickIntervalMicros();
		st.speed = interval == 0 ? REPLAY_FAST_FORWARD_MS : static_cast<int>((interval + 500) / 1000);
		renderInterval = interval == 0 ? REPLAY_FAST_FORWARD_DRAW_RATIO : 1;
	}
	if (st.nextGuiStep < 0 || st.nextGuiStep >= renderInterval)
		st.nextGuiStep = renderInterval - 1;

	// A preset change or pause starts a fresh timing budget.
	if (st.speed != previousSpeed)
		st.needToBeTime = static_cast<Sint64>(now - st.startTime);
}

// Headless / scripted-test polling: under --nox automaticEndingGame, flip
// gui.isRunning=false once a local end condition fires (local team dead, local
// team won, total-prestige reached, game ended). Records automaticGameEndTick.
void Engine::pollAutomaticEndingConditions(Uint64 now)
{
	if (!globalContainer->automaticEndingGame)
		return;

	trackTeamEliminations();
	auto endGame = [this, now](const char* reason) {
		printf("nox::%s\n", reason);
		gui.isRunning = false;
		automaticGameEndTick = now;
	};

	if (!gui.getLocalTeam()->isAlive && !globalContainer->automaticGameGlobalEndConditions)
		endGame("gui.localTeam is dead");
	else if (gui.getLocalTeam()->hasWon && !globalContainer->automaticGameGlobalEndConditions)
		endGame("gui.localTeam has won");
	else if (gui.game.totalPrestigeReached)
		endGame("gui.game.totalPrestigeReached");
	else if (gui.game.isGameEnded)
		endGame("gui.game.isGameEnded");
}

// Push this tick's local + AI orders into the network layer. AI poll and
// setWaitingOnMask always run; the "previous tick
// committed" branches (syncStep, addLocalOrder, advanceStep, sidecar) only
// fire when wasReadyLastTick — otherwise we're still waiting on a remote peer
// and must not advance.
void Engine::gatherAndAdvanceOrders(bool wasReadyLastTick)
{
	PERF_SCOPE_TIME(Orders);
	// Viewpoint changes must not move the controller used for order bookkeeping.
    const int orderPlayer=globalContainer->liveSpectating ? 0 : gui.localPlayer;
	shared_ptr<Order> localOrder;
	// But some jobs have to be executed synchronously:
	if (wasReadyLastTick)
	{
		gui.syncStep();

		// The gui.localPlayer may have been updated (in replays)
		// Keep them synchronized here
		net->setLocalPlayer(orderPlayer);

		// We get and push local orders
		localOrder = gui.getOrder();
	}
	// A turn game hands every queued order to the session as soon as the GUI makes it,
	// even while waiting for a bundle. The session sends them at the rate the relay
	// sequences them (one per tick) and lets a later order replace a waiting one for
	// the same flag or building, so a drag or a held key costs no input delay.
	if (turn)
	{
		if (localOrder)
			net->addLocalOrder(std::exchange(localOrder, std::make_shared<NullOrder>()));
		for (auto order = gui.getOrder(); order->getOrderType() != ORDER_NULL; order = gui.getOrder())
			net->addLocalOrder(order);
	}

	if (diagnostics) diagnostics->beginTick(gui.game);
	const bool localAI = wasReadyLastTick && globalContainer->liveSpectating &&
		gui.game.players[orderPlayer]->ai;
	if (!gui.gamePaused && gui.game.map.computeEnabled(Map::ComputeAI) &&
		gui.game.map.computeExecutor().threadCount() > 1)
	{
		// Decisions read the same completed tick. Bind telemetry before dispatch:
		// its first use can append to shared team statistics, including when two
		// controllers belong to one team. The workers only update their own AI.
		std::array<int, Team::MAX_COUNT> aiPlayers{};
		size_t aiCount = 0;
		if (localAI) aiPlayers[aiCount++] = orderPlayer;
		for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); ++i)
			if (gui.game.players[i]->ai &&
				!(globalContainer->liveSpectating && i == orderPlayer) &&
				!net->orderReceived(i))
				aiPlayers[aiCount++] = i;
		for (size_t job = 0; job < aiCount; ++job)
		{
			const int i = aiPlayers[job];
			if (!gui.gamePaused && gui.game.players[i]->team->isAlive)
				gui.game.players[i]->ai->bindTelemetry();
		}

		std::array<shared_ptr<Order>, Team::MAX_COUNT> aiOrders{};
		gui.game.map.computeExecutor().run(aiCount, [&](size_t job) {
			aiOrders[job] = gui.game.players[aiPlayers[job]]->ai->getOrder(gui.gamePaused);
		});
		size_t firstRemote = 0;
		if (localAI)
		{
			localOrder = aiOrders[0];
			firstRemote = 1;
		}
		if (wasReadyLastTick) net->addLocalOrder(localOrder);
		for (size_t job = firstRemote; job < aiCount; ++job)
			net->pushOrder(aiOrders[job], aiPlayers[job], true);
	}
	else
	{
		if (localAI)
			localOrder = gui.game.players[orderPlayer]->ai->getOrder(gui.gamePaused);
		if (wasReadyLastTick) net->addLocalOrder(localOrder);
		// Get and push AI orders when they are needed for this frame.
		for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); i++)
		{
			if (gui.game.players[i]->ai && !(globalContainer->liveSpectating && i==orderPlayer) && !net->orderReceived(i))
			{
				shared_ptr<Order> order = gui.game.players[i]->ai->getOrder(gui.gamePaused);
				net->pushOrder(order, i, true);
			}
		}
	}

	if (diagnostics) diagnostics->completeTick(gui.game);
	if (wasReadyLastTick)
	{
		PERF_SCOPE_TIME(Replay);
		Uint32 checksum = gui.game.checkSum(NULL, NULL, NULL);
		net->advanceStep(checksum);

		if (globalContainer->replayWriter) globalContainer->replayWriter->setCheckSum(checksum);

		if (checksumSidecar)
			checksumSidecar->writeTick(gui.game.stepCounter, checksum, gui.game);
	}
	// advanceStep inserts the local order. Measuring earlier leaves a stale
	// waiting flag; replays filter null orders and cannot clear it by executing one.
	gui.game.setWaitingOnMask(net->getWaitingOnMask());
}

// Once tickReady() is true for this tick, commit the tick: validate
// checksums (assert on desync), execute the matched orders, pump the replay
// reader if we're in playback, and run game.syncStep. Called only from inside
// the !hardPause branch, so the original !gui.hardPause guard on syncStep is
// implicit here.
void Engine::executeOrdersAndStep(bool readyNow)
{
	if (readyNow)
	{
		// A turn session never reports a mismatch here: the relay arbitrates the
		// checksums, and a divergence reaches pumpTurnSession as a reload request
		// or a flagged match. Single player and legacy games keep the dump.
		if (!turn && !net->matchCheckSums())
		{
			std::cout << "Game desychronized." << std::endl;
			gui.game.dumpAllData("glob2.world-desynchronization.dump.txt");
			assert(false);
		}
		else
		{
			// We get all currents orders from the network and execute them:
			for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); i++)
			{
				shared_ptr<Order> order = net->retrieveOrder(i);
				if (!globalContainer->replaying)
				{
					gui.executeOrder(order);
				}
				else if (order->getOrderType() == ORDER_PLAYER_QUIT_GAME ||
				         order->getOrderType() == ORDER_PAUSE_GAME)
				{
					gui.executeOrder(order);
				}
			}
			// A match with a pause limit resumes by itself when the seat that paused
			// has used its time, at the same tick on every client and in the verifier.
			if (turn)
				if (auto resume = turn->takeForcedResume())
					gui.executeOrder(resume);
			net->clearTopOrders();
		}
	}

	// Load the replay's orders
	if (globalContainer->replaying)
	{
		assert(globalContainer->replayReader);
		assert(globalContainer->replayReader->isValid());

		while (globalContainer->replayReader->hasMoreOrdersThisStep())
		{
			shared_ptr<Order> order = globalContainer->replayReader->retrieveOrder();

			if (order->getOrderType() != ORDER_PLAYER_QUIT_GAME &&
			    order->getOrderType() != ORDER_PAUSE_GAME &&
			    order->getOrderType() != ORDER_NULL)
			{
				gui.executeOrder(order);
			}
		}

		if (globalContainer->replayReader->isFinished())
		{
			gui.showEndOfReplayScreen();
		}
	}

	// here we do the real work
	if (readyNow && !gui.gamePaused)
	{
		if (globalContainer->replaying)
		{
			assert(globalContainer->replayReader);
			globalContainer->replayReader->advanceStep();
		}

		gui.game.syncStep(gui.localTeamNo);
		// Hand the tick's notices to the GUI now, also under --nox where
		// gui.step never runs, so the event queue cannot grow unbounded. With a
		// simulation thread the GUI consumes them while the simulation is parked.
		if (!gui.simulationThreaded)
			gui.consumeClientEvents();
		GAGCore::ApplicationHost::simulationAdvanced(gui.game.stepCounter);
	}
}

void Engine::drawFrame(MainLoopState& st, bool everyFrame, const Scene* scene)
{
    GAGCore::ApplicationHost::matchFrame(gui.gamePaused);
	const bool renderedFrame = everyFrame || st.nextGuiStep == 0;
	if (renderedFrame)
	{
		// A threaded client records the immutable scene it presents, rather than
		// reading the live simulation's tick or timing from the rendering thread.
		GAGCore::Recording::recorder().matchFrame(scene ? scene->tick : gui.game.stepCounter,
				gui.gamePaused || gui.hardPause, scene ? int(scene->tickInterval) : st.speed);
		gui.drawAll(gui.localTeamNo);
		{
			PERF_SCOPE_TIME(Present);
			globalContainer->gfx->nextFrame();
		}
		PerformanceTelemetry::collector().presented();
	}

}

void Engine::drawSession(bool everyFrame)
{
    if (!session) throw std::logic_error("No active engine session");
    if (diagnostics && diagnostics->pending())
    {
        const auto drain = [&] { diagnostics->drain(); };
        if (runner) runner->withGame(drain); else drain();
    }
    if (globalContainer->runNoX) return;
    if (!runner)
    {
        if (turn && !std::exchange(turnDrawPending, false)) return;
        drawFrame(*session, everyFrame && !turn);
        return;
    }
    // Threaded: draw the newest scene the simulation published, every frame.
    const Scene *scene = runner->acquireScene();
    if (!scene)
        return;
    gui.setPublishedScene(scene);
    drawFrame(*session, true, scene);
}

bool Engine::startSimulationThread(Uint64 now)
{
    if (!session) throw std::logic_error("No active engine session");
    if (runner) return true;
    // Headless sessions run serially unless GLOB2_SIM_THREAD (a test switch for
    // simulation-equivalence checks) asks for the simulation thread.
    // GLOB2_SIM_THREAD=0 keeps any session serial, for tests that count frames
    // against a scripted host clock.
    const char* simThread = std::getenv("GLOB2_SIM_THREAD");
    if (simThread && std::string(simThread) == "0") return false;
    if (globalContainer->runNoX && !simThread) return false;
    // Turn games stay serial: the relay connection is polled between steps on the
    // main thread (pollTurnSession), where the connection panel also reads it.
    if (turn) return false;
    publishSessionClock(now);
    auto started = std::make_unique<SimulationRunner>(*this);
    gui.simulationThreaded = true;
    if (!started->start())
    {
        gui.simulationThreaded = false;
        return false;
    }
    runner = std::move(started);
    return true;
}

void Engine::stopSimulationThread()
{
    if (!runner) return;
    runner->stop();
    // The simulation's measurements since the last client frame.
    PerformanceTelemetry::collector().absorb(runner->telemetry);
    runner.reset();
    gui.simulationThreaded = false;
    gui.setPublishedScene(nullptr);
    // Notices published after the last client step.
    gui.consumeClientEvents();
}

bool Engine::threadedClientFrame(Uint64 now, const std::vector<SDL_Event>& events)
{
    if (!runner) throw std::logic_error("Simulation thread not running");
    publishSessionClock(now);
    runner->rethrowFailure();
    if (gui.isRunning)
        runner->withGame([&] { clientStep(events); absorbSimulationTelemetry(); });
    runner->rethrowFailure();
    return gui.isRunning && !runner->ended();
}

void Engine::suspendSimulation()
{
    if (runner) runner->suspend();
}

void Engine::resumeSimulation(Uint64 now)
{
    publishSessionClock(now);
    if (runner) runner->resume();
}

void Engine::publishSessionClock(Uint64 now)
{
    sessionClockOffset.store(static_cast<Sint64>(now) - static_cast<Sint64>(SDL_GetTicks()));
}

Uint64 Engine::sessionClock() const
{
    return static_cast<Uint64>(static_cast<Sint64>(SDL_GetTicks()) + sessionClockOffset.load());
}

void Engine::extractScene(Scene& scene)
{
    gui.extractScene(scene);
}

void Engine::pollTurnSession(Uint64 now)
{
	if (turn && session)
		turn->turn().update(now * 1000);
}

Uint32 Engine::sessionPollDelay(Uint64 now)
{
	const Uint32 delay = sessionDelay(now);
	return turn ? std::min<Uint32>(delay, TURN_POLL_MS) : delay;
}

Uint32 Engine::sessionDelay(Uint64 now)
{
    if (!session) throw std::logic_error("No active engine session");
    // Headless games run uncapped, except turn games: their pace is the relay's
    // (a headless client must not run ahead of the horizon in a busy loop).
    if (globalContainer->runNoX && !turn) return 0;
    auto& st = *session;
	// we compute timing

	Sint64 currentTime = static_cast<Sint64>(now) - static_cast<Sint64>(st.startTime);
	//if we are more than MAX_CATCHUP_MS milliseconds behind where we should be,
	//then truncate it. This is to avoid playing "catchup" for long
	//periods of time if Glob2 received allmost no cpu time
	// A turn session catching up runs uncapped, so the cap does not apply.
	const bool turnCatchingUp = turn && turn->turn().catchingUp();
	if (!turnCatchingUp && (currentTime - st.needToBeTime) > MAX_CATCHUP_MS)
		st.needToBeTime = currentTime - MAX_CATCHUP_MS;

	//Any inconsistancies in the delays will be smoothed throughout the following frames,
	Uint64 delay = std::max<Sint64>(0, st.needToBeTime - currentTime);

    return delay > 0 ? delay : (!st.wasReadyLastTick ? 1 : 0);
}

// If the GUI requested a clean exit, drain remaining local orders into the
// network layer and flush. Returns true if the engine loop should break.
bool Engine::handleExitRequest()
{
	if (!gui.flushOutgoingAndExit)
		return false;

	shared_ptr<Order> localOrder = gui.getOrder();
	while (localOrder->getOrderType() != ORDER_NULL)
	{
		net->addLocalOrder(localOrder);
		localOrder = gui.getOrder();
	}

	gui.isRunning = false;
	net->flushAllOrders();
	return true;
}

// Print the human-readable end-of-game summary plus a single key=value line
// ("GLOB2_GAME_END ...") that the AI-trainer pipeline and external test
// drivers scrape from stdout. Caller checks automaticEndingGame.
void Engine::printAutomaticEndingSummary()
{
	int time = gui.game.stepCounter;
	int seconds = (time / GAME_TICKS_PER_SECOND) % 60;
	int minutes = (time / GAME_TICKS_PER_SECOND) / 60;
	std::cout << "automaticEndingGame ended: " << time << " ticks, " << minutes << " minutes, " << seconds << " seconds" << std::endl;

	// Machine-parseable summary line for the AI-trainer pipeline (and any
	// external driver scraping headless output). One line, key=value pairs,
	// space-separated. Winner is the first team with hasWon set, else
	// WINNER_TEAM_NONE (timeout / no winner).
	int winnerTeam = WINNER_TEAM_NONE;
	for (int t = 0; t < gui.game.mapHeader.getNumberOfTeams(); t++)
	{
		if (gui.game.teams[t] && gui.game.teams[t]->hasWon)
		{
			winnerTeam = t;
			break;
		}
	}
	Uint32 orders = globalContainer->replayWriter
		? globalContainer->replayWriter->getOrderCount() : 0;
	std::cout << "GLOB2_GAME_END ticks=" << time
		<< " winner_team=" << winnerTeam
		<< " seed=" << gui.game.gameHeader.getRandomSeed()
		<< " map=\"" << gui.game.mapHeader.getMapName() << "\""
		<< " orders=" << orders
		<< " players=";
	for (int p = 0; p < gui.game.gameHeader.getNumberOfPlayers(); p++)
	{
		const BasePlayer& bp = gui.game.gameHeader.getBasePlayer(p);
		if (p > 0) std::cout << ",";
		std::cout << "team" << bp.teamNumber << ":";
		if (bp.type == BasePlayer::P_LOCAL)
			std::cout << "local";
		else if (bp.type == BasePlayer::P_IP)
			std::cout << "ip";
		else if (bp.type >= BasePlayer::P_AI)
			std::cout << AINames::getAIText(BasePlayer::implementationIdFromPlayerType(bp.type));
		else
			std::cout << "none";
	}
	std::cout << std::endl;

	// Optional per-team economic/military timeline for AI debugging. Gated by
	// GLOB2_TEAM_TIMELINE so normal headless runs are unaffected. Dumps the
	// 512-tick EndOfGameStat history (units/buildings/prestige/hp/atk/def) plus
	// a final detailed snapshot (workers/explorers/warriors, food state, and a
	// per-building-type count) for every team, so two AIs' trajectories can be
	// compared side by side after a single game.
	if (getenv("GLOB2_TEAM_TIMELINE"))
		printTeamTimeline();

	// Optional per-team outcome lines for tooling (tools/map_fairness_tournament.py): who won or
	// lost, when each team was eliminated, where it started and what it had left. Reads state
	// only, and is gated by GLOB2_TEAM_RESULTS so normal headless output is unchanged.
	if (getenv("GLOB2_TEAM_RESULTS"))
		printTeamResults();
}

void Engine::trackTeamEliminations()
{
	const int nbTeams = gui.game.mapHeader.getNumberOfTeams();
	if ((int)teamEliminatedTick.size() != nbTeams)
		teamEliminatedTick.assign(nbTeams, -1);
	for (int t = 0; t < nbTeams; t++)
	{
		const Team* team = gui.game.teams[t];
		// stepCounter already counts the step that cleared isAlive, like GLOB2_GAME_END's ticks.
		if (team && !team->isAlive && teamEliminatedTick[t] < 0)
			teamEliminatedTick[t] = (Sint32)gui.game.stepCounter;
	}
}

void Engine::printTeamResults()
{
	trackTeamEliminations();
	Game& game = gui.game;
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); t++)
	{
		const Team* team = game.teams[t];
		if (!team)
			continue;
		int units = 0, workers = 0, explorers = 0, warriors = 0, buildings = 0, sites = 0;
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			const Unit* unit = team->myUnits[i];
			if (!unit)
				continue;
			units++;
			workers += unit->typeNum == WORKER;
			explorers += unit->typeNum == EXPLORER;
			warriors += unit->typeNum == WARRIOR;
		}
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			const Building* building = team->myBuildings[i];
			if (!building || building->type->isVirtual)
				continue;
			if (building->type->isBuildingSite)
				sites++;
			else
				buildings++;
		}
		std::cout << "GLOB2_TEAM_RESULT team=" << t
			<< " result=" << (team->hasWon ? "won" : team->hasLost ? "lost" : "undecided")
			<< " alive=" << (team->isAlive ? 1 : 0)
			<< " eliminated_tick=" << teamEliminatedTick[t]
			<< " start=" << team->startPosX << "," << team->startPosY
			<< " prestige=" << team->prestige
			<< " units=" << units
			<< " workers=" << workers
			<< " explorers=" << explorers
			<< " warriors=" << warriors
			<< " buildings=" << buildings
			<< " sites=" << sites
			<< std::endl;
	}
}

// Per-team timeline dump (see GLOB2_TEAM_TIMELINE in printAutomaticEndingSummary).
void Engine::printTeamTimeline()
{
	PERF_SCOPE_TIME(Output);
	Game& game = gui.game;
	const int nbTeams = game.mapHeader.getNumberOfTeams();

	// Map team number -> AI label from the game header.
	std::vector<std::string> aiLabel(nbTeams, "?");
	for (int p = 0; p < game.gameHeader.getNumberOfPlayers(); p++)
	{
		const BasePlayer& bp = game.gameHeader.getBasePlayer(p);
		if (bp.teamNumber < 0 || bp.teamNumber >= nbTeams)
			continue;
		if (bp.type >= BasePlayer::P_AI)
			aiLabel[bp.teamNumber] = AINames::getAIText(BasePlayer::implementationIdFromPlayerType(bp.type));
		else if (bp.type == BasePlayer::P_LOCAL)
			aiLabel[bp.teamNumber] = "local";
	}

	for (int t = 0; t < nbTeams; t++)
	{
		Team* team = game.teams[t];
		if (!team)
			continue;
		const std::vector<EndOfGameStat>& hist = team->stats.getEndOfGameStats();
		std::cout << "GLOB2_TIMELINE team=" << t << " ai=" << aiLabel[t]
			<< " result=" << (team->hasWon ? "won" : team->hasLost ? "lost" : "alive")
			<< " samples=" << hist.size() << std::endl;
		for (size_t i = 0; i < hist.size(); i++)
		{
			const EndOfGameStat& s = hist[i];
			std::cout << "GLOB2_TL team=" << t
				<< " tick=" << (i * (END_OF_GAME_STAT_INTERVAL_MASK + 1))
				<< " units=" << s.value[EndOfGameStat::TYPE_UNITS]
				<< " bld=" << s.value[EndOfGameStat::TYPE_BUILDINGS]
				<< " prestige=" << s.value[EndOfGameStat::TYPE_PRESTIGE]
				<< " hp=" << s.value[EndOfGameStat::TYPE_HP]
				<< " atk=" << s.value[EndOfGameStat::TYPE_ATTACK]
				<< " def=" << s.value[EndOfGameStat::TYPE_DEFENSE]
				<< std::endl;
		}

		team->stats.refreshMeasurements(team);
		team->stats.printMeasurements(t, true);
		AITelemetry::capture(team, false, true, true);
		// Final detailed snapshot: composition + food economy + building mix.
		TeamStat* fin = team->stats.getLatestStat();
		std::cout << "GLOB2_FINAL team=" << t
			<< " workers=" << fin->numberUnitPerType[WORKER]
			<< " explorers=" << fin->numberUnitPerType[EXPLORER]
			<< " warriors=" << fin->numberUnitPerType[WARRIOR]
			<< " food=" << fin->totalFood << "/" << fin->totalFoodCapacity
			<< " fooded=" << fin->totalUnitFooded << "/" << fin->totalUnitFoodable
			<< " foodCritical=" << fin->needFoodCritical
			<< " needFood=" << fin->needFood
			<< " bld:";
		for (int b = 0; b < IntBuildingType::NB_BUILDING; b++)
		{
			if (fin->numberBuildingPerType[b] == 0)
				continue;
			std::cout << " " << IntBuildingType::typeFromShortNumber(b)
				<< "=" << fin->numberBuildingPerType[b];
		}
		std::cout << std::endl;
	}
}

// Finish writing the last autosave, close cross-replay debug sinks (sidecar,
// dataset) and tear down the network session. The Engine itself
// stays alive for a possible reload.
void Engine::teardownSession()
{
	gui.waitForAutosave();

	if (checksumSidecar)
	{
		if (!checksumSidecar->close())
			std::cerr << "GLOB2_CHECKSUM_SIDECAR: a sidecar write or close failed; "
				"the truncated .checksums file has been deleted" << std::endl;
		checksumSidecar.reset();
	}

	if (globalContainer->datasetWriter)
	{
		globalContainer->datasetWriter->close();
		globalContainer->datasetWriter.reset();
	}

	leaveTurnMatch();
	gui.connectionOverlay.reset();
	exportTurnTelemetry();
	turn = nullptr;
	turnMatch.reset();
	net.reset();
}

void Engine::leaveTurnMatch()
{
	if (!turn)
		return;
	Turn::TurnSession& session = turn->turn();
	if (session.state() == Turn::TurnSession::State::Ended || session.state() == Turn::TurnSession::State::Rejected)
		return;
	// GameFinished means the game itself is decided (the relay may then end the match
	// as soon as nobody is connected): the end condition fired, or this colony won. A
	// colony that lost while others play on leaves like any other player.
	const bool finished = gui.game.isGameEnded || gui.game.totalPrestigeReached ||
		(gui.localTeamNo >= 0 && gui.localTeamNo < gui.game.mapHeader.getNumberOfTeams() &&
		 gui.game.teams[gui.localTeamNo] && gui.game.teams[gui.localTeamNo]->hasWon);
	session.quit(finished ? Turn::QuitReason::GameFinished : Turn::QuitReason::PlayerQuit);
}

void Engine::pumpTurnSession(Uint64 now)
{
	if (!turn)
		return;
	Turn::TurnSession& session = turn->turn();
	turnNowMicros = now * 1000;
	session.update(now * 1000);
	printTurnTelemetrySamples();
	if (session.needsReload())
		reloadTurnInitialState();
	if (session.desyncFlagged() && turnMatch && !turnMatch->flagReported)
	{
		turnMatch->flagReported = true;
		std::cerr << "Turn session: the relay flagged a desynchronization at or before tick "
			<< session.executedTick() << "; the match result will be decided by verification" << std::endl;
	}
	if (session.state() == Turn::TurnSession::State::Rejected)
	{
		std::cerr << "Turn session: the relay refused this client (reason "
			<< static_cast<int>(session.rejectReason()) << "); leaving the game" << std::endl;
		gui.isRunning = false;
	}
}

void Engine::reloadTurnInitialState()
{
	assert(turn && turnMatch);
	TurnMatchState& state = *turnMatch;
	const auto started = std::chrono::steady_clock::now();
    MapRenderState appearance;
    gui.swapColonyAppearance(appearance);
	if (!gui.loadFromHeaders(state.map, state.header, true, true, false, state.mapFile))
	{
		std::cerr << "Turn session: cannot reload the initial game state" << std::endl;
		gui.isRunning = false;
		return;
	}
	gui.swapColonyAppearance(appearance);
	globalContainer->liveSpectating = false;
	gui.localPlayer = state.localPlayer;
	gui.localTeamNo = state.localTeam;
	gui.game.clearingUncontrolledTeams();
	finalAdjustments();
	gui.localPlayer = state.localPlayer;
	gui.localTeamNo = state.localTeam;
	if (globalContainer->replayWriter)
	{
		// The replay restarts with the state it now describes.
        auto writer = std::make_unique<ReplayWriter>();
        writer->setSaveObserver(globalContainer->replayWriter->getSaveObserver());
        globalContainer->replayWriter = std::move(writer);
		globalContainer->replayWriter->init(state.replayPath, gui);
	}
	if (checksumSidecar)
	{
		checksumSidecar->close();
		checksumSidecar = std::make_unique<ChecksumSidecarWriter>();
		if (!checksumSidecar->open(state.replayPath, gui.game))
			checksumSidecar.reset();
	}
	if (!globalContainer->structuredHeadless)
		gui.game.map.configureCompute(globalContainer->aiThreads ? globalContainer->aiThreads
			: defaultAIThreadCount(gui.game), Map::ComputeAI);
	teamEliminatedTick.clear();
	if (session)
		session->wasReadyLastTick = true;
	turn->resetOrderAudit();
	turn->turn().telemetry().reloadLoad(static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()));
	turn->turn().reloadDone();
	std::cerr << "Turn session: reloaded the initial state in "
		<< std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
		<< " ms; fast-forwarding" << std::endl;
}

// Body of the outer "play one game and possibly load another" loop in run().
// On entry: the game has been initialised (initGame) and audio/cursor set up.
// On exit: doRunOnceAgain==true means run() should call this again
// (e.g. user picked a save during play); false means run() returns.
//
// Phases of one main-loop iteration:
//   1. updateTickSpeedAndDrawCadence - choose this tick's interval + draw cadence
//   2. pollAutomaticEndingConditions - headless end-condition tripwire
//   3. gui.step                   - GUI input (skipped under --nox / off-cadence)
//   4. gatherAndAdvanceOrders     - push local+AI orders, advance net (if prev tick committed)
//   5. (gate flip) readyNow = net->tickReady()
//   6. executeOrdersAndStep       - run matched orders, replay reader, sim syncStep
//   7. automatic-ending step-count check
//   8. drawSession / sessionDelay  - draw, gameplay capture, host pacing
//   9. handleExitRequest           - drain on exit request
//
// Track order readiness separately for the previous and current ticks.
void Engine::beginSession(Uint64 now)
{
    if (session) throw std::logic_error("Engine session is already active");
    if (!net) throw std::logic_error("Engine session requires an initialized game");
	if (!globalContainer->structuredHeadless)
		gui.game.map.configureCompute(globalContainer->aiThreads ? globalContainer->aiThreads
			: defaultAIThreadCount(gui.game), Map::ComputeAI);
    sessionEndingTarget = globalContainer->automaticEndingSteps;
    MainLoopState st{};
    st.adjustableGameSpeed = gui.canChangeGameSpeed();
    st.speed = st.adjustableGameSpeed ? globalContainer->settings.getGameSpeedStepDuration() : GAME_TICK_MS;
    st.wasReadyLastTick = true;
    st.nextGuiStep = 1;
    st.startTime = now;
    teamEliminatedTick.clear();
    session = st;
    randomRequirement.emplace();
    automaticGameStartTick = now;
	if (!globalContainer->runNoX)
		GAGCore::Recording::recorder().beginMatch(
			globalContainer->replaying ? "replay" : (turn ? "multiplayer" : "single_player"),
			gui.game.mapHeader.getMapName(), gui.localTeamNo, gui.game.stepCounter);
	auto &perf = PerformanceTelemetry::collector();
	if (!perf.enabled && !perf.started)
		perf.reset();
	if (perf.output)
	{
		std::ostringstream metadata;
		metadata << "version=" << VERSION_MINOR << " platform=" << std::quoted(SDL_GetPlatform())
				 << " map_w=" << gui.game.map.getW() << " map_h=" << gui.game.map.getH()
				 << " players=" << gui.game.gameHeader.getNumberOfPlayers()
				 << " teams=" << gui.game.mapHeader.getNumberOfTeams() << " renderer="
				 << (globalContainer->runNoX ? "headless"
					 : (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
						 ? "opengl"
						 : "software")
				 << " width=" << (globalContainer->runNoX ? 0 : globalContainer->gfx->getW())
				 << " height=" << (globalContainer->runNoX ? 0 : globalContainer->gfx->getH());
#ifdef __VERSION__
		metadata << " compiler=" << std::quoted(__VERSION__);
#else
		metadata << " compiler=unknown";
#endif
		metadata << " pointer_bits=" << sizeof(void *) * 8;
		if (const char *label = std::getenv("GLOB2_PERF_BUILD_LABEL"))
			metadata << " build=" << std::quoted(label);
		perf.describe(metadata.str());
	}
	printTurnTelemetrySession();

}

bool Engine::stepSession(Uint64 now)
{
    GAGCore::EventQueue events;
    SDL_Event event;
    if (!globalContainer->runNoX)
        while (SDL_PollEvent(&event)) events.push_back(event);
    return stepSession(now, events.events());
}

bool Engine::stepSession(Uint64 now, const std::vector<SDL_Event>& events)
{
    return guardedSessionStep(*this, [&] { return stepSessionImpl(now, events); });
}

void Engine::abortSession() noexcept
{
    try { stopSimulationThread(); } catch (...) {}
    gui.isRunning = false;
    gui.toLoadGameFileName.clear();
    try { teardownSession(); }
    catch (...)
    {
        std::cerr << "Failure while closing game resources; session cannot continue\n";
        net.reset();
        checksumSidecar.reset();
        globalContainer->datasetWriter.reset();
    }
    if (diagnostics) diagnostics->drain();
    session.reset();
    randomRequirement.reset();
    sessionInput.clear();
    globalContainer->replayWriter.reset();
    PerformanceTelemetry::collector().reset();
}

bool Engine::stepSessionImpl(Uint64 now, const std::vector<SDL_Event>& events)
{
    if (!session) throw std::logic_error("No active engine session");
    if (!gui.isRunning) return false;
    auto& st = *session;
    turnDrawPending = true;
    --st.nextGuiStep;
    for (const auto &event : events) sessionInput.push_back(event);
    return advanceSession(now, [&] {
        if (!globalContainer->runNoX && st.nextGuiStep == 0) {
            // Touch event timestamps use SDL time, which keeps advancing while
            // the session clock is suspended in the background.
            gui.step(sessionInput.events(), SDL_GetTicks());
            sessionInput.clear();
        }
    }, true);
}

bool Engine::simulationStep(Uint64 now)
{
    if (!session) throw std::logic_error("No active engine session");
    if (!gui.isRunning) return false;
    // The client half runs on the main thread (clientStep), with the simulation parked.
    return advanceSession(now, [] {}, false);
}

bool Engine::presentationPaused() const { return gui.gamePaused || gui.hardPause; }

bool Engine::serialClientFrame(Uint64 now, const std::vector<SDL_Event>& events, Uint32 budget)
{
    return guardedSessionStep(*this, [&]() -> bool {
        if (!session) throw std::logic_error("No active engine session");
        const Uint64 started = SDL_GetTicks();
        clientStep(events);
        // Apply speed/pause input before deciding whether a tick is due.
        updateTickSpeedAndDrawCadence(*session, now);
        bool advanced = false;
        while (gui.isRunning)
        {
            const Uint64 elapsed = SDL_GetTicks() - started;
            const Uint64 current = now + elapsed;
            // Slow input/layout work must not starve an already due tick.
            if (sessionDelay(current) != 0 || (advanced && elapsed >= budget)) break;
            advanceSession(current, [] {}, false);
            advanced = true;
        }
        return gui.isRunning;
    });
}

void Engine::clientStep(const std::vector<SDL_Event>& events)
{
    if (!session) throw std::logic_error("No active engine session");
    // Headless sessions never run the GUI step; they only take the notices.
    if (globalContainer->runNoX)
        gui.consumeClientEvents();
    else
        // Match SDL input timestamps, not the suspendable simulation clock.
        gui.threadedClientStep(events, SDL_GetTicks());
    handleExitRequest();
}

void Engine::configureSessionTelemetry(MainLoopState& st, PerformanceTelemetry::Collector& perf)
{
	const bool paused = gui.gamePaused || gui.hardPause;
	const int renderRatio = paused ? 1
							: (globalContainer->replaying && globalContainer->replayFastForward)
								? REPLAY_FAST_FORWARD_DRAW_RATIO
							: st.adjustableGameSpeed
								? globalContainer->settings.getGameSpeedRenderInterval()
								: 1;
	const auto budget = globalContainer->runNoX ? 0ULL : std::uint64_t(st.speed) * 1000000ULL;
	// Threaded drawing presents every display frame; budget it against 60 Hz.
	const auto frameBudget = gui.simulationThreaded ? 16666667ULL : budget * renderRatio;
	perf.configure(gui.game.stepCounter, budget, frameBudget,
				   paused                       ? "paused"
				   : globalContainer->runNoX    ? "headless"
				   : globalContainer->replaying ? "replay"
				   : !st.wasReadyLastTick       ? "waiting"
												: "live");
}

void Engine::absorbSimulationTelemetry()
{
	auto &perf = PerformanceTelemetry::collector();
	perf.absorb(runner->telemetry);
	configureSessionTelemetry(*session, perf);
	perf.capture(gui.game.stepCounter);
}

bool Engine::advanceSession(Uint64 now, const std::function<void()>& clientWork, bool handleExit)
{
    auto& st = *session;
    updateTickSpeedAndDrawCadence(st, now);
    auto &perf = PerformanceTelemetry::collector();
		// Threaded, the main thread configures and captures the session collector
		// after absorbing this thread's window (absorbSimulationTelemetry).
		if (!gui.simulationThreaded)
			configureSessionTelemetry(st, perf);
		PerformanceTelemetry::Scope loopTime(PerformanceTelemetry::Id::Loop);
		PerformanceTelemetry::Scope workTime(PerformanceTelemetry::Id::Work);

    pollAutomaticEndingConditions(now);
    clientWork();

    pumpTurnSession(now);
    gui.updateCommander(turn && turn->turn().tickIntervalMicros()!=0);
    bool readyNow = st.wasReadyLastTick;
    if (!gui.hardPause) {
        gatherAndAdvanceOrders(st.wasReadyLastTick);
        readyNow = net->tickReady();
        const Uint32 tickBefore = gui.game.stepCounter;
        executeOrdersAndStep(readyNow);
        if (gui.game.stepCounter != tickBefore)
            gui.recordTick(SDL_GetTicks(), Uint32(st.speed));
    }
    if (globalContainer->automaticEndingGame && (int)gui.game.stepCounter == sessionEndingTarget) {
        gui.isRunning = false;
        automaticGameEndTick = now;
        printf("nox::gui.game.checkSum() = %08x\n", gui.game.checkSum());
    }

		if (!headlessOutput.empty() && readyNow)
		{
			const auto tick=gui.game.stepCounter;
			if (tick % 256 == 0)
				std::ofstream(headlessOutput + "/progress.jsonl", std::ios::app)
					<< "{\"schema_version\":1,\"tick\":" << tick << "}\n";
			if (headlessSaveInterval > 0 && tick % headlessSaveInterval == 0)
				saveInitialGameStateOrExit(headlessOutput + "/checkpoint-" + std::to_string(tick) + ".game",
					"checkpoint", gui.game.mapHeader.getMapName());
		}

    // A turn game that waited for a late bundle moves its schedule back by up to one
    // tick instead of bursting through the ticks it owes: a bundle that is a little
    // late becomes a little more buffer, which the delay controller drains at up to
    // 5% speed. A longer wait still catches up the rest at once.
    if (turn && readyNow && !st.wasReadyLastTick && !turn->turn().catchingUp())
    {
        const Sint64 late = static_cast<Sint64>(now - st.startTime) - st.needToBeTime;
        if (late > 0) st.needToBeTime += std::min<Sint64>(late, st.speed);
    }
    st.wasReadyLastTick = readyNow;
    // A turn game's budget advances only with executed ticks, so frames spent
    // waiting for the relay poll quickly instead of sleeping a whole tick.
    if (turn ? readyNow : !globalContainer->runNoX) st.needToBeTime += st.speed;
    if (handleExit) handleExitRequest();
    workTime.stop();
    loopTime.stop();
    if (!gui.simulationThreaded)
        perf.capture(gui.game.stepCounter);
    return gui.isRunning;
}

bool Engine::advancePendingSave(const std::vector<SDL_Event>& events)
{
    // The UI may still need to capture a queued manual save. Stop the producer
    // before touching that state, and never resume simulation during teardown.
    stopSimulationThread();
    // Save/retry dialogs share the SDL input clock, just like active gameplay.
    if (gui.savePending()) gui.step(events, SDL_GetTicks());
    return gui.savePending();
}

std::optional<Engine::PendingLoad> Engine::finishSessionForHost()
{
    if (!session) throw std::logic_error("No active engine session");
    stopSimulationThread();
    if (gui.isRunning) throw std::logic_error("Cannot finish a running engine session");
    if (globalContainer->automaticEndingGame) printAutomaticEndingSummary();
    teardownSession();
    auto &perf = PerformanceTelemetry::collector();
	// Structured runs may still write their requested final save after run().
	if (!globalContainer->structuredHeadless)
	{
		perf.capture(gui.game.stepCounter, true, true);
		perf.reset();
	}

    if (diagnostics) diagnostics->drain();
    session.reset();
    randomRequirement.reset();
    sessionInput.clear();
    const auto filename = std::exchange(gui.toLoadGameFileName, {});
    if (gui.exitGlobCompletely || filename.empty()) return std::nullopt;
    // The outgoing end screen will not be shown; finalize its replay before
    // a cooperative initializer creates the next session's writer.
    globalContainer->replayWriter.reset();
    return PendingLoad{filename, globalContainer->replaying};
}

bool Engine::finishSession()
{
    const auto request = finishSessionForHost();
    if (!request) return false;
    return (request->replay ? loadReplay(request->filename) : initCustom(request->filename)) == EE_NO_ERROR;
}

void Engine::runOneGameSession(bool& doRunOnceAgain)
{
    beginSession(SDL_GetTicks());
    if (startSimulationThread(SDL_GetTicks()))
    {
        // The simulation runs on its own thread; this thread handles input and draws
        // at up to about 120 frames per second. Headless runs get here only with the
        // GLOB2_SIM_THREAD test switch and then only take scenes.
        for (;;)
        {
            const Uint64 frameStarted = SDL_GetTicks();
            GAGCore::EventQueue events;
            if (!globalContainer->runNoX)
            {
                SDL_Event event;
                while (SDL_PollEvent(&event)) events.push_back(event);
            }
            if (!threadedClientFrame(SDL_GetTicks(), events.events()))
                break;
            if (globalContainer->runNoX)
            {
                drawSession();
                runner->acquireScene();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            else
            {
                drawSession();
                GAGCore::ApplicationHost::wait(threadedFrameWait(SDL_GetTicks() - frameStarted));
            }
        }
        doRunOnceAgain = finishSession();
        return;
    }
    while (gui.isRunning) {
        stepSession(SDL_GetTicks());
        drawSession();
        if (!globalContainer->runNoX) {
            PerformanceTelemetry::Scope delayTime(waitingOnNetwork()
                ? PerformanceTelemetry::Id::NetworkSleep : PerformanceTelemetry::Id::Sleep);
            Uint32 delay = sessionDelay(SDL_GetTicks());
            // A turn game reads its relay connection while it waits.
            while (turn && delay > TURN_POLL_MS && gui.isRunning)
            {
                GAGCore::ApplicationHost::wait(TURN_POLL_MS);
                pollTurnSession(SDL_GetTicks());
                delay = sessionDelay(SDL_GetTicks());
            }
            GAGCore::ApplicationHost::wait(delay);
        }
    }
    doRunOnceAgain = finishSession();
}

bool Engine::diagnosticsPending() const { return diagnostics && diagnostics->pending(); }
