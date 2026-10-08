// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "render/scene/SceneExtract.h"

// The presentation job owns only a projection of the published boundary and
// an immutable client request. No serially extracted PresentationFrame or live pointees.
struct SceneInputs
{
    SimulationSnapshot::Handle world;
    SceneRequest request;
};
