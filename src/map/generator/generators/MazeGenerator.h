// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct MazeOptions {
  int cellSize, roomSize, corridorWidth, loopiness;
  int corn, wood, stone, algae, fruit;
  explicit MazeOptions(const GenerationRequest &r);
};
GeneratorDefinition mazeDefinition();
