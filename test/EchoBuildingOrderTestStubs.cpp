// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by BuildingOrder.o that
// EchoBuildingOrderSaveLoadTest does not exercise. Only save() and load() are
// under test; find_location(), passes_conditions() and queue_gradients() live in
// the same translation unit, so the linker still has to resolve what they call --
// globalContainer and the building type tables, Map's placement predicate,
// FlagMap, GradientManager, and the Constraint / Condition factories. Pulling in
// the real definitions would drag in most of the game.
//
// None of these are called at runtime by the test. The Constraint / Condition
// factories are the closest call: BuildingOrder::load() and save() do reach them,
// but only once per element of the constraint and condition vectors, and every
// fixture in the test uses an order with both lists empty.

#include <string>
#include "echo/Echo.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Map.h"

GlobalContainer *globalContainer = NULL;

BuildingType *BuildingsTypes::getByType(const std::string &, int, bool)
{
	return NULL;
}

const std::string &IntBuildingType::typeFromShortNumber(int)
{
	static const std::string none;
	return none;
}

bool Map::isHardSpaceForBuilding(int, int, int, int) const
{
	return false;
}

int AIEcho::Construction::FlagMap::get_flag(int, int)
{
	return 0;
}

void AIEcho::Gradients::GradientManager::queue_gradient(const AIEcho::Gradients::GradientInfo &)
{
}

bool AIEcho::Gradients::GradientManager::is_updated(const AIEcho::Gradients::GradientInfo &)
{
	return false;
}

AIEcho::Conditions::Condition *AIEcho::Conditions::Condition::load_condition(
	GAGCore::InputStream *, Player *, Sint32)
{
	return NULL;
}

void AIEcho::Conditions::Condition::save_condition(
	AIEcho::Conditions::Condition *, GAGCore::OutputStream *)
{
}

AIEcho::Construction::Constraint *AIEcho::Construction::Constraint::load_constraint(
	GAGCore::InputStream *, Player *, Sint32)
{
	return NULL;
}

void AIEcho::Construction::Constraint::save_constraint(
	AIEcho::Construction::Constraint *, GAGCore::OutputStream *)
{
}
