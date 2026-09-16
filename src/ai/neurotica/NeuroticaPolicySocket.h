// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Neurotica policy bridge: the engine asks a neural network what it wants.

  The network lives in a Python process holding the GPU; this is the C++ end of
  a Unix-domain socket to it. The engine encodes the same observation planes the
  training corpus uses (NeuroticaObservation.h), sends them, and gets back a
  desired-state field that NeuroticaReconciler turns into orders.

  Why a socket rather than embedding LibTorch: training and inference want the
  network in Python, the policy rate is ~1 Hz per team so the round trip is
  cheap relative to a tick, and keeping the engine free of a deep-learning
  runtime means nothing about a normal build changes. Throughput for large-scale
  self-play will eventually want shared memory instead; the wire format below is
  deliberately trivial so that swap is local.

  Protocol (little-endian, no framing beyond the lengths shown):

    HANDSHAKE, client -> server, once per connection
      [4B] magic "NPS1"
      [2B] u16 map_w
      [2B] u16 map_h
      [1B] u8  num_static_planes
      [1B] u8  num_dynamic_planes
      [2B] pad
      [...] static planes, num_static * w * h bytes

    REQUEST, client -> server, per policy step
      [4B] u32 tick
      [1B] u8  team_number
      [3B] pad
      [...] dynamic planes, num_dynamic * w * h bytes

    RESPONSE, server -> client
      [...] 3 * w * h bytes, cell-major:
              [0] building class, 0 = none, 1..13 = type + 1
              [1] score 0..255 for that class at that cell
              [2] area bits (NeuroticaDesiredState.h AreaBit mask)

  A failed connection, short read or short write leaves the source inert rather
  than throwing: an AI whose policy server has died should stand still, not
  crash the game or act on a half-read field.
*/

#include "NeuroticaDesiredState.h"
#include "NeuroticaFieldSource.h"

#include <string>
#include <vector>

class Team;

namespace Neurotica
{
	class PolicySocketSource : public FieldSource
	{
	public:
		PolicySocketSource(Team *team, const std::string &socketPath);
		~PolicySocketSource() override;

		bool field(Uint32 tick, DesiredState &out) override;
		const char *name() const override { return "policy-socket"; }

		bool connected() const { return fd_ >= 0; }

	private:
		bool ensureConnected();
		void disconnect();
		bool writeAll(const void *data, size_t bytes);
		bool readAll(void *data, size_t bytes);

		Team *team_ = nullptr;
		std::string path_;
		int fd_ = -1;
		//! Set once a connection has failed, so a dead server costs one attempt
		//! per game rather than a syscall every policy step.
		bool gaveUp_ = false;
		std::vector<Uint8> planes_;
		std::vector<Uint8> reply_;
	};
} // namespace Neurotica
