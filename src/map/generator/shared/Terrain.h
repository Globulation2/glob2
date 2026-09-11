#pragma once
#include "GenerationRequest.h"
#include "HeightMap.h"
#include <functional>
#include <vector>
class Game;
struct GenerationContext;
namespace MapGeneration
{
struct HeightFieldOptions
{
	int water, sand, grass, desert, smoothing, fruit, repeat;
	bool swamp = false;
	// Each resource band as a percentage of its default size; 100 reproduces it exactly.
	int wheat = 100, wood = 100, stone = 100, algae = 100;
	// Stone on the highest grass rather than beside the shore.
	bool hilltopStone = false;
};
// The resource controls every height-field generator shares, and reading them from a request.
std::vector<GeneratorControl> heightFieldResourceControls();
void readResourceControls(HeightFieldOptions &, const GenerationRequest &);
// Call after placeStarts: at any non-default resource amount, open up a colony that the widened
// bands walled in (openCrampedStarts) and re-run the wheat/wood guarantee in case the clearing
// took its nearest crop too. Nothing happens at the default amounts.
void openStartsBuriedByAmounts(Game &, GenerationContext &, const HeightFieldOptions &);
using HeightFieldBuilder = std::function<void(HeightMap &, unsigned, unsigned, float)>;
bool generateHeightField(Game &, GenerationContext &, const HeightFieldOptions &,
						 const HeightFieldBuilder &);
} // namespace MapGeneration
