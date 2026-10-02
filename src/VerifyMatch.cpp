// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// --verify-match: replays a relay match record through the same engine path live
// clients run (Engine::initTurnMatch with a RecordTransport in place of the relay)
// and compares every checksum the clients reported with the verifier's own.

#include "VerifyMatch.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

#include "Engine.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Headless.h"
#include "ReplayWriter.h"
#include "Sha256.h"
#include "SimVersion.h"
#include "TurnLockstep.h"
#include "Version.h"
#include "WinningConditions.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
std::string fileSha256(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return {};
	Online::Sha256 hash;
	char buffer[65536];
	while (in)
	{
		in.read(buffer, sizeof buffer);
		hash.update(buffer, static_cast<std::size_t>(in.gcount()));
	}
	return Online::toHex(hash.finish());
}

std::string hex32(std::uint32_t value)
{
	std::ostringstream out;
	out << std::hex << std::setw(8) << std::setfill('0') << value;
	return out.str();
}

using Usage = std::invalid_argument;
}

MatchVerifier::Verdict MatchVerifier::judge(const Turn::MatchRecord& record,
                                            const std::map<std::uint32_t, Uint32>& checksums,
                                            const std::string& versionProblem)
{
	Verdict v;
	v.checksums = checksums;
	std::set<int> reporters, diverged;
	for (const auto& report : record.reports)
	{
		auto it = checksums.find(report.tick);
		if (it == checksums.end())
			continue; // beyond the record's end; nothing to compare with
		reporters.insert(report.seat);
		++v.compared;
		if (it->second != report.checksum)
		{
			diverged.insert(report.seat);
			if (!v.firstDivergence.count(report.seat))
				v.firstDivergence[report.seat] = report.tick;
		}
	}
	if (!versionProblem.empty())
	{
		v.verdict = "unverifiable";
		v.reason = versionProblem;
	}
	else if (reporters.empty())
	{
		v.verdict = "unverifiable";
		v.reason = "the record holds no checksum reports to compare";
	}
	else if (diverged.size() == reporters.size())
	{
		std::uint32_t first = UINT32_MAX;
		for (const auto& [seat, tick] : v.firstDivergence)
			first = std::min(first, tick);
		v.verdict = "unverifiable";
		v.reason = "no client matches the verifier; the first mismatch is at tick " + std::to_string(first) +
		           " (engine nondeterminism or a corrupt record)";
	}
	else if (diverged.empty())
		v.verdict = "verified";
	else
	{
		v.verdict = "diverged";
		v.seats.assign(diverged.begin(), diverged.end());
	}
	return v;
}

