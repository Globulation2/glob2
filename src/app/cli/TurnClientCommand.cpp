// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// --turn-client: a headless online player. It takes the MatchAssignment a client
// receives in match.start (ticket, relay URL, MatchSetup), connects to the relay over
// WSS and plays the match in real time through the same engine path as the GUI client
// (Engine::initTurnMatch with a RelayTransport). The local seat is played by a simple
// bot that queues ordinary orders, or by nobody; AI seats run locally as on every
// client. Used for end-to-end tests of a deployed instance (docs/hosting/README.md).

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "Building.h"
#include "BuildingType.h"
#include "Engine.h"
#include "Environment.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Headless.h"
#include "MatchSetup.h"
#include "Order.h"
#include "RelayTransport.h"
#include "SimVersion.h"
#include "Team.h"
#include "TurnLockstep.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
using Usage = std::invalid_argument;

struct Options
{
	std::string assignment, map, out, profile = "glob2-turn-client";
	double ordersPerSecond = 0.5;
	double maxSeconds = 1800;
	std::uint32_t seed = 1;
};

std::string readText(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw Usage("cannot read " + path);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

}

/// The --turn-client player (a friend of Engine and GameGUI, like MatchVerifier).
struct TurnClient
{
/// What a player might click, as in src/net/turn/TurnEngineHarness.cpp: building sites and
/// flags near the colony, and worker counts on existing buildings.
static std::shared_ptr<Order> botOrder(Engine& engine, std::mt19937& bot)
{
	Game& game = engine.gui.game;
	if (engine.gui.localTeamNo < 0)
		return nullptr;
	Team* team = game.teams[engine.gui.localTeamNo];
	if (!team || !team->isAlive)
		return nullptr;
	std::uniform_int_distribution<int> offset(-10, 10);
	const int x = team->startPosX + offset(bot), y = team->startPosY + offset(bot);
	switch (bot() % 4)
	{
	case 0:
	case 1:
	{
		static const char* sites[] = {"inn", "swarm", "hospital", "racetrack", "swimmingpool", "school"};
		const int type = globalContainer->buildingsTypes.getTypeNum(sites[bot() % 6], 0, true);
		return std::make_shared<OrderCreate>(team->teamNumber, x, y, type, 1 + bot() % 4, 1 + bot() % 4);
	}
	case 2:
	{
		static const char* flags[] = {"explorationflag", "clearingflag"};
		const int type = globalContainer->buildingsTypes.getTypeNum(flags[bot() % 2], 0, false);
		return std::make_shared<OrderCreate>(team->teamNumber, x, y, type, 1 + bot() % 6, 1 + bot() % 6);
	}
	default:
	{
		std::vector<Uint16> gids;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
			if (const Building* b = team->myBuildings[i])
				if (!b->type->isVirtual && b->type->maxUnitWorking > 0)
					gids.push_back(b->gid);
		if (gids.empty())
			return nullptr;
		return std::make_shared<OrderModifyBuilding>(gids[bot() % gids.size()], 1 + bot() % 8);
	}
	}
}

static int play(const Options& options, const fs::path& output)
{
	const json assignment = json::parse(readText(options.assignment));
	for (const char* key : {"matchId", "seat", "ticket", "relayUrl", "setup"})
		if (!assignment.contains(key))
			throw Usage(std::string("the assignment has no ") + key);
	const std::string matchId = assignment["matchId"].get<std::string>();
	const int seat = assignment["seat"].get<int>();
	const std::string relayUrl = assignment["relayUrl"].get<std::string>();

	Online::MatchSetup setup;
	try
	{
		setup = Online::MatchSetup::parse(assignment["setup"].dump());
	}
	catch (const Online::MatchSetupError& error)
	{
		throw Usage(std::string("the assignment's setup is invalid: ") + error.what());
	}

	GlobalContainer globals(options.profile.c_str());
	globalContainer = &globals;
	globals.runNoX = true;
	globals.structuredHeadless = true;
	globals.automaticEndingGame = false;
	globals.load();

	std::string map;
	try
	{
		map = Online::resolveMatchMap(setup, options.map);
	}
	catch (const Online::MatchSetupError& error)
	{
		throw Usage(error.what());
	}
	const Online::SimVersion local = Online::currentSimVersion();
	if (setup.simVersion != local)
		throw Usage("the match runs sim version " + setup.simVersion.key() + " but this client is " + local.key());

	auto transport = std::make_shared<Online::RelayTransport>(relayUrl);
	Engine engine;
	Engine::TurnMatchStart start;
	start.setup = setup;
	start.mapFile = map;
	start.localSeat = seat;
	start.transport = transport;
	start.config.ticket = assignment["ticket"].get<std::string>();
	// Network telemetry context (ClientNetworkSummary.match); optional in MatchAssignment.
	start.networkKind = "online";
	if (assignment.contains("relayId") && assignment["relayId"].is_string())
		start.relayId = assignment["relayId"].get<std::string>();
	if (assignment.contains("relayRegion") && assignment["relayRegion"].is_string())
		start.relayRegion = assignment["relayRegion"].get<std::string>();
	if (engine.initTurnMatch(start) != Engine::EE_NO_ERROR)
		throw Usage("cannot start the match: " + engine.getInitializationDiagnostic());
	Turn::TurnLockstepSession& lockstep = *engine.turnLockstep();
	std::map<std::uint32_t, Uint32> checksums;
	int reloads = 0;
	lockstep.onChecksum = [&](std::uint32_t tick, Uint32 checksum) {
		if (tick == 0 && !checksums.empty())
			++reloads;
		checksums[tick] = checksum;
	};

	std::mt19937 bot(options.seed * 7919u + static_cast<std::uint32_t>(seat));
	std::uniform_real_distribution<double> uniform(0, 1);
	const auto began = std::chrono::steady_clock::now();
	auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); };
	std::cout << "turn-client: match " << matchId << " seat " << seat << " via " << relayUrl << std::endl;

	engine.beginSession(SDL_GetTicks());
	std::uint32_t ordersQueued = 0;
	double lastReport = 0, firstTickAt = -1;
	bool quitQueued = false, timedOut = false;
	std::string endedBy = "engine";
	for (;;)
	{
		const Uint64 now = SDL_GetTicks();
		Turn::TurnSession& session = lockstep.turn();
		const std::uint32_t before = session.executedTick();
		const bool running = engine.stepSession(now);
		engine.trackTeamEliminations();
		const std::uint32_t after = session.executedTick();
		if (after > 0 && firstTickAt < 0)
			firstTickAt = elapsed();
		// One bot decision per executed tick in real time; none while catching up.
		if (after > before && !session.catchingUp() && options.ordersPerSecond > 0 &&
		    uniform(bot) < options.ordersPerSecond / 25.0)
			if (auto order = botOrder(engine, bot))
			{
				engine.gui.orderQueue.push_back(order);
				++ordersQueued;
			}
		if (session.state() == Turn::TurnSession::State::Rejected)
		{
			endedBy = "rejected";
			break;
		}
		if (!running)
			break;
		Game& game = engine.gui.game;
		if (game.isGameEnded || game.totalPrestigeReached)
		{
			// The end screen's "quit": leave the finished game.
			engine.gui.isRunning = false;
			continue;
		}
		if (!quitQueued && elapsed() > options.maxSeconds)
		{
			// The in-game "quit" menu: a sequenced PlayerQuitsGameOrder, then leave.
			std::cerr << "turn-client: --max-seconds reached; quitting" << std::endl;
			engine.gui.orderQueue.push_back(std::make_shared<PlayerQuitsGameOrder>(seat));
			quitQueued = true;
			timedOut = true;
			endedBy = "max-seconds";
		}
		if (quitQueued && elapsed() > options.maxSeconds + 30)
			break;
		if (elapsed() - lastReport >= 10)
		{
			lastReport = elapsed();
			std::cout << "turn-client: seat " << seat << " tick " << after << " state " << int(session.state())
			          << " rtt " << session.rttMicros() / 1000 << "ms jitter " << session.jitterMicros() / 1000
			          << "ms orders " << ordersQueued << std::endl;
		}
		const Uint32 delay = engine.sessionDelay(now);
		if (delay > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(std::min<Uint32>(delay, 20)));
	}
	Turn::TurnSession& session = lockstep.turn();
	const auto finalState = session.state();
	const std::uint32_t finalTick = session.executedTick();
	const auto rtt = session.rttMicros(), jitter = session.jitterMicros();
	const bool desync = session.desyncFlagged();
	if (engine.gui.isRunning)
		engine.gui.isRunning = false;
	engine.finishSessionForHost();
	// Let the transport flush the Quit before the process exits.
	const auto flushUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < flushUntil &&
	       transport->state() == Turn::TurnTransport::State::Connected)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

	Game& game = engine.gui.game;
	std::ostringstream result;
	result << "{\"schema_version\":1,\"job_type\":\"turn_client\",\"status\":\"completed\",\"match_id\":"
	       << Headless::quote(matchId) << ",\"seat\":" << seat << ",\"sim_version\":" << Headless::quote(local.key())
	       << ",\"ended_by\":" << Headless::quote(endedBy) << ",\"session_state\":" << int(finalState)
	       << ",\"executed_ticks\":" << finalTick << ",\"game_ended\":" << (game.isGameEnded ? "true" : "false")
	       << ",\"timed_out\":" << (timedOut ? "true" : "false") << ",\"desync_flagged\":" << (desync ? "true" : "false")
	       << ",\"reloads\":" << reloads << ",\"orders_queued\":" << ordersQueued
	       << ",\"rtt_ms\":" << rtt / 1000 << ",\"jitter_ms\":" << jitter / 1000
	       << ",\"wall_seconds\":" << elapsed() << ",\"first_tick_seconds\":" << firstTickAt
	       << ",\"relay_error\":" << Headless::quote(transport->error()) << ",";
	Headless::playersAndTeamsJson(result, game, engine.teamEliminatedTick);
	result << '}';
	Headless::writeJson((output / "result.json").string(), result.str());
	{
		std::ofstream trace(output / "checksums.txt", std::ios::binary);
		trace << "# glob2 turn-client trace v1: tick, then the state checksum before that tick\n";
		for (const auto& [tick, checksum] : checksums)
		{
			char hex[9];
			std::snprintf(hex, sizeof hex, "%08x", checksum);
			trace << tick << ' ' << hex << '\n';
		}
	}
	Headless::writeManifest(output.string());
	std::cout << "turn-client: seat " << seat << " finished at tick " << finalTick << " (" << endedBy << ")"
	          << std::endl;
	return finalState == Turn::TurnSession::State::Rejected ? 4 : 0;
}
};

