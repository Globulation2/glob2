// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct TidalFlatsOptions {
  int homeIslandSize, extraIslands, sandbars, tidePools, lagoons, coastRoughness;
  bool centralIsland;
  int wheat, wood, stone, algae, fruit; // percentages of the default amounts
  explicit TidalFlatsOptions(const GenerationRequest &r);
};
GeneratorDefinition tidalFlatsDefinition();
