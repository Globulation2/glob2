// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct StoneHighlandsOptions {
  int valleySize, passWidth, loopiness, pondSize, fruit;
  bool homeValleyFruit;
  int wheat, wood, stone, algae; // percentages of the default amounts
  explicit StoneHighlandsOptions(const GenerationRequest &r);
};
GeneratorDefinition stoneHighlandsDefinition();
