// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "NeuroticaPolicySocket.h"

#include "WinProbability.h"
#include "NeuroticaActions.h"
#include "Order.h"
#include <stdexcept>
#include <algorithm>

#include "Game.h"
#include "Map.h"
#include "NeuroticaObservation.h"
#include "Team.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Neurotica
{
	PolicySocketSource::PolicySocketSource(Team *team, const std::string &socketPath)
		: team_(team), path_(socketPath)
	{
	}

	PolicySocketSource::~PolicySocketSource()
	{
		disconnect();
	}

	void PolicySocketSource::disconnect()
	{
		if (fd_ >= 0)
			::close(fd_);
		fd_ = -1;
	}

	bool PolicySocketSource::writeAll(const void *data, size_t bytes)
	{
		const char *p = static_cast<const char *>(data);
		while (bytes)
		{
#ifdef MSG_NOSIGNAL
			const ssize_t n = ::send(fd_, p, bytes, MSG_NOSIGNAL);
#else
			const ssize_t n = ::write(fd_, p, bytes);
#endif
			if (n <= 0)
			{
				if (n < 0 && errno == EINTR)
					continue;
				return false;
			}
			p += n;
			bytes -= size_t(n);
		}
		return true;
	}

	bool PolicySocketSource::readAll(void *data, size_t bytes)
	{
		char *p = static_cast<char *>(data);
		while (bytes)
		{
			const ssize_t n = ::read(fd_, p, bytes);
			if (n <= 0)
			{
				if (n < 0 && errno == EINTR)
					continue;
				return false;
			}
			p += n;
			bytes -= size_t(n);
		}
		return true;
	}

	bool PolicySocketSource::ensureConnected()
	{
		if (fd_ >= 0)
			return true;
		sockaddr_un addr{};
		addr.sun_family = AF_UNIX;
		if (path_.size() >= sizeof(addr.sun_path))
			throw std::runtime_error("NEUROTICA_POLICY_FAILURE socket path too long");
		std::strcpy(addr.sun_path, path_.c_str());
		fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd_ < 0 || ::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)))
			throw std::runtime_error("NEUROTICA_POLICY_FAILURE cannot connect");
		timeval timeout{60, 0};
		setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
		int yes = 1;
		setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
		if (!writeAll("NPS6", 4))
			throw std::runtime_error("NEUROTICA_POLICY_FAILURE handshake");
		return true;
	}
	std::shared_ptr<Order> PolicySocketSource::actionOrder(Uint32 tick)
	{
		if (tick < nextTick_)
			return std::make_shared<NullOrder>();
		ensureConnected();
		auto context = actionContext(team_);
		auto length = [](Uint32 n)
		{ return std::array<Uint8, 4>{{Uint8(n), Uint8(n >> 8), Uint8(n >> 16), Uint8(n >> 24)}}; };
		auto size = length(context.size());
		if (!writeAll(size.data(), 4) || !writeAll(context.data(), context.size()) ||
			!readAll(size.data(), 4))
			throw std::runtime_error("NEUROTICA_POLICY_FAILURE request/response");
		Uint32 n = Uint32(size[0]) | (Uint32(size[1]) << 8) | (Uint32(size[2]) << 16) |
				   (Uint32(size[3]) << 24);
		if (n < FIELD_COUNT * 4 ||
			n > FIELD_COUNT * 4 + size_t(team_->game->map.getW()) * team_->game->map.getH())
			throw std::runtime_error("NEUROTICA_POLICY_FAILURE response length");
		reply_.resize(n);
		if (!readAll(reply_.data(), n))
			throw std::runtime_error("NEUROTICA_POLICY_FAILURE truncated response");
		auto action = unpackAction(reply_);
		auto order = decodeAction(team_, action);
		nextTick_ = tick + action.v[DELAY];
		std::cerr << "NEUROTICA_ACTION tick=" << tick << " team=" << int(team_->teamNumber)
				  << " op=" << action.v[OP] << " delay=" << action.v[DELAY] << std::endl;
		return order;
	}
	bool PolicySocketSource::field(Uint32, DesiredState &)
	{
		throw std::runtime_error("Neurotica NPS6 uses semantic orders, not fields");
	}
} // namespace Neurotica
