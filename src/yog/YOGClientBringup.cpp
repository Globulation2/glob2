// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "YOGClientBringup.h"
#include "YOGClientGameListManager.h"
#include <SDL.h>
#include <functional>

namespace
{
	/// Wall-clock ceiling for a blocking LAN bring-up wait. The in-process
	/// loopback server normally replies within a few milliseconds; this only
	/// fires when the handshake is wedged or the connection has silently died.
	constexpr Uint32 HANDSHAKE_TIMEOUT_MS = 10000;

	/// Idle between client.update() polls so a blocking bring-up wait does not
	/// peg a CPU core while it holds the menu thread.
	constexpr Uint32 HANDSHAKE_POLL_DELAY_MS = 50;

	/// Shared poll skeleton for the bring-up waits. Calls client.update()
	/// repeatedly until `done()` holds (-> Reached), an early failure predicate
	/// fires, the connection drops, or HANDSHAKE_TIMEOUT_MS elapses (-> Failed).
	/// `earlyFail` may be null when there is no condition-specific failure to
	/// detect beyond a dropped connection.
	LANBringup::Result pumpClient(YOGClient& client,
	                              const std::function<bool()>& done,
	                              const std::function<bool()>& earlyFail)
	{
		const Uint32 start = SDL_GetTicks();
		while (!done())
		{
			client.update();
			if (done())
				break;
			if (earlyFail && earlyFail())
				return LANBringup::Result::Failed;
			// A dropped connection leaves the NetConnection neither connecting
			// nor connected; without this check the wait could never terminate.
			if (!client.isConnecting() && !client.isConnected())
				return LANBringup::Result::Failed;
			if (SDL_GetTicks() - start >= HANDSHAKE_TIMEOUT_MS)
				return LANBringup::Result::Failed;
			SDL_Delay(HANDSHAKE_POLL_DELAY_MS);
		}
		return LANBringup::Result::Reached;
	}
}

namespace LANBringup
{
	Result waitForConnectionState(YOGClient& client, YOGClient::ConnectionState target)
	{
		return pumpClient(client,
			[&]() { return client.getConnectionState() == target; },
			[&]() {
				// Login refused: the server returned us to
				// WaitingForLoginInformation instead of advancing to
				// ClientOnStandby. Only meaningful while awaiting standby.
				return target == YOGClient::ClientOnStandby &&
				       client.getConnectionState() == YOGClient::WaitingForLoginInformation;
			});
	}

	Result waitForGameList(YOGClient& client)
	{
		return pumpClient(client,
			[&]() { return client.getGameListManager()->getGameList().size() != 0; },
			nullptr);
	}
}
