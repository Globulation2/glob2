// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <string>

namespace SimulationSnapshot { struct Handle; }
namespace GameDiagnostics
{
// Offline observation only: never consulted by an AI or serialized into saves.
// Intervals use the state at their left endpoint. A continuation starts a new,
// left-censored trace; generations prevent recycled slots joining old journeys.
class WarriorTacticsTrace
{
public:
    explicit WarriorTacticsTrace(const std::string& directory);
    ~WarriorTacticsTrace();
    void capture(const SimulationSnapshot::Handle& world);
    void finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
