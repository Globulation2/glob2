// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for symbols referenced by the shared AI runtime's BuildingOrder.o that the
// save/load unit test does not exercise: find_location(), passes_conditions() and
// queue_gradients() live in the same translation unit, so the linker still has to
// resolve the building type tables, FlagMap, GradientManager and the Constraint /
// Condition factories. The factories are reached only per element of the constraint
// and condition vectors, and every fixture uses an order with both lists empty.

#include <string>
#include "shared_runtime/Runtime.h"
#include "BuildingType.h"
#include "IntBuildingType.h"

BuildingType *BuildingsTypes::getByType(const std::string &, int, bool)
{
	return NULL;
}

const std::string &IntBuildingType::typeFromShortNumber(int)
{
	static const std::string none;
	return none;
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
