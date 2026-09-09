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
	MissionSiege,
	MissionRelief
};

enum MissionPhase
{
	PhaseIdle,
	PhaseMuster,
	PhaseTransit,
	PhaseEngage,
	PhaseWithdraw,
	PhaseCooldown
};

enum SiegeTargetContinuity
{
	SiegeTargetTracked,
	SiegeTargetLost,
	SiegeTargetReplacementAvailable
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

struct Mission
{
	Mission();
	void reset();
	void retargetSiege(int gid, int x, int y, int tick);

	MissionKind kind;
	MissionPhase phase;
	int flagId;
	int targetTeam;
	int targetGid;
	int targetX;
	int targetY;
	int rallyX;
	int rallyY;
	int requestedForce;
	int minimumForce;
	int launchedForce;
	int startedTick;
	int phaseSinceTick;
	int lastContactTick;
	int lastProgressTick;
	int cooldownUntil;
	int initialTargetWorkers;
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

	static int desiredRaidForce(int workerCount, int forceBonus,
		int minimumForce, int maximumForce);
	static bool musterReady(int enrolled, int onSite, int requested,
		int requiredPercent);
	static bool musterLaunchAllowed(MissionKind kind, int enrolled, int onSite,
		int requested, int reliefMinimumForce, int requiredPercent);
	static bool raidRetargetAllowed(bool sameTeam, int distanceSquare,
		int followRadius, int candidateScore, int currentScore, int scoreMargin);
	static bool targetQuarantined(int gid, int tick, bool enabled,
		const std::map<int, int>& quarantineUntil);
	static bool raidUnsafe(int visibleDefenders, int launchedForce,
		int defenderMinimum, int defenderPercent);
	static bool casualtiesRequireWithdrawal(int enrolled, int launchedForce,
		int survivorMinimum, int casualtyPercent);
	static bool reliefRetargetAllowed(bool changed, bool sameTeam,
		int distanceSquare, int followRadius, int candidateScore,
		int currentScore, int scoreMargin);
	static SiegeTargetContinuity siegeTargetContinuity(bool targetRemembered,
		int currentTarget, int replacementTarget);
	static int forceForPower(const std::vector<int>& descendingPowers,
		int requiredPower, int minimumForce, int maximumForce);

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
