// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by BuildingOrder.o that
// RuntimeBuildingOrderSaveLoadTest does not exercise. Only save() and load() are
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
#include "shared_runtime/Runtime.h"
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

int AISharedRuntime::Construction::FlagMap::get_flag(int, int)
{
	return 0;
}

void AISharedRuntime::Gradients::GradientManager::queue_gradient(const AISharedRuntime::Gradients::GradientInfo &)
{
}

bool AISharedRuntime::Gradients::GradientManager::is_updated(const AISharedRuntime::Gradients::GradientInfo &)
{
	return false;
}

AISharedRuntime::Conditions::Condition *AISharedRuntime::Conditions::Condition::load_condition(
	GAGCore::InputStream *, Player *, Sint32)
{
	return NULL;
}

void AISharedRuntime::Conditions::Condition::save_condition(
	AISharedRuntime::Conditions::Condition *, GAGCore::OutputStream *)
{
}

AISharedRuntime::Construction::Constraint *AISharedRuntime::Construction::Constraint::load_constraint(
	GAGCore::InputStream *, Player *, Sint32)
{
	return NULL;
}

void AISharedRuntime::Construction::Constraint::save_constraint(
	AISharedRuntime::Construction::Constraint *, GAGCore::OutputStream *)
{
}
