// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FormatableString.h>

#include "AINames.h"
#include "ChecksumSidecar.h"
#include "DatasetWriter.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "SDLCompat.h"
#include "team/Team.h"
#include "TeamStat.h"
#include "building/IntBuildingType.h"
#include "unit/UnitConsts.h"

#include <iostream>

using std::shared_ptr;


void Engine::updateTickSpeedAndDrawCadence(MainLoopState& st)
{
	if (globalContainer->replaying)
	{
		if (globalContainer->replayFastForward && !gui.gamePaused)
		{
			st.speed = REPLAY_FAST_FORWARD_MS;
			if (st.nextGuiStep < 0) st.nextGuiStep = REPLAY_FAST_FORWARD_DRAW_RATIO - 1;
		}
		else
		{
			st.speed = GAME_TICK_MS;
			if (st.nextGuiStep < 0) st.nextGuiStep = 0;
		}
	}
	else
	{
		// Process the GUI as usual, every step
		st.nextGuiStep = 0;
	}
}

// Headless / scripted-test polling: under --nox automaticEndingGame, flip
// gui.isRunning=false once a local end condition fires (local team dead, local
// team won, total-prestige reached, game ended). Records automaticGameEndTick.
void Engine::pollAutomaticEndingConditions()
{
	if (!globalContainer->automaticEndingGame)
		return;

	auto endGame = [this](const char* reason) {
		printf("nox::%s\n", reason);
		gui.isRunning = false;
		automaticGameEndTick = SDL_GetTicks64();
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

// Push this tick's local + AI orders into the network layer. AI poll,
// setWaitingOnMask, and multiplayer->update() always run; the "previous tick
// committed" branches (syncStep, addLocalOrder, advanceStep, sidecar) only
// fire when wasReadyLastTick — otherwise we're still waiting on a remote peer
// and must not advance.
void Engine::gatherAndAdvanceOrders(bool wasReadyLastTick)
{
	// But some jobs have to be executed synchronously:
	if (wasReadyLastTick)
	{
		gui.syncStep();

		// The gui.localPlayer may have been updated (in replays)
		// Keep them synchronized here
		net->setLocalPlayer(gui.localPlayer);

		// We get and push local orders
		shared_ptr<Order> localOrder = gui.getOrder();
		net->addLocalOrder(localOrder);
	}

	// we get and push ai orders, if they are needed for this frame
	for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (gui.game.players[i]->ai && !net->orderRecieved(i))
		{
			shared_ptr<Order> order = gui.game.players[i]->ai->getOrder(gui.gamePaused);
			net->pushOrder(order, i, true);
		}
	}

	gui.game.setWaitingOnMask(net->getWaitingOnMask());

	if (multiplayer)
		multiplayer->update();

	if (wasReadyLastTick)
	{
		Uint32 checksum = gui.game.checkSum(NULL, NULL, NULL);
		net->advanceStep(checksum);

		// Enable this to do test if checksums in the replay match
		//if (globalContainer->replayReader) globalContainer->replayReader->setCheckSum(checksum);
		if (globalContainer->replayWriter) globalContainer->replayWriter->setCheckSum(checksum);

		if (checksumSidecar)
			checksumSidecar->writeTick(gui.game.stepCounter, checksum, gui.game);
	}
}

// Once allOrdersRecieved() is true for this tick, commit the tick: validate
// checksums (assert on desync), execute the matched orders, pump the replay
// reader if we're in playback, and run game.syncStep. Called only from inside
// the !hardPause branch, so the original !gui.hardPause guard on syncStep is
// implicit here.
void Engine::executeOrdersAndStep(bool readyNow)
{
	if (readyNow)
	{
		if (!net->matchCheckSums())
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
	}
}

void Engine::drawAndPaceFrame(MainLoopState& st)
{
	if (st.nextGuiStep == 0)
	{
		gui.drawAll(gui.localTeamNo);
		globalContainer->gfx->nextFrame();
	}

	// if required, save videoshot
	if (!(globalContainer->videoshotName.empty()) &&
		!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
		)
	{
		FormatableString fileName = FormatableString("videoshots/%0.%1.bmp").arg(globalContainer->videoshotName).arg(st.frameNumber++, 10, 10, '0');
		printf("printing video shot %s\n", fileName.c_str());
		globalContainer->gfx->printScreen(fileName.c_str());
	}

	// we compute timing
	st.needToBeTime += st.speed;
	Sint64 currentTime = static_cast<Sint64>(SDL_GetTicks64()) - static_cast<Sint64>(st.startTime);
	//if we are more than MAX_CATCHUP_MS milliseconds behind where we should be,
	//then truncate it. This is to avoid playing "catchup" for long
	//periods of time if Glob2 recieved allmost no cpu time
	if ((currentTime - st.needToBeTime) > MAX_CATCHUP_MS)
		st.needToBeTime = currentTime - MAX_CATCHUP_MS;

	//Any inconsistancies in the delays will be smoothed throughout the following frames,
	Uint64 delay = std::max<Sint64>(0, st.needToBeTime - currentTime);
	SDL_Delay(delay);

	// we set CPU stats
	// Convert slept time into CPU load for one game tick.
	const int loadPercent = static_cast<int>(
		(GAME_TICK_MS * 100 - delay * 100) / GAME_TICK_MS);
	gui.setCpuLoad(loadPercent);
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
			std::cout << AINames::getAIText(BasePlayer::implementitionIdFromPlayerType(bp.type));
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
}

// Per-team timeline dump (see GLOB2_TEAM_TIMELINE in printAutomaticEndingSummary).
void Engine::printTeamTimeline()
{
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
			aiLabel[bp.teamNumber] = AINames::getAIText(BasePlayer::implementitionIdFromPlayerType(bp.type));
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

// Tell the YOG multiplayer session how this match ended (won, lost, quit) so
// it can update ratings. Caller must check `multiplayer` is non-null first.
void Engine::reportMultiplayerResult()
{
	if (gui.game.totalPrestigeReached)
	{
		Team *t = gui.game.getTeamWithMostPrestige();
		assert(t);
		if (t == gui.getLocalTeam())
		{
			multiplayer->setGameResult(YOGGameResultWonGame);
		}
		else
		{
			if ((t->allies) & (gui.getLocalTeam()->me))
				multiplayer->setGameResult(YOGGameResultWonGame);
			else
				multiplayer->setGameResult(YOGGameResultLostGame);
		}
	}
	else if (gui.getLocalTeam()->hasWon)
	{
		multiplayer->setGameResult(YOGGameResultWonGame);
	}
	else if (!gui.getLocalTeam()->isAlive)
	{
		multiplayer->setGameResult(YOGGameResultLostGame);
	}
	else if (!gui.game.isGameEnded)
	{
		multiplayer->setGameResult(YOGGameResultQuitGame);
	}
}

// Close cross-replay debug sinks (sidecar, dataset) and tear down the network
// + multiplayer session. The Engine itself stays alive for a possible reload.
void Engine::teardownSession()
{
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

	net.reset();
	multiplayer.reset();
}

// Decide whether run() should loop back into runOneGameSession (e.g. the GUI
// armed a load-game request) or return to the menu. Always clears
// toLoadGameFileName afterwards so the next pass doesn't re-trigger it.
void Engine::prepareNextGameSession(bool& doRunOnceAgain)
{
	if (gui.exitGlobCompletely)
	{
		doRunOnceAgain = false;
		return; // There is no bypass for the "close window button"
	}

	doRunOnceAgain = false;

	if (!gui.toLoadGameFileName.empty())
	{
		int rv;

		// A new game session is starting, so no EndGameScreen will be shown
		// for the finished one: finalize its replay now (ReplayWriter::finish
		// writes the NullOrder terminator and flushes). initGame requires the
		// writer slot to be empty before it allocates the next session's.
		globalContainer->replayWriter.reset();

		if (globalContainer->replaying) rv = loadReplay(gui.toLoadGameFileName);
		else rv = initCustom(gui.toLoadGameFileName);

		if (rv == EE_NO_ERROR)
			doRunOnceAgain = true;
		gui.toLoadGameFileName.clear(); // Avoid the communication system between GameGUI and Engine to loop.
	}
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
//   5. (gate flip) readyNow = net->allOrdersRecieved()
//   6. executeOrdersAndStep       - run matched orders, replay reader, sim syncStep
//   7. automatic-ending step-count check
//   8. drawAndPaceFrame            - draw, videoshot, sleep
//   9. handleExitRequest           - drain on exit request
//
// Track order readiness separately for the previous and current ticks.
void Engine::runOneGameSession(bool& doRunOnceAgain)
{
	MainLoopState st;
	st.speed = GAME_TICK_MS;
	st.wasReadyLastTick = true;
	// If playing in fast-forward, we process the GUI and draw everything only
	// once every 3 game-steps so the overall fps stays about the same.
	st.nextGuiStep = 1;
	st.needToBeTime = 0;
	st.startTime = SDL_GetTicks64();
	st.frameNumber = 0;

	while (gui.isRunning)
	{
		st.nextGuiStep--;
		updateTickSpeedAndDrawCadence(st);

		pollAutomaticEndingConditions();

		if (!globalContainer->runNoX && st.nextGuiStep == 0)
			gui.step();

		// Hard pause skips the readiness update, so carry the previous value forward.
		bool readyNow = st.wasReadyLastTick;

		if (!gui.hardPause)
		{
			if (multiplayer && multiplayer->getMultiplayerMode() == MultiplayerGame::NoMode)
				gui.isRunning = false;

			gatherAndAdvanceOrders(st.wasReadyLastTick);

			// Gate flip: from "previous tick committed" to "all orders for
			// this tick are now in." Downstream helpers take readyNow, not
			// wasReadyLastTick.
			readyNow = net->allOrdersRecieved();

			executeOrdersAndStep(readyNow);
		}

		if (globalContainer->automaticEndingGame)
		{
			if ((int)gui.game.stepCounter == globalContainer->automaticEndingSteps)
			{
				gui.isRunning = false;
				automaticGameEndTick = SDL_GetTicks64();
				printf("nox::gui.game.checkSum() = %08x\n", gui.game.checkSum());
			}
		}

		if (!globalContainer->runNoX)
			drawAndPaceFrame(st);

		if (handleExitRequest())
			break;

		st.wasReadyLastTick = readyNow;
	}

	if (globalContainer->automaticEndingGame)
		printAutomaticEndingSummary();

	if (multiplayer)
		reportMultiplayerResult();

	teardownSession();

	prepareNextGameSession(doRunOnceAgain);
}
