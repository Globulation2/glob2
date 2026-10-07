// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/EntityRef.h"
#include "AITelemetry.h"
#include "TeamStat.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

class GameObjectives;
class GameHints;

namespace SimulationSnapshot
{
// Optional world observations. These contain source values, never renderer
// objects, view-dependent selections, formatting, or pointers into live state.
struct Session
{
    struct Player { std::string name; int teamNumber = 0; bool competing = false; int type = 0; };
    std::vector<Player> players;
    bool fixedAlliances = false;
    std::shared_ptr<const std::string> missionBriefing;
    std::shared_ptr<const GameObjectives> objectives;
    std::shared_ptr<const GameHints> hints;
    bool editor = false, totalPrestigeReached = false, prestigeWinCondition = false;
    int prestigeToReach = 0;
    bool anyPlayerWaited = false;
    Uint32 maskAwayPlayer = 0;
    Uint64 executedOrderRevision = 0;
    Uint32 terrainSeed = 0;
    Uint16 fertilityMaximum = 0;
    bool legacyScriptTextShown = false;
    std::string legacyScriptText;
    int legacyScriptTimer = 0;
};
struct BulletRecord { Sint32 px, py, speedX, speedY, ticksLeft, ticksInitial; };
struct ExplosionRecord { int x, y, ticksLeft; };
struct DeathRecord { int x, y, ticksLeft, team; };
struct SectorEffects
{
    std::vector<BulletRecord> bullets;
    std::vector<ExplosionRecord> explosions;
    std::vector<DeathRecord> deaths;
};
struct Effects { std::vector<SectorEffects> sectors; };
struct Statistics { std::vector<std::shared_ptr<const TeamStats>> teams; };
struct Telemetry
{
    struct Row
    {
        int team = 0, player = 0;
        std::string name;
        bool available = false;
        std::vector<AITelemetry::Field> fields;
        std::vector<AITelemetry::Value> values;
        std::vector<AITelemetry::NamedValue> named;
    };
    std::vector<Row> rows;
};
struct History { std::vector<std::shared_ptr<const TeamStats>> teams; };
struct EntityDiagnostics
{
    static constexpr unsigned FailReasons = 8;
    struct Building
    {
        BuildingRef identity;
        int verbose = 0;
        bool recordFailingUnits = false;
        std::array<Uint32, FailReasons> failingCounts{};
        std::array<std::vector<Uint16>, FailReasons> failingUnits;
        std::vector<Uint16> gradient;
    };
    std::vector<Building> buildings;
};
}
