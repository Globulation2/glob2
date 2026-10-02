// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once

// The in-game connection HUD of turn-protocol games (multiplayer mock-ups 4 and 5).
//
// - A permanent compact panel in the top-left of the map, where the "waiting for X"
//   box was: every player's colour, name, connection state and latency, own row
//   highlighted with this client's input delay. Classic navy on pointer hosts, the
//   touch HUD's aubergine under the status strip on phones, shrinking to a grid of
//   dots and milliseconds beyond four people. Clicking (tapping) it opens details.
// - Centre cards when this client waits on the network: connection lost
//   (reconnecting, with the grace time and Leave match), catching up (progress of
//   the fast-forward) and out of sync (rejoining after the relay's verdict).
// - One-line notices in the message list when another player's state changes.
//
// Presentation only: it reads a ConnectionSnapshot (Engine builds it from the
// TurnSession) and is never simulated, networked or saved.

#include <GraphicContext.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct ConnectionRow
{
	enum class State
	{
		Connected,
		Slow,
		Reconnecting,
		Resyncing,
		Waiting, ///< not connected yet (start of the match)
		Left,
		AI,
	};
	int seat = 0;
	std::string name;
	GAGCore::Color color;
	State state = State::Connected;
	int latencyMs = -1;   ///< humans: delay to the relay; own row: input delay
	int graceSeconds = -1; ///< reconnecting: time left before the seat is removed
	bool local = false;
	bool unstable = false; ///< own row: delay well above its usual level
};

struct ConnectionSnapshot
{
	std::vector<ConnectionRow> rows;
	enum class Card
	{
		None,
		Reconnecting,
		CatchingUp,
		Desync,
	} card = Card::None;
	int attempt = 0;
	int graceSeconds = -1;
	std::uint32_t catchupDone = 0, catchupTotal = 0;
	int missedSeconds = 0;
	int secondsLeft = -1;
	int inputDelayMs = -1, rttMs = -1;
	std::string relay;
	bool ownUnstable = false;
};

class ConnectionOverlay
{
  public:
	std::function<ConnectionSnapshot()> source;
	/// Leave match (reconnecting card): the game ends for this player.
	std::function<void()> leave;
	/// One-line notices for the message list.
	std::function<void(const std::string &)> notice;

	/// Paints the panel (and any card) for this frame. `touch` selects the phone HUD
	/// look; `area` is the map's rectangle in drawable pixels and `unit` the drawable
	/// pixels per point.
	void draw(bool touch, SDL_Rect area, double unit);
	/// Clicks and taps on the panel, the details and the cards; true when consumed.
	bool handle(const SDL_Event &event);
	bool detailsOpen() const { return details; }
	void openDetails(bool open) { details = open; }
	/// The last snapshot drawn (harness).
	const ConnectionSnapshot &last() const { return snapshot; }

  private:
	void observe(const ConnectionSnapshot &next);
	void drawPanel(bool touch, SDL_Rect area, double unit);
	void drawDetails(bool touch, SDL_Rect area, double unit);
	void drawCard(bool touch, SDL_Rect area, double unit);
	bool inside(const SDL_Rect &r, int x, int y) const;

	ConnectionSnapshot snapshot;
	std::map<int, ConnectionRow::State> seen;
	bool primed = false;
	bool details = false;
	SDL_Rect panelRect{}, closeRect{}, leaveRect{};
};
