// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __HARVEST_METRICS_H
#define __HARVEST_METRICS_H

// Optional counters for measuring how workers fetch map resources: how often
// they reach a resource, deliver it, turn back, or walk towards a tile that
// has fewer resources left than units heading for it. Enabled by setting
// GLOB2_HARVEST_METRICS in the environment; GLOB2_HARVEST_METRICS_EVERY=N also
// prints cumulative totals every N ticks.
//
// Measurement only: every hook reads game state and writes nothing but this
// module's own tables, so a run's checksum is the same with the metrics on or
// off. Hooks return at once when the metrics are off.

class Unit;
class Game;

namespace HarvestMetrics
{
	extern const bool enabled;

	//! The unit set out to fetch its destinationPurpose from the map.
	void onCommit(const Unit *unit);
	//! The unit took a step towards the resource it is fetching; retargeted
	//! when that step also moved its gradient destination.
	void onStep(const Unit *unit, bool retargeted);
	//! The resource gradient gave no step. abandoned: the unit gave up the job.
	//! ghost: it stands on a tile the gradient still takes for the resource.
	void onLost(const Unit *unit, bool abandoned, bool ghost);
	void onHarvest(const Unit *unit, int resource);
	void onDeliver(const Unit *unit, int resource);
	//! Once per simulated tick, before the step counter advances.
	void sample(const Game &game);
	//! Cumulative totals, one line per team and resource with any activity.
	void report(const Game &game);
	//! Resources of this type delivered by the team so far.
	unsigned long long deliveries(int teamNumber, int resource);
}

#endif
