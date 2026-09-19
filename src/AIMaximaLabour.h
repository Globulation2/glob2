/*
  Labour economy for Maxima. See doc/MaximaLabourEconomy.md.

  Worker time is the resource every stage of the colony competes for, so it is
  the one currency here: a flow is worker-ticks per tick, and an investment has
  a cost and a return in worker-ticks.

  The calculations are pure functions of observations of the colony. Maxima
  retains their results between scheduling passes and saves that state so a
  loaded game makes the same decisions as the run that saved it.
 */

#ifndef AI_MAXIMA_LABOUR_H
#define AI_MAXIMA_LABOUR_H

#include <algorithm>

namespace AIMaxima
{
namespace Labour
{

struct Policy
{
	Policy(): reserveMaximumPercent(25), reserveBuffer(1),
		reserveMinimumWorkforce(16), minimumWorkersPerSwarm(1),
		bootstrapWorkforce(16), swarmFloorPercent(30) {}
	/// The training reserve never idles more than this share of the workforce.
	int reserveMaximumPercent;
	/// Free workers kept beyond the training reserve, so a new site or an empty
	/// inn is staffed without first starving another building.
	int reserveBuffer;
	/// Below this many workers nobody is held back: every hand is growth.
	int reserveMinimumWorkforce;
	int minimumWorkersPerSwarm;
	/// Below this many workers the opening is left alone: every hand a swarm
	/// asks for is growth, and the measured opening already matches the best.
	int bootstrapWorkforce;
	/// Afterwards births keep at least this share of the workforce.
	int swarmFloorPercent;
};

/// What the colony's workers are doing right now, and what could train them.
struct Observation
{
	Observation(): workers(0), eating(0), hurt(0), training(0), idle(0),
		swarmCarriers(0), innCarriers(0), builders(0), otherAssigned(0),
		trainable(0), trainingSlots(0), untrainedWalkers(0), barracksSeats(0), hospitals(0), hospitalSeats(0),
		hurtUnits(0), hungryUnits(0), inns(0), innSeats(0), swarms(0),
		swarmRequested(0), siteRequested(0) {}
	int workers;
	/// Workers the colony cannot assign: going to or inside an inn or hospital.
	int eating;
	int hurt;
	/// Workers walking to or inside a training building.
	int training;
	int idle;
	/// Workers attached to completed swarms, completed inns, sites, and the rest.
	int swarmCarriers;
	int innCarriers;
	int builders;
	int otherAssigned;
	/// Workers not yet training who could learn at a completed training building.
	int trainable;
	/// Worker places inside completed training buildings.
	int trainingSlots;
	/// Workers still at walk level 0.
	int untrainedWalkers;
	/// Places inside completed barracks.
	int barracksSeats;
	/// Completed hospitals, their places, and units of any kind needing them.
	int hospitals;
	int hospitalSeats;
	int hurtUnits;
	/// Units of any kind that are hungry, and the inn seats that could feed them.
	int hungryUnits;
	int inns;
	int innSeats;
	int swarms;
	/// Workers requested by completed swarms, and wanted by construction sites.
	/// Inn and tower requests are upper bounds that routinely exceed the
	/// workforce, so their actual carriers are counted instead.
	int swarmRequested;
	int siteRequested;
};

struct Plan
{
	Plan(): trainingReserve(0), assignable(0), swarmCap(0), uncapped(false) {}
	/// Workers deliberately left free: the engine sends only free workers to
	/// train, so slack is how the colony buys productivity.
	int trainingReserve;
	/// Workers available to requests once eating, healing, training and the
	/// reserve are set aside.
	int assignable;
	/// Ceiling on workers requested by all completed swarms together. Births
	/// take what subsistence, the reserve and construction leave.
	int swarmCap;
	/// The opening: swarm requests pass through untrimmed.
	bool uncapped;
};

// The scheduler consumes these snapshots between observation passes.
template<class Archive> void fields(Archive& a, Observation& value)
{
	a("workers", value.workers);
	a("eating", value.eating);
	a("hurt", value.hurt);
	a("training", value.training);
	a("idle", value.idle);
	a("swarmCarriers", value.swarmCarriers);
	a("innCarriers", value.innCarriers);
	a("builders", value.builders);
	a("otherAssigned", value.otherAssigned);
	a("trainable", value.trainable);
	a("trainingSlots", value.trainingSlots);
	a("untrainedWalkers", value.untrainedWalkers);
	a("barracksSeats", value.barracksSeats);
	a("hospitals", value.hospitals);
	a("hospitalSeats", value.hospitalSeats);
	a("hurtUnits", value.hurtUnits);
	a("hungryUnits", value.hungryUnits);
	a("inns", value.inns);
	a("innSeats", value.innSeats);
	a("swarms", value.swarms);
	a("swarmRequested", value.swarmRequested);
	a("siteRequested", value.siteRequested);
}

template<class Archive> void fields(Archive& a, Plan& value)
{
	a("trainingReserve", value.trainingReserve);
	a("assignable", value.assignable);
	a("swarmCap", value.swarmCap);
	a("uncapped", value.uncapped);
}

/// Warriors are work in progress until a barracks has trained them, and an
/// untrained warrior in the field is lost for nothing. Births are paced to the
/// seats that can train them: the backlog may hold two per seat, and never less
/// than the configured floor.
///
/// The floor matters more than it looks. Pacing to seats alone means a colony
/// with no barracks yet tolerates no backlog at all, and measured across 342
/// games that froze warrior production at exactly two untrained warriors in
/// every single game, for about half of all pre-barracks time, while the
/// director still wanted more warriors in 100% of those snapshots. Losses
/// spent close to twice the share of the game frozen that wins did.
inline int warriorBacklogLimit(int barracksSeats, int floorWarriors)
{
	return std::max(std::max(0, floorWarriors), 2*std::max(0, barracksSeats));
}

/// One capacity policy, rounded up to whole beds; sites and upgrades are
/// credited separately by the development planner.
inline int hospitalBedsWanted(int warriors, int bedsPerWarriorPercent)
{
	return (std::max(0, warriors)*std::max(0, bedsPerWarriorPercent)+99)/100;
}

/// Damage rate of a warrior by combat level: attack speed times what its
/// strength leaves after armour (src/game/entities/Race.cpp, armour 10).
const int WarriorDamageRate[4]={36, 64, 110, 168};

/// Whether an army's damage rate clears the defenders it is believed to face.
/// Defenders are taken at the mean of levels 1 and 2 (the measured mix by
/// mid-game), and the estimate they come from already runs about a fifth high,
/// so no further margin is added. A level-3 army needs fewer heads than a
/// level-1 one; counting heads alone under-rates the one that trained.
inline bool attackStrengthSufficient(long long ownDamageRate,
	int estimatedEnemyWarriors)
{
	const long long required=static_cast<long long>(std::max(0,
		estimatedEnemyWarriors))*(WarriorDamageRate[1]+WarriorDamageRate[2])/2;
	return ownDamageRate>=required;
}

/// Inns are a service too. The model's units-per-inn is a nominal figure; the
/// colony's hunger is the measurement. When the hungry outnumber half the
/// seats, the queue is forming and another inn is wanted, whatever the model
/// says; never fewer than already stand.
inline int innsWorthBuilding(int modelTarget, int inns, int innSeats,
	int hungryUnits, int maximum)
{
	int wanted=std::max(std::max(0, modelTarget), std::max(0, inns));
	if(2*std::max(0, hungryUnits)>std::max(0, innSeats)) wanted=std::max(wanted, inns+1);
	// The queue rule has to answer to the same ceiling as the model it
	// overrides. Without this it asked for one more inn than stood, forever:
	// measured over 456 games, seventeen per cent of colonies built past the
	// configured cap and one reached fifty inns for a hundred and eighty-five
	// units, which made economy.inn_target_cap not a cap at all.
	return std::min(std::max(0, maximum), wanted);
}

/// The labour budget. fundedSwarmWorkers is the swarm controller's food-funded
/// birth labour; the budget never raises it, only trims it to what the rest of
/// the colony leaves.
inline Plan plan(const Observation& observation, const Policy& policy,
	int fundedSwarmWorkers)
{
	Plan result;
	const int workers=std::max(0, observation.workers);
	const int reserveCeiling=workers*std::max(0, policy.reserveMaximumPercent)/100;
	const int seats=std::max(0, observation.trainingSlots-observation.training);
	int reserve=0;
	if(workers>=policy.reserveMinimumWorkforce)
	{
		reserve=std::min(std::max(0, observation.trainable), seats);
		if(reserve>0 || observation.trainingSlots>0)
			reserve+=std::max(0, policy.reserveBuffer);
	}
	result.trainingReserve=std::min(reserve, reserveCeiling);
	result.assignable=std::max(0, workers-observation.eating-observation.hurt
		-observation.training-result.trainingReserve);
	if(workers<policy.bootstrapWorkforce)
	{
		result.swarmCap=std::max(0, observation.swarmRequested);
		result.uncapped=true;
		return result;
	}
	const int floor=std::max(std::max(0, observation.swarms)
			*std::max(0, policy.minimumWorkersPerSwarm),
		(workers*std::max(0, policy.swarmFloorPercent)+99)/100);
	// Food funds births; the floor only keeps a funded colony from being
	// squeezed out, so the funding is raised to it rather than the reverse.
	const int funded=std::max(std::max(0, fundedSwarmWorkers), floor);
	const int residual=result.assignable-std::max(0, observation.innCarriers)
		-std::max(0, observation.siteRequested)
		-std::max(0, observation.otherAssigned);
	result.swarmCap=std::max(floor, std::min(funded, residual));
	return result;
}

/// Trim per-swarm requests to a total, taking one worker at a time from the
/// largest request (lowest index first on ties) and never below the minimum.
/// Requests are modified in place; returns the workers removed.
template<class Requests>
inline int trimToCap(Requests& requests, int cap, int minimumEach)
{
	int total=0;
	for(size_t i=0;i<requests.size();++i) total+=requests[i];
	int removed=0;
	while(total>cap)
	{
		int pick=-1;
		for(size_t i=0;i<requests.size();++i)
			if(requests[i]>minimumEach && (pick<0 || requests[i]>requests[pick]))
				pick=int(i);
		if(pick<0) break;
		--requests[pick];--total;++removed;
	}
	return removed;
}

}
}

#endif
