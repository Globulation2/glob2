// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
class Game;
namespace SimulationSnapshot { struct Handle; }
namespace HiringStudy
{
// Shadow-only producer. It never reserves or assigns a live entity.
bool enabled();
void configure(bool shadow);
struct Schedule
{
    unsigned delay=4, refresh=4, capacity=128;
    bool split=true, parallel=true, score=false;
    int extras=2; bool availableOnly=false, coordinate=false;
};
void configureScheduled(Schedule);
void advance(const Game&);
void drain(const Game&);
std::uint32_t requirements(const Game&,bool paused);
void observe(const Game&, const SimulationSnapshot::Handle&, bool paused);
void clear(const Game&);
void write(std::ostream&,bool finish=true);
}
