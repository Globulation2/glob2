/*
  Maxima private farming primitives.

  These types deliberately live outside Map and the shared AI runtime.  They are
  deterministic, allocation-free during queries, and are never serialized.
 */

#ifndef AI_MAXIMA_FARMING_H
#define AI_MAXIMA_FARMING_H

#include <stdint.h>
#include <vector>

namespace AIMaxima
{
namespace Farming
{

/// All farm patterns share the map's fixed 2x2 phase. Interior seeds are a
/// subset of the boundary checkerboard, so transitions never release a seed.
inline bool isInteriorSeed(int x, int y) { return (x&1) && (y&1); }
inline bool isExpansionCell(int x, int y) { return (x&1)==(y&1); }
inline bool isCoastalOpeningCell(int x, int y) { return !isExpansionCell(x, y); }

enum FertilityCalculationPath
{
	AdaptiveFertilityPath,
	SandCorrectionFertilityPath,
	WaterSplatFertilityPath
};

class ExactFertilityCache
{
public:
	ExactFertilityCache();

	/// Rebuilds the exact growth numerator. Masks use row-major y*width+x layout.
	void rebuild(int width, int height, const std::vector<uint8_t>& water,
		const std::vector<uint8_t>& sand,
		FertilityCalculationPath path=AdaptiveFertilityPath);

	bool validFor(int width, int height) const;
	uint32_t at(int x, int y) const;
	const std::vector<uint32_t>& values() const { return fertility; }
	FertilityCalculationPath pathUsed() const { return usedPath; }
	int waterCount() const { return waterTiles; }
	int sandCount() const { return sandTiles; }

private:
	int width;
	int height;
	int waterTiles;
	int sandTiles;
	FertilityCalculationPath usedPath;
	// uint32_t is Glob2's Uint32 storage width without importing SDL here.
	std::vector<uint32_t> fertility;
	std::vector<uint32_t> first;
	std::vector<uint32_t> second;
	std::vector<int> wrappedX;
	std::vector<int> wrappedY;
	int wx(int x, int offset) const;
	int wy(int y, int offset) const;
	void buildWrappedIndexes();
	void buildWaterConvolution(const std::vector<uint8_t>& water);
};

/// fertility * amount/8 * available-neighbours/8, with wheat's 1/3 factor.
uint32_t usefulExpansionCapacity(uint32_t fertility, int amount,
	int availableNeighbors, bool wheat);

int woodSupplyScore(int accessibleWood, int population,
	int scale=300, int populationOffset=30);
int woodClearPressure(int spaceCapacity, int woodSupply,
	int recentConstructionFailures, int growthDemand,
	int base, int spaceDivisor, int supplyDivisor,
	int constructionDivisor, int growthDivisor,
	int constructionFailurePressure=25);
uint32_t minimumWoodFertility(int pressure, int basePercent,
	int pressurePercent);

/// True when fertility lies inside an inclusive percentage band of the
/// engine's 65536-point fertility scale.
bool fertilityWithinPercentBand(uint32_t fertility, int minimumPercent,
	int maximumPercent);

/// Tests the eight-cell toroidal ring around a tile for protected wheat farm.
bool hasAdjacentProtectedWheat(const std::vector<uint8_t>& protectedWheat,
	int width, int height, int x, int y);

struct ReservationClearingSelection
{
	std::vector<int> tiles;
	int preservedResourceTiles;
	int fallbackEntranceTile;
	ReservationClearingSelection(): preservedResourceTiles(0),
		fallbackEntranceTile(-1) {}
};

/// Keeps valuable resources around one member's actual footprint. Connects an
/// entrance to worker-reachable land through circulation, minimizing resource
/// burden first and path length second. Reachable land excludes obstructions.
/// Call separately for each campus member; one entrance to the combined parcel
/// does not provide access to all of its buildings.
ReservationClearingSelection selectResourcePreservingCirculation(
	int width, int height, const std::vector<int>& footprint,
	const std::vector<int>& circulation,
	const std::vector<uint8_t>& preservedResources,
	const std::vector<int>& resourceBurden,
	const std::vector<uint8_t>& reachable);

struct CoastalBarrierPorosityResult
{
	// Counts refer to enclosed open pockets, not entire connected landmasses.
	int sealedComponents;
	int restoredComponents;
	int openedTiles;
	CoastalBarrierPorosityResult(): sealedComponents(0),
		restoredComponents(0), openedTiles(0) {}
};

/// Checks each open interior pocket independently. Opens fixed cross-parity
/// cells on enclosing coastal contours, then connects each still-unreachable
/// pocket to shore with a minimum-cost channel restricted to the envelope.
/// These passive openings are not strategic gates or clearing contracts.
CoastalBarrierPorosityResult makeSealedCoastalFarmBarriersPorous(
	int width, int height, const std::vector<uint8_t>& land,
	const std::vector<uint8_t>& shore, const std::vector<uint8_t>& interior,
	const std::vector<uint8_t>& coastalFarm,
	const std::vector<uint8_t>& porosityEnvelope,
	std::vector<uint8_t>& protectedTiles,
	std::vector<uint8_t>& protectedWheat);

/// Construction-space clearing is warranted by either live space scarcity or
/// a recent run of placements obstructed by clearable resources.
bool openingSpaceConstrained(int spaceCapacity, int urgentSpaceThreshold,
	int recentConstructionFailures, int failureThreshold,
	int ticksSinceConstructionFailure, int failureWindowTicks);

/// Shortest toroidal distance from a candidate to either previous gate.
/// Returns zero when there is no complete previous pair.
int gateRelocationDistance(
	const std::vector<std::vector<int> >& previousGates,
	const std::vector<int>& gate, int width, int height);
/// Sum of the shortest matched toroidal distances between an old and new
/// two-gate pair. Returns zero when there is no complete previous pair.
int gatePairRelocationDistance(
	const std::vector<std::vector<int> >& previousGates,
	const std::vector<int>& firstGate, const std::vector<int>& secondGate,
	int width, int height);

}
}

#endif
