// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "YOGClient.h"

/// Blocking bring-up helpers shared by the synchronous LAN menu screens
/// (LANMenuScreen host path, LANFindScreen connect path). They pump
/// YOGClient::update() on the menu thread until a target condition is reached.
///
/// Each replaces a raw `while (state != target) client->update();` busy-spin
/// that had no timeout, no sleep, and no failure exit, and therefore hung the
/// menu thread forever (pegging a CPU core) whenever the YOG handshake failed
/// (BH-110, BH-120). The helpers always terminate: they bail on a dropped
/// connection, on a login refusal, or after a fixed wall-clock timeout, and
/// they sleep between polls so the wait does not peg a core.
namespace LANBringup
{
	/// Outcome of a blocking bring-up wait. `Reached` means the awaited
	/// condition was satisfied; `Failed` means the wait gave up (dropped
	/// connection, login refused, or timeout) instead of hanging. The caller
	/// is expected to surface a "can't connect" message on `Failed`.
	enum class Result { Reached, Failed };

	/// Pumps client.update() until the connection reaches `target`.
	///
	/// Returns `Failed` (rather than spinning forever) when:
	///  - the connection drops (NetConnection is neither connecting nor
	///    connected), or
	///  - the login is refused: while awaiting ClientOnStandby, the server
	///    bounces the client back to WaitingForLoginInformation instead of
	///    advancing (MNetRefuseLogin in YOGClient::update), or
	///  - the fixed handshake timeout elapses.
	Result waitForConnectionState(YOGClient& client, YOGClient::ConnectionState target);

	/// Pumps client.update() until the client's game list is non-empty.
	/// Returns `Failed` on a dropped connection or after the handshake timeout
	/// (e.g. a host that publishes no games), instead of hanging.
	Result waitForGameList(YOGClient& client);
}
