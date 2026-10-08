// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
namespace SimulationSnapshot
{
enum class Component : Uint32
{
 Catalogs = 1, Terrain = 2, Resources = 4, Occupancy = 8,
 Areas = 16, Visibility = 32, Entities = 64, Teams = 128, Rules = 256,
 ResourceFields = 512, Growth = 1024, Session = 2048, Effects = 4096,
 Statistics = 8192, EntityDiagnostics = 16384, Annotations = 32768, History = 65536, Telemetry = 131072
};
using Requirements = Uint32;
constexpr Requirements bit(Component value) { return static_cast<Requirements>(value); }
constexpr Requirements Simulation = 2047;
constexpr Requirements All = 262143;
constexpr bool needs(Requirements mask, Component value) { return (mask & bit(value)) != 0; }
}
