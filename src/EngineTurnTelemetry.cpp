// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Network telemetry output for turn games (docs/development/network-telemetry.md).
// Reads TurnSession's counters only: nothing here feeds back into the game, its
// orders, checksums, replays or saves.

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <FileManager.h>
#include <PerformanceTelemetry.h>
#include <Toolkit.h>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "Engine.h"
#include "GlobalContainer.h"
#include "OrderValidation.h"
#include "ReplayWriter.h"
#include "TurnLockstep.h"

namespace
{
bool timelineOutput()
{
	return std::getenv("GLOB2_TEAM_TIMELINE") != nullptr;
}

void writeSummary(std::ostream& out, const char* name, const Turn::SessionTelemetry::Summary& s)
{
	out << ' ' << name << ".count=" << s.count;
	if (!s.count)
		out << ' ' << name << ".mean=na " << name << ".p50=na " << name << ".p95=na " << name << ".max=na";
	else
		out << ' ' << name << ".mean=" << s.mean << ' ' << name << ".p50=" << s.p50 << ' ' << name << ".p95=" << s.p95
		    << ' ' << name << ".max=" << s.max;
}

std::string networkSidecarPath(const std::string& replayPath)
{
	const std::string suffix = ".replay";
	std::string base = replayPath;
	if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
		base.resize(base.size() - suffix.size());
	return base + ".network.json";
}
}

bool Engine::waitingOnNetwork() const
{
	if (turn)
		return turn->turn().waitingOnNetwork();
	return session && !session->wasReadyLastTick;
}

nlohmann::json Engine::turnNetworkSummary(bool includeSeries) const
{
	if (!turn || !turnMatch || turnMatch->localSeat < 0)
		return nullptr;
	const Turn::TurnSession& s = turn->turn();
	Turn::ClientNetworkContext context;
	context.simVersion = turnMatch->simVersion;
	context.platform = SDL_GetPlatform();
	context.transport = turnMatch->networkKind;
	context.relayId = turnMatch->relayId;
	context.relayRegion = turnMatch->relayRegion;
	context.seat = turnMatch->localSeat;
	context.humanSeatMask = s.humanSeatMask();
	context.players = gui.game.gameHeader.getNumberOfPlayers();
	context.tickRateMilliHz = s.tickRateMilliHz();
	context.finalTick = gui.game.stepCounter;
	// The deterministic order check (OrderValidation.h), when the engine installed it:
	// per human seat, the verdicts since the game last (re)started from tick 0.
	if (turn->validator)
	{
		context.orderValidationAvailable = true;
		const OrderValidation::Audit& audit = turn->orderAudit();
		for (unsigned seat = 0; seat < OrderValidation::Audit::SEATS && seat < 32; ++seat)
		{
			if (!(context.humanSeatMask & (1u << seat)))
				continue;
			const OrderValidation::SeatAudit& a = audit.seats[seat];
			Turn::ClientNetworkContext::SeatVerdicts v;
			v.seat = static_cast<int>(seat);
			v.accepted = a.accepted;
			v.stale = a.stale;
			v.rejected = a.rejected;
			v.voiceRejected = a.voiceRejected;
			for (std::size_t r = 1; r < OrderValidation::REASON_COUNT; ++r)
				if (a.reasons[r])
					v.reasons.emplace_back(OrderValidation::name(static_cast<OrderValidation::Reason>(r)), a.reasons[r]);
			context.orderValidation.push_back(std::move(v));
		}
	}
	return Turn::clientNetworkSummary(s.telemetry(), context, s.nowMicros(), includeSeries);
}

void Engine::printTurnTelemetrySession()
{
	if (!turn || !turnMatch || turnMatch->localSeat < 0 || !timelineOutput())
		return;
	const Turn::TurnSession& s = turn->turn();
	std::cout << "GLOB2_NET_SESSION session=" << PerformanceTelemetry::collector().session
	          << " schema=ClientNetworkSummary schema_version=" << Turn::CLIENT_NETWORK_SUMMARY_VERSION
	          << " transport=" << turnMatch->networkKind << " seat=" << turnMatch->localSeat
	          << " sim_version=" << turnMatch->simVersion
	          << " interval_us=" << s.telemetry().intervalMicros << " unit=us clock=steady" << std::endl;
}

