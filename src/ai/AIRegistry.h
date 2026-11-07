// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AI.h"

// Creates and loads the AI implementation for an id (src/ai/AIRegistry.cpp).
// Ids are stored in saves and replays; an unknown id loads as Failed.
namespace AIRegistry
{
AIImplementation* create(AI::ImplementationID, Player *);

enum class Loaded { Yes, Failed, FailedInSection };
// FailedInSection: the caller still has to leave its "AI" section.
Loaded load(AI::ImplementationID, GAGCore::InputStream *, Player *, Sint32 versionMinor, AIImplementation *&implementation);
}
