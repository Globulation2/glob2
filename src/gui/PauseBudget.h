// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once

// Pausing in network matches (docs/multiplayer/client.md, "Pausing"). Rooms and LAN
// games may pause freely. Quick matches, rated or not, give every player a budget: a
// few pauses and a total time; anyone may resume. Every client sees the same pause
// orders with their sender, so each one keeps everyone's budget and any client sends
// the resume once the player holding the pause runs out of time. The orders stay as
// they were (Pause orders are valid for every player, OrderValidation.cpp); only
// when the user interface offers and sends them changes.

#include <algorithm>
#include <array>
#include <cstdint>

class PauseBudget
{
  public:
	static constexpr int PLAYERS = 32;
	struct Rules
	{
		bool limited = false;
		int maxPauses = 3;
		std::uint32_t totalMs = 60000;
	};

	void setRules(Rules value) { rules = value; }
	const Rules &currentRules() const { return rules; }
	bool limited() const { return rules.limited; }

	/// A pause order from `player` executed (`pause` false: a resume) at `nowMs`.
	void executed(int player, bool pause, std::uint32_t nowMs)
	{
		if (pause)
		{
			if (holder >= 0 || !valid(player))
				return; // already paused: the first pauser keeps the pause
			holder = player;
			since = nowMs;
			++count[player];
		}
		else if (holder >= 0)
		{
			used[holder] += nowMs - std::min(nowMs, since);
			holder = -1;
		}
	}
	/// Whether `player` may start a pause now.
	bool canPause(int player) const { return !rules.limited || (valid(player) && pausesLeft(player) > 0 && usedMs(player, 0) < rules.totalMs); }
	int pausesLeft(int player) const { return valid(player) ? std::max(0, rules.maxPauses - count[player]) : 0; }
	/// Pause time `player` has left (the running pause counted until `nowMs`).
	std::uint32_t timeLeftMs(int player, std::uint32_t nowMs) const
	{
		const std::uint32_t spent = usedMs(player, nowMs);
		return spent >= rules.totalMs ? 0 : rules.totalMs - spent;
	}
	/// Who holds the current pause; -1 when not paused (or paused before tracking).
	int pauser() const { return holder; }
	/// True when the current pause is over its holder's budget: more pauses than
	/// allowed, or out of time. Any client then resumes the match.
	bool expired(std::uint32_t nowMs) const
	{
		if (!rules.limited || holder < 0)
			return false;
		return count[holder] > rules.maxPauses || usedMs(holder, nowMs) >= rules.totalMs;
	}

  private:
	static bool valid(int player) { return player >= 0 && player < PLAYERS; }
	std::uint32_t usedMs(int player, std::uint32_t nowMs) const
	{
		if (!valid(player))
			return 0;
		std::uint32_t spent = used[player];
		if (player == holder && nowMs > since)
			spent += nowMs - since;
		return spent;
	}

	Rules rules;
	std::array<int, PLAYERS> count{};
	std::array<std::uint32_t, PLAYERS> used{};
	int holder = -1;
	std::uint32_t since = 0;
};