int runTurnClient(int argc, char** argv)
{
	fs::path output;
	try
	{
		if (argc < 3)
			throw Usage("usage: --turn-client <assignment.json> --map <file> --out <dir> [--orders-per-second R] "
			            "[--max-seconds S] [--seed N] [--profile <name>]");
		Options options;
		options.assignment = argv[2];
		for (int i = 3; i < argc; i += 2)
		{
			const std::string key = argv[i];
			if (i + 1 >= argc)
				throw Usage("missing value for " + key);
			const std::string value = argv[i + 1];
			if (key == "--map")
				options.map = value;
			else if (key == "--out")
				options.out = value;
			else if (key == "--profile")
				options.profile = value;
			else if (key == "--orders-per-second")
				options.ordersPerSecond = std::stod(value);
			else if (key == "--max-seconds")
				options.maxSeconds = std::stod(value);
			else if (key == "--seed")
				options.seed = static_cast<std::uint32_t>(std::stoul(value));
			else
				throw Usage("unknown option: " + key);
		}
		if (options.map.empty() || options.out.empty())
			throw Usage("--map and --out are required");
		output = fs::absolute(options.out);
		fs::create_directories(output / "profile");
		// Both profile variables, as the other headless commands set them (#554).
		const std::string profileDir = (output / "profile").string();
		GAGCore::setProcessEnvironment("GLOB2_USER_DIR", profileDir.c_str(), 1);
		GAGCore::setProcessEnvironment("GLOB2_USER_DATA_DIR", profileDir.c_str(), 1);
		if (fs::exists(output / "result.json"))
			throw Usage("output directory already contains a result");
		options.assignment = fs::absolute(options.assignment).string();
		options.map = fs::absolute(options.map).string();
		return TurnClient::play(options, output);
	}
	catch (const std::exception& error)
	{
		std::cerr << "turn-client: " << error.what() << std::endl;
		const bool invalid = dynamic_cast<const std::invalid_argument*>(&error) != nullptr;
		if (!output.empty() && !fs::exists(output / "result.json"))
			try
			{
				Headless::writeJson((output / "result.json").string(),
				                    "{\"schema_version\":1,\"job_type\":\"turn_client\",\"status\":" +
				                        Headless::quote(invalid ? "invalid_request" : "failure") +
				                        ",\"diagnostic\":" + Headless::quote(error.what()) + "}");
			}
			catch (...)
			{
			}
		return invalid ? 2 : 3;
	}
}
