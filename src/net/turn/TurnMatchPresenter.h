// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// What the connection HUD of a turn game shows (docs/multiplayer/connection-quality.md):
// the per-player rows and centre cards (ConnectionSnapshot, drawn by ConnectionOverlay)
// and the text notice lines, built from a read-only TurnSession and the game's players.
// Presentation only: the Engine pumps the session and installs this as the overlay's
// source. It keeps the little state the cards need across frames (when the link was
// lost, where a catch-up started and how fast it closes).

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ConnectionOverlay.h"

class Game;
namespace Turn
{
	class TurnSession;
}

class TurnMatchPresenter
{
public:
	/// The HUD at `nowMicros` (the engine's turn clock).
	ConnectionSnapshot snapshot(const Turn::TurnSession& session, const Game& game, Uint64 nowMicros);
	/// The text form of the connection state (GameGUI::connectionNotice; LAN harness logs).
	static std::vector<std::string> notice(const Turn::TurnSession& session, const Game& game);

private:
	std::uint32_t catchupFrom = 0;
	bool catchupActive = false;
	Uint64 connectionLostMicros = 0;
	Uint64 catchupStartedMicros = 0;
	/// Catch-up progress sampled every few seconds: whether the gap to the relay
	/// shrinks (CatchUpPace in ConnectionOverlay.h).
	CatchUpPace catchupPace;
};
