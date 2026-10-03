// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RelayTransport.h"

#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace Online
{
std::vector<std::uint8_t> relayFrame(const std::vector<std::uint8_t>& payload)
{
	return NetFrame::encode(payload);
}

RelayTransport::RelayTransport(std::string relayUrl, Factory factory)
	: url(std::move(relayUrl)), factory(std::move(factory))
{
	if (!this->factory)
		this->factory = [] { return makeNetTransport(); };
}

namespace
{
struct Lingering
{
	std::unique_ptr<NetTransport> link;
	Uint64 since = 0;
	bool awaitClose = false; ///< a Quit was sent: wait for the relay to close
};
std::vector<Lingering> &lingering()
{
	static std::vector<Lingering> links;
	return links;
}
} // namespace

void pumpLingeringRelayConnections()
{
	auto &links = lingering();
	const Uint64 now = SDL_GetTicks();
	for (auto it = links.begin(); it != links.end();)
	{
		// Read and drop whatever still arrives: closing a socket with unread input
		// resets it, and a reset can discard the Quit before the relay reads it.
		std::vector<std::uint8_t> ignored;
		while (it->link->receive(ignored))
			ignored.clear();
		const bool written = it->link->pendingOutgoing() == 0;
		const bool closedByRelay = it->link->state() != NetTransport::State::Connected;
		if (closedByRelay || (written && !it->awaitClose) || now - it->since > LINGER_MS)
		{
			it->link->close();
			it = links.erase(it);
		}
		else
			++it;
	}
}

std::size_t lingeringRelayConnections()
{
	pumpLingeringRelayConnections();
	return lingering().size();
}

RelayTransport::~RelayTransport()
{
	// The last frames (Quit) are usually still queued when the game tears the session
	// down. Closing now would drop them, or reset the connection before the relay has
	// read them, and the relay would hold the seat for its reconnect grace. After a
	// Quit the relay closes the connection itself; wait for that.
	if (link && link->state() == NetTransport::State::Connected && (sentQuit || link->pendingOutgoing() > 0))
	{
		lingering().push_back({std::move(link), SDL_GetTicks(), sentQuit});
		return;
	}
	close();
}

Turn::TurnTransport::State RelayTransport::state()
{
	if (!link)
		return State::Disconnected;
	switch (link->state())
	{
	case NetTransport::State::Connecting:
		return State::Connecting;
	case NetTransport::State::Connected:
		return State::Connected;
	case NetTransport::State::Closed:
	default:
		if (lastError.empty())
			lastError = link->error();
		return State::Disconnected;
	}
}

void RelayTransport::connect()
{
	close();
	reader.clear();
	lastError.clear();
	sentQuit = false;
	link = factory();
	try
	{
		link->open(url);
	}
	catch (const std::exception& error)
	{
		lastError = error.what();
		std::cerr << "RelayTransport: cannot open " << url << ": " << lastError << std::endl;
		link.reset();
	}
}

void RelayTransport::close()
{
	if (link)
	{
		link->close();
		link.reset();
	}
}

bool RelayTransport::send(const std::vector<std::uint8_t>& payload)
{
	if (!link || link->state() != NetTransport::State::Connected)
		return false;
	auto frame = relayFrame(payload);
	if (frame.empty() || !link->send(std::move(frame)))
		return false;
	if (!payload.empty() && payload[0] == Turn::MSG_QUIT)
		sentQuit = true;
	return true;
}

void RelayTransport::flush()
{
	// NetTransport has no explicit flush: WssTransport writes as its event loop runs,
	// and state() runs it (received data stays queued for the next receive()).
	if (link)
		link->state();
}

void RelayTransport::pump()
{
	if (!link)
		return;
	std::vector<std::uint8_t> bytes;
	while (link->receive(bytes))
	{
		reader.append(bytes.data(), bytes.size());
		bytes.clear();
	}
}

bool RelayTransport::receive(std::vector<std::uint8_t>& payload)
{
	pump();
	return reader.next(payload);
}

std::string RelayTransport::error() const
{
	return lastError;
}
}
