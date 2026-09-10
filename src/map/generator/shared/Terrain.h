#pragma once
#include "HeightMap.h"
#include <functional>
class Game;
struct GenerationContext;
namespace MapGeneration
{
struct HeightFieldOptions
{
	int water, sand, grass, desert, smoothing, fruit, repeat;
	bool swamp = false;
};
using HeightFieldBuilder = std::function<void(HeightMap &, unsigned, unsigned, float)>;
bool generateHeightField(Game &, GenerationContext &, const HeightFieldOptions &,
						 const HeightFieldBuilder &);
} // namespace MapGeneration