MatchVerifier::Verdict MatchVerifier::verify(const Turn::MatchRecord& record, const Online::MatchSetup& setup,
                                             const std::string& mapPath, const fs::path& output)
{
	GlobalContainer& globals = *globalContainer;
	globals.runNoX = true;
	globals.structuredHeadless = true;
	globals.automaticEndingGame = false;
	globals.headlessReplay = false;

	std::string map;
	try
	{
		map = Online::resolveMatchMap(setup, mapPath);
	}
	catch (const Online::MatchSetupError& error)
	{
		throw Usage(error.what());
	}

	const Online::SimVersion current = Online::currentSimVersion();
	std::string versionProblem;
	if (setup.simVersion != current)
		versionProblem = "the match ran sim version " + setup.simVersion.key() + " but this verifier is " + current.key();
	else if (!record.simVersion.empty() && record.simVersion != current.key())
		versionProblem = "the record names sim version " + record.simVersion + " but this verifier is " + current.key();

	const auto runStart = std::chrono::steady_clock::now();
	Engine engine;
	Engine::TurnMatchStart start;
	start.setup = setup;
	start.mapFile = map;
	start.localSeat = -1;
	std::uint8_t viewSeat = 0;
	for (const auto& seat : setup.seats)
		if (seat.human)
		{
			viewSeat = static_cast<std::uint8_t>(seat.seat);
			break;
		}
	start.transport = std::make_shared<Turn::RecordTransport>(record, viewSeat);
	if (engine.initTurnMatch(start) != Engine::EE_NO_ERROR)
		throw Usage("cannot start the match: " + engine.getInitializationDiagnostic());

	const std::string replay = (output / "match.replay").string();
	globals.headlessReplay = true;
	globals.replayWriter = std::make_unique<ReplayWriter>();
	globals.replayWriter->init(replay, engine.gui);

	std::map<std::uint32_t, Uint32> checksums;
	Turn::TurnLockstepSession& lockstep = *engine.turnLockstep();
	lockstep.onChecksum = [&checksums](std::uint32_t tick, Uint32 checksum) { checksums.emplace(tick, checksum); };

	// Execute every tick below endTick. A seat's recorded PlayerQuitsGameOrder stops
	// a live client that plays that seat; the verifier plays none, so it keeps going.
	engine.beginSession(0);
	Uint64 now = 0;
	std::uint32_t lastProgress = 0;
	int idle = 0;
	while (lockstep.turn().executedTick() < record.endTick)
	{
		engine.stepSession(++now);
		engine.trackTeamEliminations();
		engine.gui.isRunning = true;
		if (lockstep.turn().executedTick() != lastProgress)
		{
			lastProgress = lockstep.turn().executedTick();
			idle = 0;
		}
		else if (++idle > 10000)
			throw std::runtime_error("the verifier stalled at tick " + std::to_string(lastProgress));
	}
	// The checksum a client would report for endTick itself: the state before the
	// next tick, taken where the engine takes it.
	engine.gatherAndAdvanceOrders(true);
	engine.trackTeamEliminations();
	const auto runEnd = std::chrono::steady_clock::now();
	Game& game = engine.gui.game;
	const Uint32 finalTick = game.stepCounter;

	Verdict verdict = judge(record, checksums, versionProblem);

	{
		std::ofstream trace(output / "checksums.txt", std::ios::binary);
		trace << "# glob2 verify-match trace v1: tick, then the state checksum before that tick\n";
		for (const auto& [tick, checksum] : checksums)
			trace << tick << ' ' << hex32(checksum) << '\n';
		if (!trace)
			throw std::runtime_error("cannot write checksums.txt");
	}

	json verification = {{"verdict", verdict.verdict},
	                     {"compared_reports", verdict.compared},
	                     {"reports", record.reports.size()},
	                     {"verified_ticks", checksums.size()}};
	if (verdict.verdict == "diverged")
		verification["seats"] = verdict.seats;
	if (verdict.verdict == "unverifiable")
		verification["reason"] = verdict.reason;
	json first = json::object();
	for (const auto& [seat, tick] : verdict.firstDivergence)
		first[std::to_string(seat)] = tick;
	verification["first_divergence"] = first;

	// No wall-clock fields: the same record verifies to the same bytes everywhere.
	std::ostringstream result;
	result << "{\"schema_version\":1,\"job_type\":\"verify_match\",\"status\":\"completed\",\"ticks\":" << finalTick
	       << ",\"end_tick\":" << record.endTick << ",\"match_id\":" << Headless::quote(record.matchId)
	       << ",\"sim_version\":" << Headless::quote(setup.simVersion.key())
	       << ",\"verifier_sim_version\":" << Headless::quote(current.key()) << ",\"record_flags\":{\"desync_flagged\":"
	       << ((record.flags & Turn::MatchRecord::FLAG_DESYNC_FLAGGED) ? "true" : "false")
	       << ",\"incomplete\":" << ((record.flags & Turn::MatchRecord::FLAG_INCOMPLETE) ? "true" : "false")
	       << "},\"game_seed\":" << game.gameHeader.getRandomSeed() << ",\"termination\":"
	       << Headless::quote(game.isGameEnded || game.totalPrestigeReached ? "engine_end" : "record_end")
	       << ",\"resolved\":{\"map\":" << Headless::quote(game.mapHeader.getMapName())
	       << ",\"map_hash\":" << Headless::quote(setup.map.hash) << ",\"save_version\":" << VERSION_MINOR
	       << ",\"winning_conditions\":[";
	bool comma = false;
	for (const auto& c : game.gameHeader.getWinningConditions())
	{
		if (comma)
			result << ',';
		comma = true;
		result << int(c->getType());
	}
	result << "],\"experiments\":[";
	comma = false;
	for (const auto& key : game.gameHeader.getExperiments().keys())
	{
		if (comma)
			result << ',';
		comma = true;
		result << Headless::quote(key);
	}
	result << "]},";
	Headless::playersAndTeamsJson(result, game, engine.teamEliminatedTick);
	result << ",\"verification\":" << verification.dump() << '}';
	Headless::writeJson((output / "result.json").string(), result.str());

	json teams = json::array();
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
	{
		const Team* team = game.teams[t];
		json entry = {{"team", t},
		              {"outcome", team->hasWon ? "won" : team->hasLost ? "lost" : "unresolved"},
		              {"prestige", team->prestige}};
		if (t < static_cast<int>(engine.teamEliminatedTick.size()) && engine.teamEliminatedTick[t] >= 0)
			entry["eliminatedTick"] = engine.teamEliminatedTick[t];
		teams.push_back(entry);
	}

	// Close the session and finish the replay file before hashing it.
	engine.abortSession();
	globals.replayWriter.reset();

	json outcome = {{"finalTick", finalTick},
	                {"teams", teams},
	                {"resultHash", fileSha256(output / "result.json")},
	                {"replayHash", fileSha256(replay)}};

	// verdict.json: {verdict, seats (diverged), reason (unverifiable)} for the engine
	// agent, plus the protocol package's VerifyVerdict members (clients, outcome), so
	// the same file is also a valid VerifyVerdict.
	json verdictJson = {{"verdict", verdict.verdict}};
	if (verdict.verdict == "diverged")
	{
		verdictJson["seats"] = verdict.seats;
		verdictJson["clients"] = verdict.seats;
	}
	if (verdict.verdict == "unverifiable")
		verdictJson["reason"] = verdict.reason;
	else
		verdictJson["outcome"] = outcome;
	Headless::writeJson((output / "verdict.json").string(), verdictJson.dump());
	Headless::writeManifest(output.string());

	std::cout << "verify-match: " << verdict.verdict;
	for (int seat : verdict.seats)
		std::cout << " seat" << seat;
	if (!verdict.reason.empty())
		std::cout << " (" << verdict.reason << ")";
	std::cout << "; " << checksums.size() << " checksums in "
	          << std::chrono::duration_cast<std::chrono::milliseconds>(runEnd - runStart).count() << " ms" << std::endl;
	return verdict;
}