void Engine::printTurnTelemetrySamples()
{
	if (!turn || !turnMatch || turnMatch->localSeat < 0 || !timelineOutput())
		return;
	const auto& points = turn->turn().telemetry().series();
	const auto session = PerformanceTelemetry::collector().session;
	for (; turnMatch->printedNetPoints < points.size(); ++turnMatch->printedNetPoints)
	{
		const auto& p = points[turnMatch->printedNetPoints];
		std::cout << "GLOB2_NET_SAMPLE session=" << session << " seat=" << turnMatch->localSeat
		          << " tick_start=" << p.startTick << " tick=" << p.endTick << " elapsed_start_us=" << p.startMicros
		          << " elapsed_us=" << p.endMicros;
		writeSummary(std::cout, "rtt_us", p.rtt);
		writeSummary(std::cout, "jitter_us", p.jitter);
		writeSummary(std::cout, "input_delay_us", p.inputDelay);
		writeSummary(std::cout, "buffer_ticks", p.buffer);
		writeSummary(std::cout, "target_ticks", p.target);
		writeSummary(std::cout, "stall_us", p.stall);
		std::cout << " bytes_sent=" << p.bytesSent << " bytes_received=" << p.bytesReceived
		          << " frames_sent=" << p.framesSent << " frames_received=" << p.framesReceived
		          << " ticks_executed=" << p.ticksExecuted << " live_ticks=" << p.liveTicks
		          << " ticks_faster=" << p.ticksFaster << " ticks_slower=" << p.ticksSlower
		          << " mean_nudge=" << p.meanNudge << " catch_up_ticks=" << p.catchUpTicks
		          << " reconnects=" << p.reconnects << " downtime_us=" << p.downtimeMicros
		          << " orders_submitted=" << p.ordersSubmitted << " voice_sent=" << p.voiceSent
		          << " voice_received=" << p.voiceReceived << " presence_transitions=" << p.presenceTransitions
		          << '\n';
	}
}

void Engine::exportTurnTelemetry()
{
	if (!turn || !turnMatch || turnMatch->localSeat < 0 || turnMatch->netExported)
		return;
	turnMatch->netExported = true;
	PERF_SCOPE_TIME(Output);
	const Turn::TurnSession& s = turn->turn();
	if (timelineOutput())
	{
		printTurnTelemetrySamples();
		const auto session = PerformanceTelemetry::collector().session;
		std::cout << "GLOB2_NET_FINAL session=" << session << " seat=" << turnMatch->localSeat
		          << " tick=" << gui.game.stepCounter << " elapsed_us=" << s.telemetry().elapsedMicros(s.nowMicros())
		          << " longest_stall_us=" << s.telemetry().longestStallMicros()
		          << " longest_downtime_us=" << s.telemetry().longestDowntimeMicros()
		          << " reload_load_us=" << s.telemetry().reloadLoadTotalMicros()
		          << " reload_fast_forward_ticks=" << s.telemetry().reloadFastForwardTicks()
		          << " reload_fast_forward_us=" << s.telemetry().reloadFastForwardMicros()
		          << " outstanding_max=" << s.telemetry().outstandingMax();
		Turn::SessionTelemetry::writeFields(std::cout, s.telemetry().totals());
		std::cout << '\n';
		for (unsigned seat = 0; seat < Turn::MAX_SEATS; ++seat)
		{
			if (!(s.telemetry().seenSeats() & (1u << seat)))
				continue;
			std::cout << "GLOB2_NET_SEAT session=" << session << " seat=" << seat
			          << " local=" << (static_cast<int>(seat) == turnMatch->localSeat ? 1 : 0);
			static const char* const names[] = {"not_connected", "connected", "lagging",
			                                    "reconnecting", "resyncing", "left"};
			for (unsigned k = 0; k < Turn::PRESENCE_STATES; ++k)
			{
				const auto state = static_cast<Turn::PresenceState>(k);
				std::cout << ' ' << names[k] << ".transitions=" << s.telemetry().transitions(seat, state) << ' '
				          << names[k] << ".time_us=" << s.telemetry().timeIn(seat, state, s.nowMicros());
			}
			std::cout << '\n';
		}
		std::cout << "GLOB2_NET_SUMMARY " << turnNetworkSummary(false).dump() << std::endl;
	}
	// The per-match ClientNetworkSummary, next to the replay the game writes. Written
	// locally only; nothing uploads it.
	if (!globalContainer->replayWriter)
		return;
	// Absolute paths are written as given, others under the user directory, as the
	// replay itself is (ReplayWriter::init).
	const std::string path = networkSidecarPath(turnMatch->replayPath);
	auto* files = GAGCore::Toolkit::getFileManager();
	FILE* fp = !path.empty() && path[0] == '/' ? std::fopen(path.c_str(), "wb")
	           : files                         ? files->openFP(path, "wb")
	                                           : nullptr;
	const std::string text = turnNetworkSummary(true).dump() + "\n";
	const bool written = fp && std::fwrite(text.data(), 1, text.size(), fp) == text.size();
	if (fp && std::fclose(fp) != 0)
		std::cerr << "Turn session: cannot close the network summary " << path << std::endl;
	else if (!written)
		std::cerr << "Turn session: cannot write the network summary " << path << std::endl;
}
