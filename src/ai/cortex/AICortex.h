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
class Building;

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

	/// Find our team's single live WAR_FLAG virtual building, or NULL if none.
	/// Cortex keeps at most one war flag and does NOT persist its gid; it is
	/// re-found each decision cycle by scanning team->virtualBuildings (a list,
	/// iterated in deterministic insertion order — never a std::set).
	Building* findOwnWarFlag() const;

	/// Ensure our single war flag sits at map tile (tx, ty): create it there if
	/// we have none (respecting the flag cooldown and the virtual-building room
	/// check), or move the existing one if it is far from the target. radius/count
	/// come from the action (clamped). Appends at most one Order to orderQueue.
	void ensureWarFlagAt(int tx, int ty, const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);

	/// Remove our war flag (OrderDelete) if one exists.
	void clearOwnWarFlag();

	/// Find the single best finished instance of `buildingType` (an
	/// IntBuildingType shortTypeNum) to upgrade to its next level, or NULL if no
	/// instance currently passes the full engine Upgradable predicate. Scans
	/// team->myBuildings by ARRAY INDEX (never a std::set) and ranks eligible
	/// instances deterministically — improving on Nicowar's random pick. See the
	/// .cpp for the predicate and the bottleneck ranking.
	Building* findUpgradeTarget(int buildingType) const;

	Player* player;

	Cortex::CortexPolicy policy;
	int timer;

	/// Game tick before which translateAction refuses to issue another build
	/// (see BUILD_COOLDOWN_TICKS). 0 = no build pending.
	int buildCooldownUntil;

	/// Game tick before which ensureWarFlagAt refuses to create another war flag.
	/// Same BUILD_COOLDOWN_TICKS latency (an OrderCreate for a virtual flag also
	/// takes several ticks to register before findOwnWarFlag can see it), but a
	/// SEPARATE timer from buildCooldownUntil: a queued economy build must never
	/// stall a time-critical flag placement — above all a defensive recall when the
	/// base is under attack — by up to BUILD_COOLDOWN_TICKS. 0 = no flag create
	/// pending.
	int flagCooldownUntil;

	/// Per-game wheat open-margin N: the first N rows of wheat nearest the harvest
	/// source stay unpainted; the checkerboard starts at depth N+1. Drawn ONCE via
	/// syncRand on the first decision cycle (sentinel -1 = not yet drawn) and then
	/// persisted (NOT redrawn on load) so same-seed replays stay byte-identical.
	/// The draw consumes one syncRand() → it shifts the shared RNG stream, so this
	/// is replay-relevant (validated against the deterministic harness).
	Sint32 wheatOpenMargin;

	/// Orders awaiting emission, one popped per getOrder() call.
	std::queue<std::shared_ptr<Order> > orderQueue;
};
