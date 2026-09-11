// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct WatershedOptions {
  int riverDensity, riverWidth, dryness, fords;
  bool delta, meanders;
  int wheat, wood, stone, algae, fruit; // percentages of the default amounts
  explicit WatershedOptions(const GenerationRequest &r);
};
GeneratorDefinition watershedDefinition();
