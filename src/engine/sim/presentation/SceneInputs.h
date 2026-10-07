// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "render/scene/SceneExtract.h"
#include "AITelemetry.h"

// Owner-captured source data not already represented by the shared components.
// Passed as const to preparation; no live Game/Map/Unit/Building survives capture.
struct SceneInputs
{
    SimulationSnapshot::Handle world;
    SceneRequest request;
    Scene source;
    Uint16 fertilityMaximum = 0;
    std::array<bool, SceneEntities::Teams> lost{};
    struct Telemetry
    {
        SceneAITelemetry row;
        std::vector<AITelemetry::Field> fields;
        std::vector<AITelemetry::Value> values;
        std::vector<AITelemetry::NamedValue> named;
    };
    std::vector<Telemetry> telemetry;
};
