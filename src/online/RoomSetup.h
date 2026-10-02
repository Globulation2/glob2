// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Converts between the custom-game lobby's draft (CustomGameSetup, which the Room
// screen reuses to edit a room's map and rules) and the JSON a platform room holds:
// RoomMapSelection {kind: "generated", generator}, MatchRules and SetupTeam
// alliances (platform/packages/protocol, matchSetup.ts and resources.ts).
//
// Only generated maps go through here: a generator descriptor is what the platform's
// engine agents produce the same bytes from for every client. Premade maps from the
// local library would have to be uploaded first (POST /api/v1/uploads).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

struct CustomGameSetup;

namespace Online
{
using Json = nlohmann::json;

/// GeneratorDescriptor for the draft's generator at its colony count. Every control
/// (shared width/height/teams/workers and the generator's own) is in `params`. A zero
/// draft seed is replaced by `fallbackSeed`. Throws std::invalid_argument for an
/// unknown or editor-only generator.
Json generatorDescriptor(const CustomGameSetup& setup, std::uint32_t fallbackSeed);
/// MatchRules from the draft's rules.
Json matchRules(const CustomGameSetup& setup);
/// SetupTeam list (team = seat index, alliance) for the draft's colonies.
Json setupTeams(const CustomGameSetup& setup);

/// Reads a room back into a draft: the generator and its parameters (when the room's
/// map is generated), the colony count, the alliances and the rules. Leaves fields the
/// room does not carry (game speed, controllers) as they were. Returns false when the
/// room's map is not a generator this build knows.
bool applyRoomToSetup(const Json& room, CustomGameSetup& setup);
/// Rules only (MatchRules JSON), for read-only summaries.
void applyRulesToSetup(const Json& rules, CustomGameSetup& setup);

/// Name of the rule preset the draft matches ("Standard", "Quick clash", …), or
/// "Custom rules".
std::string rulesetName(const CustomGameSetup& setup);
/// "2 vs 2", "FFA" or "Custom teams" from the alliances of `teams` (SetupTeam list).
std::string formatName(const Json& teams);
} // namespace Online
