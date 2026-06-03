// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "AIImplementation.h"
#include "CortexTypes.h"
#include "CortexPolicy.h"

#include <memory>
#include <queue>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Order;

// AICortex — variant A of the parent-class spike (docs/AI/cortex/NEXT.md open
// question #1). Subclasses AIImplementation DIRECTLY, owning the full
// observation -> policy -> action pipeline with no Echo framework in between.
//
// The engine constraint is one Order per getOrder() call. The action layer
// therefore translates one CortexAction into a *sequence* of Orders pushed onto
// orderQueue; getOrder() pops one per tick and returns NullOrder when the queue
// is empty. The policy is consulted on a slow cadence (OBSERVE_INTERVAL ticks),
// not every tick — cheap now, and the right shape for paying NN inference cost
// only on decision cycles later.

class AICortex : public AIImplementation
{
public:
	explicit AICortex(Player* player);
	AICortex(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor);
	~AICortex();

	bool load(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor);
	void save(GAGCore::OutputStream* stream);

	std::shared_ptr<Order> getOrder(void);

private:
	/// Ticks between policy invocations. The observation/policy run at this
	/// cadence; Order emission stays at tick rate via the queue.
	static const int OBSERVE_INTERVAL = 50;

	/// Ticks to suppress new build orders after issuing one, so the in-flight
	/// OrderCreate has time to execute and show up as a building site before the
	/// policy decides again (otherwise it re-issues duplicates the engine drops).
	static const int BUILD_COOLDOWN_TICKS = 250;

	void init(Player* player);

	/// Action layer (direct binding): translate an action intent into zero or
	/// more engine Orders, appended to orderQueue. NoOp queues nothing. The
	/// observation is passed alongside because an ACTION_BUILD only carries a
	/// candidate-slot index — the (x, y) lives in obs.buildCandidates.
	void translateAction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);

	Player* player;

	Cortex::CortexPolicy policy;
	int timer;

	/// Game tick before which translateAction refuses to issue another build
	/// (see BUILD_COOLDOWN_TICKS). 0 = no build pending.
	int buildCooldownUntil;

	/// Orders awaiting emission, one popped per getOrder() call.
	std::queue<std::shared_ptr<Order> > orderQueue;
};
