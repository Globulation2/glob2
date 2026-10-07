// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include <memory>
#include "Building.h"
#include "shared_runtime/BuildingDemands.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Conditions;

// Helper for the load_condition switches: each case constructs a new T,
// calls its load(), and breaks. T's protected/private members are accessible
// because this macro expands inside Condition::load_condition (or
// BuildingCondition::load_condition), which is a friend of every derived class.
#define LOAD_CASE(EnumVal, Type) \
	case EnumVal: \
		condition.reset(new Type); \
		if (!condition->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object"); \
		break;


Condition* Condition::load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	GAGCore::InputStream::NestedRead nesting(*stream);
	stream->readEnterSection("Condition");
	const Uint32 type=stream->readUint32("type");
	std::unique_ptr<Condition> condition;
	switch(type)
	{
		LOAD_CASE(CParticularBuilding,     ParticularBuilding)
		LOAD_CASE(CBuildingDestroyed,      BuildingDestroyed)
		LOAD_CASE(CEnemyBuildingDestroyed, EnemyBuildingDestroyed)
		LOAD_CASE(CEitherCondition,        EitherCondition)
		LOAD_CASE(CPopulation,             Population)
        LOAD_CASE(CAttractionRetiredOrDestroyed, AttractionRetiredOrDestroyed)
	}
	stream->readLeaveSection();
	if (!condition) throw std::runtime_error("Unknown saved AI object type");
	return condition.release();
}



void Condition::save_condition(Condition* condition, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Condition");
	stream->writeUint32(condition->get_type(), "type");
	condition->save(stream);
	stream->writeLeaveSection();
}



BuildingCondition* BuildingCondition::load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	GAGCore::InputStream::NestedRead nesting(*stream);
	stream->readEnterSection("BuildingCondition");
	const Uint32 type=stream->readUint32("type");
	std::unique_ptr<BuildingCondition> condition;
	switch(type)
	{
		LOAD_CASE(CNotUnderConstruction,    NotUnderConstruction)
		LOAD_CASE(CUnderConstruction,       UnderConstruction)
		LOAD_CASE(CBeingUpgraded,           BeingUpgraded)
		LOAD_CASE(CBeingUpgradedTo,         BeingUpgradedTo)
		LOAD_CASE(CSpecificBuildingType,    ProvidesBuildingCapability)
		LOAD_CASE(CNotSpecificBuildingType, LacksBuildingCapability)
		LOAD_CASE(CBuildingLevel,           BuildingLevel)
		LOAD_CASE(CUpgradable,              Upgradable)
		LOAD_CASE(CMaterialTrackerAmount,  MaterialTrackerAmount)
		LOAD_CASE(CMaterialTrackerAge,     MaterialTrackerAge)
	}
	stream->readLeaveSection();
	if (!condition) throw std::runtime_error("Unknown saved AI object type");
	return condition.release();
}

#undef LOAD_CASE



void BuildingCondition::save_condition(BuildingCondition* condition, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingCondition");
	stream->writeUint32(condition->get_type(), "type");
	condition->save(stream);
	stream->writeLeaveSection();
}



bool NotUnderConstruction::passes(Runtime& runtime, int id)
{
	const AIEngine::BuildingView* building = runtime.get_building_register().get_building(id);
	bool result=building->constructionResultState==::Building::NO_CONSTRUCTION && !runtime.get_building_register().is_building_upgrading(id);
	return result;
}



bool UnderConstruction::passes(Runtime& runtime, int id)
{
	const AIEngine::BuildingView* building = runtime.get_building_register().get_building(id);
	return building->constructionResultState!=::Building::NO_CONSTRUCTION && building->buildingState==Building::ALIVE;
}



bool BeingUpgraded::passes(Runtime& runtime, int id)
{
	return runtime.get_building_register().is_building_upgrading(id);
}




bool Upgradable::passes(Runtime& runtime, int id)
{
	const AIEngine::BuildingView* building = runtime.get_building_register().get_building(id);
 if(building && !runtime.get_building_register().is_building_upgrading(id)
    && !runtime.observation().configuration->isUnitUpgradesDisabled()
    && building->constructionResultState==Building::NO_CONSTRUCTION
    && runtime.observation().isUpgradeAvailable(*building)
    && runtime.observation().isHardSpaceForBuildingSite(*building, true)
    && building->hp==building->maxHp)
		return true;
	return false;
}
