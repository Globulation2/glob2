// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RelayTransport.h"

#include <SDL.h>
#include <iostream>
#include <stdexcept>

namespace Online
{
void RelayFrameReader::append(const std::uint8_t* data, std::size_t size)
{
	if (offset > 0 && offset == buffer.size())
		clear();
	buffer.insert(buffer.end(), data, data + size);
}

bool RelayFrameReader::next(std::vector<std::uint8_t>& payload)
{
	if (buffer.size() - offset < 2)
		return false;
	const std::size_t length = (std::size_t(buffer[offset]) << 8) | buffer[offset + 1];
	if (buffer.size() - offset - 2 < length)
		return false;
	payload.assign(buffer.begin() + offset + 2, buffer.begin() + offset + 2 + length);
	offset += 2 + length;
	// Compact once the consumed prefix dominates, so the buffer does not grow forever.
	if (offset > 65536 && offset * 2 > buffer.size())
	{
		buffer.erase(buffer.begin(), buffer.begin() + offset);
		offset = 0;
	}
	return true;
}

std::vector<std::uint8_t> relayFrame(const std::vector<std::uint8_t>& payload)
{
	if (payload.size() > 0xFFFF)
		return {};
	std::vector<std::uint8_t> frame;
	frame.reserve(payload.size() + 2);
	frame.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
	frame.push_back(static_cast<std::uint8_t>(payload.size() & 0xFF));
	frame.insert(frame.end(), payload.begin(), payload.end());
	return frame;
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
	std::uint32_t since = 0;
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
	const std::uint32_t now = SDL_GetTicks();
	for (auto it = links.begin(); it != links.end();)
	{
		if (it->link->pendingOutgoing() == 0 || it->link->state() != NetTransport::State::Connected ||
			now - it->since > LINGER_MS)
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
	// The last frames (Quit, a PlayerQuitsGameOrder) are usually still queued when the
	// game tears the session down; closing now would drop them and the relay would
	// hold the seat for its reconnect grace instead of sequencing the quit.
	if (link && link->state() == NetTransport::State::Connected && link->pendingOutgoing() > 0)
	{
		lingering().push_back({std::move(link), SDL_GetTicks()});
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
	return !frame.empty() && link->send(std::move(frame));
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
