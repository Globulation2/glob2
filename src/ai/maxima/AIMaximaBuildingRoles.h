// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace AIMaximaBuildings
{
// Private strategic demands. Values preserve saved AI filter meanings; they
// are never building catalog IDs, exclusive categories, or engine behavior.
enum PolicyRole { Production, Feeding, Healing, WalkTraining, SwimTraining,
 CombatTraining, ConstructionTraining, ProjectileDefense, ExploreAttraction,
 WarriorAttraction, WorkerAttraction, ReservedWall, ResourceExchange, RoleCount };
constexpr unsigned roleBit(int role) { return role>=0 && role<RoleCount ? 1u<<role : 0; }
}
