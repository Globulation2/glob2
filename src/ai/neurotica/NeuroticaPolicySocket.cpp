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
	namespace
	{
		//! Bytes per cell in a policy reply (NPS4): building class, score,
		//! area bits, staffing, then the swarm production mix as three
		//! worker/explorer/warrior weights.
		constexpr size_t kReplyStride = 7;
	}
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
		std::memcpy(header, "NPS4", 4);
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

		// 4 bytes per cell since NPS3: class, score, area bits, staffing.
		reply_.resize(cells * kReplyStride);
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
			const Uint8 cls = reply_[i * kReplyStride + 0];
			// A class the engine does not have is treated as "nothing wanted"
			// rather than clamped: a policy emitting garbage should be inert at
			// that cell, not build something arbitrary.
			// DONT_CARE passes through: it means "not mine to decide", which
			// is what a policy that can see a building's footprint but not its
			// anchor must say about the non-anchor cells. Folding it to 0 would
			// turn that into "want empty here".
			out.building[i] = (cls <= IntBuildingTypeCount || cls == DONT_CARE)
			                  ? cls : Uint8(0);
			out.buildingScore[i] = reply_[i * kReplyStride + 1];
			out.areas[i] = reply_[i * kReplyStride + 2] & (AREA_GUARD | AREA_CLEAR | AREA_FORBIDDEN);
			// Staffing: how many units the policy wants working this building.
			// DONT_CARE leaves it alone, which is what every cell said before
			// NPS3 -- the plane existed in the schema from the start but no
			// policy could reach it, so the network could not allocate labour
			// at all.
			out.workers[i] = reply_[i * kReplyStride + 3];
			// Swarm unit mix. A swarm is created with ratio[0]=1 and zero
			// elsewhere (Building Lifecycle.cpp), so without this plane a
			// Neurotica team produces workers and never a single warrior --
			// it cannot win by force, only outlast. DONT_CARE in the worker
			// slot leaves the whole ratio alone, per the schema.
			const size_t ratioBase = i * SWARM_RATIO_STRIDE;
			out.swarmRatio[ratioBase + 0] = reply_[i * kReplyStride + 4];
			out.swarmRatio[ratioBase + 1] = reply_[i * kReplyStride + 5];
			out.swarmRatio[ratioBase + 2] = reply_[i * kReplyStride + 6];
			// Urgency follows the score until the policy learns a head for it,
			// so the reconciler still prioritises the cells the net is most
			// confident about rather than acting in scan order.
			out.urgency[i] = reply_[i * kReplyStride + 1];
		}
		return true;
	}
} // namespace Neurotica
