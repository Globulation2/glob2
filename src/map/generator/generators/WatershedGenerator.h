// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct WatershedOptions {
  int riverDensity, riverWidth, dryness, fords;
  explicit WatershedOptions(const GenerationRequest &r);
};
GeneratorDefinition watershedDefinition();
