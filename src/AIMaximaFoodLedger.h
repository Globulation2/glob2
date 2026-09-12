/*
  Deterministic protected-wheat food ledger used only by Maxima.

  The ledger answers one question: which inns and swarms are actually backed by
  farm capacity, and how much capacity is still unclaimed.  It is rebuilt from
  scratch on every planning pass, so a destroyed building releases its claim, an
  upgraded inn claims its new demand, and a burned or regrown farm changes
  supply without any incremental bookkeeping.

  Only protected wheat cells that currently carry wheat are supply.  Protected
  cells are the producers: their growth spreads into the harvestable cells
  around them, while unprotected wheat is harvested away and cannot be counted
  as a standing equilibrium supply.

  Like the placement planner this consumes a plain grid snapshot and holds no
  engine types, so its geometry and ordering stay independently testable.  All
  arithmetic is integral and ordering is total, because placement decisions are
  simulation inputs in lockstep games.
 */

#ifndef AI_MAXIMA_FOOD_LEDGER_H
#define AI_MAXIMA_FOOD_LEDGER_H

#include <stdint.h>
#include <vector>

namespace AIMaximaFoodLedger
{

enum ConsumerKind
{
	InnConsumer,
	SwarmConsumer
};

/// Lifecycle rank of a claimer. Completed buildings claim before construction
/// sites, which claim before parcels that are only reserved.
enum ConsumerStage
{
	CompletedStage,
	SiteStage,
	ReservedStage
};

/// One inn or swarm that eats wheat, including planned ones. Colony swarms are
/// ordinary claimers: they compete for the same wheat, they simply keep their
/// own placement rules and are exempt from retirement while establishing.
struct ConsumerInput
{
	ConsumerInput();
	/// Stable identity chosen by the caller. Building ids and planned actions
	/// must not collide; the adapter encodes planned actions as negative keys.
	int key;
	ConsumerKind kind;
	ConsumerStage stage;
	/// Full-capacity consumption in micro-wheat per tick.
	int demand;
	/// Demand level, used only to break quality ties between claimers.
	int level;
	int centerX;
	int centerY;
	/// Footprint relative to the centre, matching the planner's convention.
	int left;
	int top;
	int width;
	int height;
	bool colony;
	bool retirable;
};

struct Policy
{
	Policy();
	/// Maximum harvesting-route distance, in eight-neighbour path steps.
	int supplyRadius;
	/// Quality is quantised into bands of this many tiles so that small farm
	/// changes cannot reorder claimers and move shortages between them.
	int qualityBandTiles;
	/// Distance charged for demand a claimer cannot reach at all, expressed in
	/// tiles beyond the supply radius. It keeps unreachable sites ranked last
	/// without making quality depend on map size.
	int unreachablePenaltyTiles;
};

struct ConsumerResult
{
	ConsumerResult();
	int key;
	ConsumerKind kind;
	bool colony;
	bool retirable;
	int demand;
	/// Supply this claimer actually took, capped by its demand.
	int claimed;
	/// claimed plus the unclaimed supply it could still reach. An upgrade is
	/// judged against this, because its own claim is released and retaken.
	long long available;
	int coveragePercent;
	int availablePercent;
	/// Supply-weighted mean route distance to the wheat covering full demand,
	/// ignoring every other claimer, in hundredths of a tile.
	int quality;
	int qualityBand;
	/// Final position in the interleaved claim order.
	int order;
};

struct Input
{
	Input();
	int width;
	int height;
	Policy policy;
	/// Micro-wheat per tick produced by each protected wheat cell.
	std::vector<uint32_t> yield;
	/// Cells a harvesting worker may walk through, including protected wheat.
	std::vector<uint8_t> traversable;
	std::vector<ConsumerInput> consumers;
	int index(int x, int y) const;
	int normalizeX(int x) const;
	int normalizeY(int y) const;
};

struct Result
{
	Result();
	std::vector<ConsumerResult> consumers;
	/// Supply still unclaimed on every cell, in the same micro-wheat units.
	std::vector<uint32_t> residual;
	long long totalSupply;
	long long totalDemand;
	long long totalClaimed;
	long long totalResidual;
	/// Largest unclaimed supply inside any single supply-radius square. This is
	/// an upper bound on what one new building could reach, so it bounds how
	/// many further inns or swarms the map can still support.
	long long bestSiteResidual;
	const ConsumerResult* consumer(int key) const;
};

/// Holds the reusable scratch buffers. Queries never allocate once a map size
/// has been seen, which keeps the per-candidate placement check cheap.
class Ledger
{
public:
	Ledger();
	void evaluate(const Input& input, Result& result) const;

	/// Unclaimed supply a building with this footprint could reach, stopping
	/// once `cap` is met so a satisfied candidate never walks its whole radius.
	long long reachableResidual(const Input& input, const Result& result,
		int centerX, int centerY, int left, int top, int width, int height,
		long long cap) const;

	/// Sound upper bound for the same query, from a summed-area table over the
	/// residual. Reach is contained in the Chebyshev square, so a candidate
	/// rejected here would also fail the exact walk.
	long long residualUpperBound(const Input& input, const Result& result,
		int centerX, int centerY, int left, int top, int width,
		int height) const;

private:
	struct ReachCell
	{
		int index;
		int distance;
	};
	void walk(const Input& input, int centerX, int centerY, int left, int top,
		int width, int height, std::vector<ReachCell>& reach) const;
	void prepareResidualSums(const Input& input, const Result& result) const;
	mutable std::vector<int> distanceScratch;
	mutable std::vector<uint32_t> distanceGeneration;
	mutable uint32_t generation;
	mutable std::vector<ReachCell> reachScratch;
	mutable std::vector<ReachCell> queryScratch;
	mutable std::vector<long long> residualSums;
	mutable int residualSumsWidth;
	mutable int residualSumsHeight;
	mutable const Result* residualSumsSource;
	mutable long long residualSumsTotal;
};

/// Supply of one protected wheat cell, in micro-wheat per tick:
/// fertility/65536 growth chance, one sample per `growthPeriodTicks`, scaled by
/// the share of neighbouring cells its growth can actually spread into.
uint32_t cellYield(uint32_t fertility, int openNeighbors, int growthPeriodTicks);

/// Full-capacity demand of a swarm, in micro-wheat per tick.
int swarmDemand(int resourceForOneUnit, int unitProductionTime, int percent);

/// Full-capacity demand of an inn serving `servedUnits`, in micro-wheat per
/// tick, where a fed unit eats one wheat every `ticksPerMeal` ticks.
int innDemand(int servedUnits, int ticksPerMeal, int percent);

}

#endif
