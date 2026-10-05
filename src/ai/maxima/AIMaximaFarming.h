/*
  Maxima private farming primitives.

  These types deliberately live outside Map and the shared AI runtime.  They are
  deterministic, allocation-free during queries, and are never serialized.
 */

#ifndef AI_MAXIMA_FARMING_H
#define AI_MAXIMA_FARMING_H

#include <stdint.h>
#include "FertilityField.h"
#include <utility>
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

// Preserve the private API name for existing callers; the authoritative ecology
// implementation is shared with simulation, generation and other AIs.
class ExactFertilityCache
{
public:
	void rebuild(int width, int height, const std::vector<uint8_t>& water,
		const std::vector<uint8_t>& sand,
		FertilityCalculationPath path=AdaptiveFertilityPath);
	void assign(Fertility::Field value, uint64_t revision)
	{ field=std::move(value); generation=revision; }
	bool validFor(int width, int height, uint64_t revision=0) const
	{ return width==field.getW() && height==field.getH() &&
		field.values().size()==size_t(width)*height && (!revision || generation==revision); }
	uint32_t at(int x, int y) const { return field.at(x,y); }
	const std::vector<uint32_t>& values() const { return field.values(); }
	FertilityCalculationPath pathUsed() const { return static_cast<FertilityCalculationPath>(field.pathUsed()); }
	int waterCount() const { return field.waterCount(); }
	int sandCount() const { return field.sandCount(); }
private:
	Fertility::Field field;
	uint64_t generation=0;
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

/// Whether removing the center preserves eight-connected access through its
/// 3x3 neighborhood. Bit y*3+x denotes open land; the center is ignored.
bool canProtectWithoutSplittingAccess(uint16_t openNeighborhood);

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

/// Construction-space clearing is warranted by either live space scarcity or
/// a recent run of placements obstructed by clearable resources.
bool openingSpaceConstrained(int spaceCapacity, int urgentSpaceThreshold,
	int recentConstructionFailures, int failureThreshold,
	int ticksSinceConstructionFailure, int failureWindowTicks);

}
}

#endif
