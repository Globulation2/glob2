/*
  Fog-safe, deterministic tactical policy for Maxima.

  This module deliberately has no dependency on the game runtime.  The AI
  adapter supplies observations that the team can currently see and converts
  the selected intent into ordinary Globulation 2 flag orders.
 */

#ifndef AI_MAXIMA_TACTICS_H
#define AI_MAXIMA_TACTICS_H

#include <map>
#include <vector>

namespace AIMaxima
{
namespace Tactics
{

const int NoFlag=-1;
const int NoTeam=-1;
const int NoTarget=-1;

enum MissionKind
{
	MissionNone,
	MissionRaid,
	MissionSiege
};

/// The offense either holds a flag on a target or it does not; there are no
/// muster, transit, withdrawal or cooldown states to pass through.
enum MissionPhase
{
	PhaseIdle,
	PhaseEngage
};

struct WorkerSighting
{
	WorkerSighting();
	WorkerSighting(int gid, int team, int x, int y, int tick,
		bool harvesting, bool carrying, int economicValue);

	int gid;
	int team;
	int x;
	int y;
	int tick;
	bool harvesting;
	bool carrying;
	int economicValue;
};

struct ThreatSighting
{
	ThreatSighting();
	ThreatSighting(int gid, int team, int x, int y, int power);

	int gid;
	int team;
	int x;
	int y;
	int power;
};

struct RaidRules
{
	RaidRules();
	int width;
	int height;
	int tick;
	int clusterRadius;
	int threatRadius;
	int workerMinimum;
	int workerWeight;
	int harvestingBonus;
	int carryingBonus;
	int resourceWeight;
	int defenderPenalty;
};

struct RaidCandidate
{
	RaidCandidate();
	int team;
	int x;
	int y;
	int tick;
	int workers;
	int harvesters;
	int carriers;
	int economicValue;
	int defenders;
	int defenderPower;
	int score;
	std::vector<int> workerGids;
};

/// The live offensive objective: which flag is out, what it sits on, and the
/// two clocks the planner reads (how long this objective has been held, and
/// when the target last took damage).
struct Mission
{
	Mission();
	void reset();

	MissionKind kind;
	MissionPhase phase;
	int flagId;
	int targetTeam;
	int targetGid;
	int targetX;
	int targetY;
	int requestedForce;
	int startedTick;
	int phaseSinceTick;
	int lastProgressTick;
	int candidateScore;
	int lastTargetHp;
};

class Program
{
public:
	Program();
	void reset();
	void beginObservation(int tick);
	void observeWorker(const WorkerSighting& worker);
	/// Callers supply hostile forces only, including other enemies near a raid.
	void observeThreat(const ThreatSighting& threat);
	void replaceThreats(int tick, const std::vector<ThreatSighting>& threats);
	void finishObservation(const RaidRules& rules);

	const std::vector<WorkerSighting>& workers() const { return currentWorkers; }
	const std::vector<ThreatSighting>& threats() const { return currentThreats; }
	const std::vector<RaidCandidate>& raidCandidates() const { return raids; }
	const RaidCandidate* bestRaidForTeam(int team) const;

	static bool targetQuarantined(int gid, int tick, bool enabled,
		const std::map<int, int>& quarantineUntil);

private:
	void refreshRaidThreats();
	RaidRules observationRules;
	int observationTick;
	std::vector<WorkerSighting> currentWorkers;
	std::vector<ThreatSighting> currentThreats;
	std::vector<RaidCandidate> raids;
};

const char* missionKindName(MissionKind kind);
const char* missionPhaseName(MissionPhase phase);

}
}

#endif
