// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
namespace SimulationSnapshot
{
// Every boundary contains the complete simulation view. Admitted presentation
// and diagnostics add their payloads without changing that simulation view.
struct CaptureOptions
{
 bool presentation = false;
 bool history = false;
 bool telemetry = false;
 bool entityDiagnostics = false;
 bool annotations = false;
 void include(CaptureOptions other)
 {
  presentation |= other.presentation;
  history |= other.history;
  telemetry |= other.telemetry;
  entityDiagnostics |= other.entityDiagnostics;
  annotations |= other.annotations;
 }
};

// Internal component flags for incremental capture and narrow buffer leases.
// Consumers select CaptureOptions and named Handle views instead of assembling masks.
enum class Component : Uint32
{
 Catalogs = 1, Terrain = 2, Resources = 4, Occupancy = 8,
 Areas = 16, Visibility = 32, Entities = 64, Teams = 128, Rules = 256,
 ResourceFields = 512, Growth = 1024, Session = 2048, Effects = 4096,
 Statistics = 8192, EntityDiagnostics = 16384, Annotations = 32768, History = 65536, Telemetry = 131072, StatisticsInputs = 262144
};
using Requirements = Uint32;
constexpr Requirements bit(Component value) { return static_cast<Requirements>(value); }
constexpr Requirements Simulation = bit(Component::Catalogs) | bit(Component::Terrain)
 | bit(Component::Resources) | bit(Component::Occupancy) | bit(Component::Areas)
 | bit(Component::Visibility) | bit(Component::Entities) | bit(Component::Teams)
 | bit(Component::Rules) | bit(Component::ResourceFields) | bit(Component::Growth) | bit(Component::StatisticsInputs);
constexpr Requirements All = Simulation | bit(Component::Session) | bit(Component::Effects)
 | bit(Component::Statistics) | bit(Component::EntityDiagnostics) | bit(Component::Annotations)
 | bit(Component::History) | bit(Component::Telemetry);
constexpr bool needs(Requirements mask, Component value) { return (mask & bit(value)) != 0; }
constexpr Requirements captureRequirements(CaptureOptions options)
{
 auto result = Simulation;
 if (options.presentation) result |= bit(Component::Session) | bit(Component::Effects) | bit(Component::Statistics);
 if (options.history) result |= bit(Component::History);
 if (options.telemetry) result |= bit(Component::Telemetry);
 if (options.entityDiagnostics) result |= bit(Component::EntityDiagnostics);
 if (options.annotations) result |= bit(Component::Annotations);
 return result;
}
}
