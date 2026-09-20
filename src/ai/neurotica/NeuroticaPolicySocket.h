// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/* Private NPS6 learning protocol (independent of game replay/save format).
 Client sends magic NPS6, then repeated length-prefixed NAC1 contexts.
 Server returns length-prefixed semantic Action packets (NeuroticaActions.h).
 All lengths/integers are little endian. A chosen Action is executed directly;
 its delay says when the next decision occurs. No implicit decode/field edits.
 Configured policy failures throw and invalidate the game; no inert fallback.
*/

#include "NeuroticaDesiredState.h"
#include "NeuroticaFieldSource.h"

#include <string>
#include <vector>

class Team;
class Order;

namespace Neurotica
{
	class PolicySocketSource : public FieldSource
	{
	  public:
		PolicySocketSource(Team *team, const std::string &socketPath);
		~PolicySocketSource() override;

		bool field(Uint32 tick, DesiredState &out) override;
		std::shared_ptr<Order> actionOrder(Uint32 tick);
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
		Uint32 nextTick_ = 0;
		std::vector<Uint8> reply_;
	};
} // namespace Neurotica
