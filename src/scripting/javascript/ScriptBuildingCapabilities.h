// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCapabilities.h"
#include "Game.h"
#include "ScriptValue.h"
#include <stdexcept>

namespace Script
{
inline const auto& buildingCapabilityNames()
{
 using I = AIPlanning::BuildingIntent;
 static const std::pair<const char*, I> names[] = {
  {"produceWorker", I::ProduceWorker}, {"produceExplorer", I::ProduceExplorer}, {"produceWarrior", I::ProduceWarrior},
  {"feed", I::Feed}, {"heal", I::Heal}, {"trainWalk", I::TrainWalk}, {"trainSwim", I::TrainSwim},
  {"trainFly", I::TrainFly}, {"trainBuild", I::TrainBuild}, {"trainHarvest", I::TrainHarvest},
  {"trainAttackSpeed", I::TrainAttackSpeed}, {"trainAttackStrength", I::TrainAttackStrength},
  {"trainAirAttack", I::TrainAirAttack}, {"trainBombing", I::TrainBombing},
  {"trainCreateWood", I::TrainCreateWood}, {"trainCreateWheat", I::TrainCreateWheat}, {"trainCreateAlgae", I::TrainCreateAlgae},
  {"trainArmor", I::TrainArmor}, {"trainHealth", I::TrainHealth}, {"trainConstruction", I::TrainConstruction},
  {"projectileDefense", I::ProjectileDefense}, {"attractWorkers", I::AttractWorkers},
  {"attractExplorers", I::AttractExplorers}, {"attractWarriors", I::AttractWarriors},
  {"clearResources", I::ClearResources}, {"exchangeResources", I::ExchangeResources}
 };
 return names;
}
inline AIPlanning::BuildingIntent buildingCapability(const std::string& name)
{
 for (const auto& [key, intent] : buildingCapabilityNames()) if (name == key) return intent;
 throw std::runtime_error("Unknown building capability: " + name);
}
inline bool buildingProvides(const Game& game, int type, AIPlanning::BuildingIntent intent)
{
 const auto* descriptor = game.buildingsTypes.get(type);
 if (descriptor->isBuildingSite) type = descriptor->nextLevel;
 return type >= 0 && game.buildingCapabilities().available(type, intent, game.gameHeader);
}
inline Value buildingCapabilities(const Game& game, int type)
{
 Value out = Value::array();
 for (const auto& [key, intent] : buildingCapabilityNames())
  if (buildingProvides(game, type, intent)) out.items.emplace_back(key);
 return out;
}
inline bool buildingVariantDescendsFrom(const BuildingsTypes& catalog, int origin, int actual)
{
 for (std::size_t n = 0; origin >= 0 && n < catalog.size(); ++n)
 {
  if (origin == actual) return true;
  origin = catalog.get(origin)->nextLevel;
 }
 return false;
}
}
