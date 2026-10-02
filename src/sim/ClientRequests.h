// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <utility>
#include <variant>

#include <SDL_stdinc.h>

#include "sim/ClientEvents.h"
#include "sim/EntityRef.h"

/// Commands the client sends to the simulation that must not be lost or
/// merged with later ones.
namespace ClientCommand
{
	//! The player acknowledged an SGSL "wait for Space" prompt.
	struct ScriptSpacePressed {};
}

using ClientCommandVariant = std::variant<ClientCommand::ScriptSpacePressed>;

/// Client → simulation channel. Two kinds of traffic:
///
///  - a latest-value mailbox (ClientView) for what the client is looking at.
///    The client overwrites it whenever it changes; the simulation reads the
///    newest value at a tick boundary and never sees intermediate values.
///  - a lossless FIFO of ClientCommands.
///
/// Nothing here is simulation state: it is never saved, checksummed or sent
/// over the network, and it must only steer presentation-only work (such as
/// which building records Building::unitsFailingByReason).
class ClientRequests
{
public:
	struct ClientView
	{
		//! Top-left tile and size, in tiles, of the visible map area.
		int viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0;
		//! Building whose panel is open (its failing units are recorded).
		BuildingRef observedBuilding;
		//! OverlayArea::OverlayType the client displays (None = 0).
		Uint8 overlay = 0;
		//! Bit mask of debug layers the client displays.
		Uint32 debugLayers = 0;
	};

	void publishViewport(int x, int y, int w, int h)
	{
		view.viewportX = x;
		view.viewportY = y;
		view.viewportW = w;
		view.viewportH = h;
	}
	void publishObservedBuilding(BuildingRef building) { view.observedBuilding = building; }
	void publishOverlay(Uint8 overlay) { view.overlay = overlay; }
	void publishDebugLayers(Uint32 layers) { view.debugLayers = layers; }
	//! Newest published view (a copy, so it can later be read under a lock).
	ClientView latest() const { return view; }

	void push(ClientCommandVariant command) { commands.push(std::move(command)); }
	template <typename F>
	void drain(F &&consume) { commands.drain(std::forward<F>(consume)); }
	bool empty() const { return commands.empty(); }

	// The SGSL Space acknowledgement keeps its historical one-bit meaning:
	// several presses before the script looks count as one.
	void requestScriptSpace() { push(ClientCommand::ScriptSpacePressed{}); }
	bool scriptSpacePending() const { return !commands.empty(); }
	//! Consume every pending acknowledgement; true if there was at least one.
	bool takeScriptSpace()
	{
		bool any = false;
		drain([&](ClientCommandVariant &&c) { any |= std::holds_alternative<ClientCommand::ScriptSpacePressed>(c); });
		return any;
	}
	void discardScriptSpace() { takeScriptSpace(); }

	void reset()
	{
		view = ClientView();
		commands.clear();
	}

private:
	ClientView view;
	LosslessQueue<ClientCommandVariant> commands;
};
