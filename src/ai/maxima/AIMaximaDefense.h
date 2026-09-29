/* Deterministic, runtime-independent Maxima preemptive-defense topology. */
#ifndef AI_MAXIMA_DEFENSE_H
#define AI_MAXIMA_DEFENSE_H

#include <vector>

namespace AIMaxima
{
namespace Defense
{

enum MovementMode
{
	LandMode=0,
	AmphibiousMode=1
};

enum CandidateState
{
	CandidateUnselected=0,
	CandidateSelected=1,
	CandidateRejectedOverlap=2,
	CandidateRejectedCap=3
};

struct EnemySources
{
	EnemySources(int team=-1) : team(team) {}
	int team;
	std::vector<int> sources;
};

struct Policy
{
	Policy();
	int innerDistance;
	int bandWidth;
	int pathSlack;
	int probeRadius;
	int maximumCrossSection;
	int zoneRadius;
};

struct ModeInput
{
	ModeInput();
	MovementMode mode;
	int width;
	int height;
	std::vector<unsigned char> walkable;
	std::vector<int> homeSources;
	std::vector<EnemySources> enemies;
};

struct TeamField
{
	TeamField();
	int team;
	int shortestDistance;
	std::vector<int> enemyDistance;
	std::vector<unsigned char> corridor;
	std::vector<int> corridorWidth;
	std::vector<int> terrainWidth;
};

struct Candidate
{
	Candidate();
	MovementMode mode;
	int index;
	int memberships;
	int crossSection;
	int terrainCrossSection;
	int bandOffset;
	int homeDistance;
	CandidateState state;
	std::vector<int> footprint;
};

struct ModeResult
{
	ModeResult();
	MovementMode mode;
	int width;
	int height;
	std::vector<unsigned char> walkable;
	std::vector<int> homeDistance;
	std::vector<TeamField> teams;
	std::vector<int> memberships;
	std::vector<int> minimumCrossSection;
	std::vector<int> minimumTerrainCrossSection;
	std::vector<unsigned char> qualified;
	std::vector<Candidate> candidates;
};

struct PlanResult
{
	PlanResult();
	int width;
	int height;
	int effectiveZoneCap;
	int selectedCount;
	std::vector<ModeResult> modes;
	std::vector<Candidate> candidates;
	std::vector<unsigned char> desired;
};

void computeDistanceField(int width, int height,
	const std::vector<unsigned char>& walkable,
	const std::vector<int>& sources, std::vector<int>& distances);

ModeResult analyzeMode(const ModeInput& input, const Policy& policy);

std::vector<int> buildFootprint(const ModeResult& mode, int center,
	int radius);

PlanResult combineModes(const std::vector<ModeResult>& modes,
	int effectiveZoneCap, int zoneRadius);

bool betterCandidate(const Candidate& left, const Candidate& right);

int effectiveZoneCap(int trainedWarriors, int configuredMinimum,
	int warriorsPerZone, int configuredMaximum);

bool amphibiousEligible(bool systemActive, bool configuredEnabled,
	int swimmingWarriors, int configuredMinimum);

bool topologyRefreshRequired(unsigned int currentBuildingSignature,
	unsigned int cachedBuildingSignature, int currentEffectiveCap,
	int cachedEffectiveCap, bool currentAmphibiousEligibility,
	bool cachedAmphibiousEligibility, int tick, int lastRefreshTick,
	int refreshInterval, bool hasDiagnosticSnapshot);

}
}

#endif
