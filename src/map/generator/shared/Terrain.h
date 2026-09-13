// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "HeightMap.h"
#include <functional>
#include <vector>
class Game;
class Map;
struct GenerationContext;
namespace MapGeneration
{
// The height-field generators (Swamp, River, Islands, Crater lakes): one Perlin height field
// shapes the terrain and paints the resource bands from the same values, then the colonies take
// the most balanced sites on it. generateHeightField runs the stages below in order; each is
// also callable on its own, so a generator can take the field's terrain and do its own resources,
// or its starts, or its groves.

struct HeightFieldOptions
{
	int water, sand, grass, desert, smoothing, fruit, repeat;
	bool swamp = false;
	// Each resource band as a percentage of its default size; 100 reproduces it exactly.
	int wheat = 100, wood = 100, stone = 100, algae = 100;
	// Stone on the highest grass rather than beside the shore.
	bool hilltopStone = false;
	/// The shared terrain and resource controls of a request. A swamp has no sand or desert
	/// weight and takes its water against its grass alone.
	static HeightFieldOptions fromRequest(const GenerationRequest &, bool swamp);
};
// The resource controls every height-field generator shares.
std::vector<GeneratorControl> heightFieldResourceControls();

/// How the field tiles the map: `repeat` halvings shared out between the axes, so a field of
/// w by h is stamped wRepeat by hRepeat times.
struct HeightFieldTiling
{
	int wRepeat = 1, hRepeat = 1;
	unsigned w = 0, h = 0;
};
HeightFieldTiling heightFieldTiling(int mapWidth, int mapHeight, int repeat);

/// The field's terrain and resource bands as thresholds on its values, from the shares the
/// options ask for: below water is water, below sand beach, below grass grass and above it
/// desert; algae is the lowest of the water, stone a band just above the beach (or the highest
/// grass), and wheat and wood the farmland above.
struct HeightFieldLevels
{
	float water = 0, sand = 0, grass = 0, wheat = 0, wood = 0, algae = 0, stone = 0, stoneFloor = 0;
};
HeightFieldLevels classifyHeightField(HeightMap &, const HeightFieldTiling &,
									  const HeightFieldOptions &);

/// Writes the field's terrain to the undermap, tiled, and runs the engine's sand control.
void paintHeightFieldTerrain(Map &, HeightMap &, const HeightFieldTiling &,
							 const HeightFieldLevels &);
/// Paints algae, stone, wheat and wood as bands of the field; wheat and wood alternate along the
/// field's own slope so the two come out in smooth areas rather than rings.
void paintHeightFieldResources(Map &, HeightMap &, const HeightFieldTiling &,
							   const HeightFieldLevels &, const HeightFieldOptions &);
/// Every colony's boot tile: chooseBalancedStarts on the finished resources, falling back to
/// the oldest search (the widest grass patch each) where no balanced set exists. False when
/// the map has no room at all.
bool chooseHeightFieldStarts(Game &, GenerationContext &);
/// `count` fruit groves of a random kind each, grown tile by tile over free grass; false with a
/// diagnostic when no grass is free.
bool plantHeightFieldGroves(Map &, GenerationContext &, const HeightFieldTiling &, int count);

// Call after placeStarts: at any non-default resource amount, open up a colony that the widened
// bands walled in (openCrampedStarts) and re-run the wheat/wood guarantee in case the clearing
// took its nearest crop too. Nothing happens at the default amounts.
void openStartsBuriedByAmounts(Game &, GenerationContext &, const HeightFieldOptions &);

using HeightFieldBuilder = std::function<void(HeightMap &, unsigned, unsigned, float)>;
/// The whole pipeline: build the field, classify and paint its terrain and resources, choose
/// the starts, guarantee their crops and plant the groves. The caller then places the colonies.
bool generateHeightField(Game &, GenerationContext &, const HeightFieldOptions &,
						 const HeightFieldBuilder &);
} // namespace MapGeneration
