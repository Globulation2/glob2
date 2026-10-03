// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MatchReport.h"

#include "RelayLog.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <map>
#include <stdexcept>

namespace Relay
{
using json = nlohmann::json;

namespace
{
	json loadJson(const RelayLoad& load)
	{
		return json{{"matches", load.matches}, {"connections", load.connections}};
	}
}

std::string registrationJson(const RegistrationInfo& info, const RelayLoad& load, bool draining)
{
	json j;
	j["relayId"] = info.relayId;
	j["publicUrl"] = info.publicUrl;
	j["region"] = info.region;
	j["build"] = info.build.substr(0, 128);
	j["turnProtocol"] = Turn::PROTOCOL_VERSION;
	j["capacity"] = json{{"maxMatches", info.maxMatches}};
	j["load"] = loadJson(load);
	j["draining"] = draining;
	return j.dump();
}

std::string heartbeatJson(const std::string& relayId, const RelayLoad& load, bool draining,
                          const std::vector<std::string>& activeMatchIds)
{
	json j;
	j["relayId"] = relayId;
	j["load"] = loadJson(load);
	j["draining"] = draining;
	j["activeMatchIds"] = activeMatchIds;
	return j.dump();
}

const char* endReasonName(EndReason reason)
{
	switch (reason)
	{
	case EndReason::Completed: return "completed";
	case EndReason::Abandoned: return "abandoned";
	case EndReason::Aborted: return "aborted";
	}
	return "aborted";
}

std::string sha256Hex(const std::vector<std::uint8_t>& bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int length = 0;
	if (EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(), nullptr) != 1 || length != 32)
		throw std::runtime_error("SHA-256 failed");
	static const char hex[] = "0123456789abcdef";
	std::string out;
	for (unsigned i = 0; i < length; ++i)
	{
		out += hex[digest[i] >> 4];
		out += hex[digest[i] & 15];
	}
	return out;
}

std::string matchEndedJson(const MatchEndInfo& info, const Turn::MatchRecord& record,
                           const std::vector<std::uint8_t>& recordBytes)
{
	struct SeatSummary
	{
		unsigned disconnects = 0;
		bool quit = false;
		std::uint32_t quitTick = 0;
		bool dropped = false;
	};
	std::map<unsigned, SeatSummary> seats;
	for (unsigned s = 0; s < static_cast<unsigned>(MAX_TICKET_SEATS); ++s)
		if (record.humanSeatMask & (1u << s))
			seats[s] = SeatSummary{};
	for (const auto& e : record.events)
	{
		auto it = seats.find(e.seat);
		if (it == seats.end())
			continue;
		switch (e.kind)
		{
		case Turn::MatchEventKind::Disconnected: ++it->second.disconnects; break;
		case Turn::MatchEventKind::LeftByQuit:
		case Turn::MatchEventKind::LeftByGrace:
			if (!it->second.quit)
			{
				it->second.quit = true;
				it->second.quitTick = e.tick;
			}
			break;
		case Turn::MatchEventKind::ToldToRejoin: it->second.dropped = true; break;
		default: break;
		}
	}
	json j;
	j["matchId"] = info.matchId;
	j["relayId"] = info.relayId;
	j["simVersion"] = json{{"versionMinor", info.simVersion.versionMinor},
	                       {"netProtocol", info.simVersion.netProtocol},
	                       {"dataHash", info.simVersion.dataHash}};
	j["startedAt"] = utcTimestamp(info.startedAt);
	j["endedAt"] = utcTimestamp(info.endedAt);
	j["finalTick"] = record.endTick;
	j["reason"] = endReasonName(info.reason);
	json seatArray = json::array();
	json minority = json::array();
	for (const auto& s : seats)
	{
		json seat{{"seat", s.first}, {"disconnects", s.second.disconnects}, {"droppedForDesync", s.second.dropped}};
		if (s.second.quit)
			seat["quitTick"] = s.second.quitTick;
		seatArray.push_back(seat);
		if (s.second.dropped)
			minority.push_back(s.first);
	}
	j["seats"] = seatArray;
	j["desync"] = json{{"flagged", (record.flags & Turn::MatchRecord::FLAG_DESYNC_FLAGGED) != 0},
	                   {"minoritySeats", minority}};
	j["record"] = json{{"sha256", sha256Hex(recordBytes)},
	                   {"size", recordBytes.size()},
	                   {"formatVersion", Turn::MatchRecord::FORMAT_VERSION}};
	if (info.network.is_object())
		j["network"] = info.network;
	return j.dump();
}

bool setupMapHash(const std::string& setupJson, std::array<std::uint8_t, 32>& out)
{
	const json setup = json::parse(setupJson, nullptr, false);
	if (setup.is_discarded() || !setup.is_object() || !setup.contains("map") || !setup["map"].is_object())
		return false;
	const json& map = setup["map"];
	if (!map.contains("hash") || !map["hash"].is_string())
		return false;
	const std::string hex = map["hash"].get<std::string>();
	if (hex.size() != 64)
		return false;
	auto value = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	};
	for (std::size_t i = 0; i < 32; ++i)
	{
		const int hi = value(hex[2 * i]), lo = value(hex[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
	}
	return true;
}
}
