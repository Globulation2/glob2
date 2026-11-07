// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AI.h"
#include <string>

// Creates and loads the AI implementation for an id (src/ai/AIRegistry.cpp).
// Ids are stored in saves and replays; an unknown id loads as Failed.
namespace AIRegistry
{
// False for ids compile_profile=core leaves out (and for ids that do not exist). Ids
// never change between profiles; create and load throw unavailableMessage for them.
bool available(int implementationID);
std::string unavailableMessage(int implementationID);

AIImplementation* create(AI::ImplementationID, Player *);

enum class Loaded { Yes, Failed, FailedInSection };
// FailedInSection: the caller still has to leave its "AI" section.
Loaded load(AI::ImplementationID, GAGCore::InputStream *, Player *, Sint32 versionMinor, AIImplementation *&implementation);

// Key=value tuning for AIs that take it through the registry (Cortex). False when the AI
// has no such hook; throws std::invalid_argument for a bad value.
bool applyTuning(int implementationID, const std::string &overrides, std::string &config);
// The JSON schema of those tuning values, or "" when the AI has none here.
std::string tuningSchemaJson(int implementationID);
// `glob2 dev dump-wheat`: Cortex's wheat-protection plan for one team of a map.
int dumpWheatPlan(const std::string &mapName, int team);
}