int MatchVerifier::run(const std::string& recordPath, const std::string& mapPath, const fs::path& output,
                       const std::string& profile)
{
	// Record and setup first: a defect here is a bad request, not a verdict.
	Turn::MatchRecord record;
	try
	{
		record = Turn::MatchRecord::readFile(recordPath);
	}
	catch (const std::exception& error)
	{
		throw Usage(std::string("cannot read the match record: ") + error.what());
	}
	Online::MatchSetup setup;
	try
	{
		setup = Online::MatchSetup::parse(record.setupJson);
	}
	catch (const Online::MatchSetupError& error)
	{
		throw Usage(std::string("the record's setup is invalid: ") + error.what());
	}
	if (Online::toHex(record.mapHash.data(), record.mapHash.size()) != setup.map.hash)
		throw Usage("the record's map hash differs from its setup's map.hash");
	if (record.humanSeatMask != setup.humanSeatMask())
		throw Usage("the record's human seats differ from its setup's");

	GlobalContainer globals(profile.c_str());
	globalContainer = &globals;
	globals.runNoX = true;
	globals.structuredHeadless = true;
	globals.load();
	verify(record, setup, mapPath, output);
	return 0;
}

int runVerifyMatch(int argc, char** argv)
{
	fs::path output;
	try
	{
		if (argc < 3)
			throw Usage("usage: --verify-match <record> --map <file> --out <dir> [--profile <name>]");
		const std::string record = argv[2];
		std::string map, out, profile = "glob2-verify";
		for (int i = 3; i < argc; i += 2)
		{
			const std::string key = argv[i];
			if (i + 1 >= argc)
				throw Usage("missing value for " + key);
			if (key == "--map")
				map = argv[i + 1];
			else if (key == "--out" || key == "--output-dir")
				out = argv[i + 1];
			else if (key == "--profile")
				profile = argv[i + 1];
			else
				throw Usage("unknown option: " + key);
		}
		if (map.empty() || out.empty())
			throw Usage("--map and --out are required");
		output = fs::absolute(out);
		fs::create_directories(output / "profile");
		SDL_setenv("GLOB2_USER_DIR", (output / "profile").string().c_str(), 1);
		if (fs::exists(output / "result.json"))
			throw Usage("output directory already contains a result");
		return MatchVerifier::run(fs::absolute(record).string(), fs::absolute(map).string(), output, profile);
	}
	catch (const std::exception& error)
	{
		std::cerr << "verify-match: " << error.what() << std::endl;
		const bool invalid = dynamic_cast<const std::invalid_argument*>(&error) != nullptr;
		if (!output.empty() && !fs::exists(output / "result.json"))
			try
			{
				Headless::writeJson((output / "result.json").string(),
				                    "{\"schema_version\":1,\"job_type\":\"verify_match\",\"status\":" +
				                        Headless::quote(invalid ? "invalid_request" : "artifact_failure") +
				                        ",\"diagnostic\":" + Headless::quote(error.what()) + "}");
				Headless::writeManifest(output.string());
			}
			catch (...)
			{
			}
		return invalid ? 2 : 3;
	}
}
