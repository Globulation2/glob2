// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include "IntBuildingType.h"

using namespace AIEcho;
using namespace AIEcho::Conditions;

// Helper for the load_condition switches: each case constructs a new T,
// calls its load(), and breaks. T's protected/private members are accessible
// because this macro expands inside Condition::load_condition (or
// BuildingCondition::load_condition), which is a friend of every derived class.
#define LOAD_CASE(EnumVal, Type) \
	case EnumVal: \
		condition = new Type; \
		condition->load(stream, player, versionMinor); \
		break;


Condition* Condition::load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Condition");
	ConditionType type=static_cast<ConditionType>(stream->readUint32("type"));
	Condition* condition=NULL;
	switch(type)
	{
		LOAD_CASE(CParticularBuilding,     ParticularBuilding)
		LOAD_CASE(CBuildingDestroyed,      BuildingDestroyed)
		LOAD_CASE(CEnemyBuildingDestroyed, EnemyBuildingDestroyed)
		LOAD_CASE(CEitherCondition,        EitherCondition)
		LOAD_CASE(CPopulation,             Population)
	}
	stream->readLeaveSection();
	return condition;
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
	stream->readEnterSection("BuildingCondition");
	BuildingConditionType type=static_cast<BuildingConditionType>(stream->readUint32("type"));
	BuildingCondition* condition=NULL;
	switch(type)
	{
		LOAD_CASE(CNotUnderConstruction,    NotUnderConstruction)
		LOAD_CASE(CUnderConstruction,       UnderConstruction)
		LOAD_CASE(CBeingUpgraded,           BeingUpgraded)
		LOAD_CASE(CBeingUpgradedTo,         BeingUpgradedTo)
		LOAD_CASE(CSpecificBuildingType,    SpecificBuildingType)
		LOAD_CASE(CNotSpecificBuildingType, NotSpecificBuildingType)
		LOAD_CASE(CBuildingLevel,           BuildingLevel)
		LOAD_CASE(CUpgradable,              Upgradable)
		LOAD_CASE(CRessourceTrackerAmount,  RessourceTrackerAmount)
		LOAD_CASE(CRessourceTrackerAge,     RessourceTrackerAge)
	}
	stream->readLeaveSection();
	return condition;
}

#undef LOAD_CASE



void BuildingCondition::save_condition(BuildingCondition* condition, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingCondition");
	stream->writeUint32(condition->get_type(), "type");
	condition->save(stream);
	stream->writeLeaveSection();
}



bool NotUnderConstruction::passes(Echo& echo, int id)
{
	Building* building = echo.get_building_register().get_building(id);
	bool result=building->constructionResultState==::Building::NO_CONSTRUCTION && !echo.get_building_register().is_building_upgrading(id);
	return result;
}



bool UnderConstruction::passes(Echo& echo, int id)
{
	Building* building = echo.get_building_register().get_building(id);
	return building->constructionResultState!=::Building::NO_CONSTRUCTION && building->buildingState==Building::ALIVE;
}



bool BeingUpgraded::passes(Echo& echo, int id)
{
	return echo.get_building_register().is_building_upgrading(id);
}




bool Upgradable::passes(Echo& echo, int id)
{
	Building* building = echo.get_building_register().get_building(id);
	if((building->type->shortTypeNum==IntBuildingType::FOOD_BUILDING ||
	    building->type->shortTypeNum==IntBuildingType::HEAL_BUILDING ||
	    building->type->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING ||
	    building->type->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING ||
	    building->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING ||
	    building->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING ||
	    building->type->shortTypeNum==IntBuildingType::DEFENSE_BUILDING) &&
	   building->constructionResultState==Building::NO_CONSTRUCTION &&
	   building->type->level!=AI_ECHO_MAX_BUILDING_LEVEL_INDEX &&
	   building->isHardSpaceForBuildingSite(Building::UPGRADE) &&
	   building->hp == building->type->hpMax
	    )
		return true;
	return false;
}
