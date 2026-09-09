/*
  Maxima private reconnaissance model.

  This file deliberately has no dependency on the game runtime.  The Maxima AI
  adapter supplies only observations that its team can currently see.
 */

#ifndef AI_MAXIMA_RECON_H
#define AI_MAXIMA_RECON_H

#include <map>
#include <vector>

namespace AIMaxima
{
namespace Recon
{

const int NoTeam=-1;
const int NoFlag=-1;

struct BuildingSighting
{
	BuildingSighting();
	BuildingSighting(int gid, int team, int type, int x, int y,
		int width, int height, bool construction, int tick);

	int gid;
	int team;
	int type;
	int x;
	int y;
	int width;
	int height;
	bool construction;
	int lastSeenTick;
	bool currentlyVisible;
};

struct OpponentIntel
{
	OpponentIntel();

	bool alive;
	int visibleWarriors;
	int visibleExplorers;
	int visibleAttackExplorers;
	int visibleBuildings;
	int lastObservedWarriors;
	int lastObservedExplorers;
	int estimatedWarriors;
	int estimatedExplorers;
	int knownBuildings;
	int strategicValue;
	int reachableBuildings;
	int nearestBuilding;
	int lastSeenTick;
	int lastForceSeenTick;
	int lastWarriorSeenTick;
	int lastExplorerSeenTick;
	int lastBuildingSeenTick;
	int lastEconomicSeenTick;
	int lastEconomicX;
	int lastEconomicY;
	int confidence;
	std::map<int, BuildingSighting> buildings;
};

struct ReconMission
{
	ReconMission();
	ReconMission(int flagId, int targetTeam, bool frontier,
		int x, int y, int tick, bool economicWatch=false);

	int flagId;
	int targetTeam;
	bool frontier;
	bool economicWatch;
	int x;
	int y;
	int createdTick;
	int lastRetaskTick;
};

struct MissionObjective
{
	MissionObjective();
	MissionObjective(int targetTeam, bool frontier, int x, int y, int score,
		bool economicWatch=false);

	int targetTeam;
	bool frontier;
	bool economicWatch;
	int x;
	int y;
	int score;
};

struct ReconReport
{
	ReconReport();

	std::map<int, OpponentIntel> opponents;
	std::vector<ReconMission> missions;
	int tick;
	int visibleWarriors;
	int visibleExplorers;
	int visibleAttackExplorers;
	int visibleColonyThreat;
	int visibleColonyExplorerThreat;
	int aliveEnemies;
	int exploredPercent;
	int desiredMissions;
};

class Program
{
public:
	Program();
	void configure(int memoryHorizonTicks, int forceMemoryHoldTicks,
		int staleContactAgeTicks, bool forceMemoryEnabled=true);

	void reset();
	void beginForceObservation(int tick, const std::vector<int>& livingTeams);
	void beginObservation(int tick, const std::vector<int>& livingTeams);
	void observeUnit(int team, bool warrior, bool explorer,
		bool attackExplorer, bool colonyThreat, bool colonyExplorerThreat);
	void observeEconomicActivity(int team, int x, int y);
	void observeBuilding(const BuildingSighting& sighting);
	void confirmBuildingAbsent(int team, int gid);
	void finishForceObservation();
	void finishObservation();
	void setBuildingAssessment(int team, int strategicValue,
		int reachableBuildings, int nearestBuilding);
	void setExploredPercent(int exploredPercent);
	void setDesiredMissions(int desiredMissions);

	const ReconReport& report() const { return current; }
	ReconReport& mutableReport() { return current; }
	const OpponentIntel* opponent(int team) const;
	OpponentIntel* mutableOpponent(int team);

	void addMission(const ReconMission& mission);
	void removeMission(int flagId);
	void clearMissions();

	static int confidenceForAge(int age, int memoryHorizonTicks);
	static int confidenceForAge(int age, int memoryHoldTicks,
		int memoryHorizonTicks);
	static int weightedEstimate(int observation, int age,
		int memoryHoldTicks, int memoryHorizonTicks);
	static int staleOrUnseenEnemies(const ReconReport& report, int tick,
		int staleContactAgeTicks);
	static int desiredMissionCount(int livingEnemies, int staleOrUnseen,
		int population, bool emergency, int populationDivisor=30);
	static std::vector<MissionObjective> planObjectives(
		const ReconReport& report, int desiredMissions, int width, int height,
		const std::vector<unsigned char>& discovered, int radius);
	static int frontierScore(int x, int y, int width, int height,
		const std::vector<unsigned char>& discovered, int radius);
	static int exploredPercentInRadius(int x, int y, int width, int height,
		const std::vector<unsigned char>& discovered, int radius);

private:
	ReconReport current;
	int memoryHorizonTicks;
	int forceMemoryHoldTicks;
	int staleContactAgeTicks;
	bool forceMemoryEnabled;
};

}
}

#endif
