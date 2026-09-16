// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "NeuroticaPolicySocket.h"

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
			const ssize_t n = ::write(fd_, p, bytes);
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
		if (gaveUp_ || !team_ || !team_->game)
			return false;

		sockaddr_un addr{};
		addr.sun_family = AF_UNIX;
		if (path_.size() + 1 > sizeof(addr.sun_path))
		{
			std::cerr << "Neurotica: policy socket path too long: " << path_ << std::endl;
			gaveUp_ = true;
			return false;
		}
		std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

		fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd_ < 0)
		{
			gaveUp_ = true;
			return false;
		}
		if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
		{
			std::cerr << "Neurotica: cannot reach policy server at " << path_ << ": "
			          << std::strerror(errno) << std::endl;
			disconnect();
			gaveUp_ = true;
			return false;
		}

		const Map *map = &team_->game->map;
		std::vector<Uint8> statics;
		if (!encodeStaticPlanes(map, statics))
		{
			disconnect();
			gaveUp_ = true;
			return false;
		}

		Uint8 header[16];
		std::memcpy(header, "NPS2", 4);
		const Uint16 w = Uint16(map->getW()), h = Uint16(map->getH());
		std::memcpy(header + 4, &w, 2);
		std::memcpy(header + 6, &h, 2);
		header[8] = Uint8(SP_COUNT);
		header[9] = Uint8(DP_COUNT);
		header[10] = header[11] = 0;
		Uint32 gameId = 0;
		if (const char *env = getenv("GLOB2_NEUROTICA_GAME_ID"))
			gameId = Uint32(strtoul(env, nullptr, 10));
		std::memcpy(header + 12, &gameId, 4);
		if (!writeAll(header, sizeof(header)) || !writeAll(statics.data(), statics.size()))
		{
			std::cerr << "Neurotica: policy handshake failed" << std::endl;
			disconnect();
			gaveUp_ = true;
			return false;
		}
		return true;
	}

	bool PolicySocketSource::field(Uint32 tick, DesiredState &out)
	{
		if (!ensureConnected() || !team_ || !team_->game)
			return false;
		const Map *map = &team_->game->map;
		const Sint32 w = map->getW(), h = map->getH();
		const size_t cells = size_t(w) * size_t(h);

		if (!encodeDynamicPlanes(team_, planes_))
			return false;

		Uint8 header[8];
		std::memcpy(header, &tick, 4);
		header[4] = Uint8(team_->teamNumber);
		header[5] = header[6] = header[7] = 0;
		if (!writeAll(header, sizeof(header)) || !writeAll(planes_.data(), planes_.size()))
		{
			std::cerr << "Neurotica: policy request failed; going inert" << std::endl;
			disconnect();
			gaveUp_ = true;
			return false;
		}

		reply_.resize(cells * 3);
		if (!readAll(reply_.data(), reply_.size()))
		{
			std::cerr << "Neurotica: policy reply truncated; going inert" << std::endl;
			disconnect();
			gaveUp_ = true;
			return false;
		}

		out.reset(w, h);
		for (size_t i = 0; i < cells; i++)
		{
			const Uint8 cls = reply_[i * 3 + 0];
			// A class the engine does not have is treated as "nothing wanted"
			// rather than clamped: a policy emitting garbage should be inert at
			// that cell, not build something arbitrary.
			out.building[i] = (cls <= IntBuildingTypeCount) ? cls : Uint8(0);
			out.buildingScore[i] = reply_[i * 3 + 1];
			out.areas[i] = reply_[i * 3 + 2] & (AREA_GUARD | AREA_CLEAR | AREA_FORBIDDEN);
			// Urgency follows the score until the policy learns a head for it,
			// so the reconciler still prioritises the cells the net is most
			// confident about rather than acting in scan order.
			out.urgency[i] = reply_[i * 3 + 1];
		}
		return true;
	}
} // namespace Neurotica
