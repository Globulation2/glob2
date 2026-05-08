// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Game.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;

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



ParticularBuilding::ParticularBuilding(BuildingCondition* condition, int id) : condition(condition), id(id)
{

}



ParticularBuilding::~ParticularBuilding()
{
	delete condition;
}



boost::logic::tribool ParticularBuilding::passes(Echo& echo)
{
	if(!echo.get_building_register().is_building_found(id) && !echo.get_building_register().is_building_pending(id))
	{
		return indeterminate;
	}
	if(echo.get_building_register().is_building_found(id))
	{
		bool passes=condition->passes(echo, id);
		return passes;
	}
	return false;
}



ConditionType ParticularBuilding::get_type()
{
	return CParticularBuilding;
}



bool ParticularBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ParticularBuilding");
	id=stream->readSint32("id");
	condition=BuildingCondition::load_condition(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void ParticularBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ParticularBuilding");
	stream->writeSint32(id, "id");
	BuildingCondition::save_condition(condition, stream);
	stream->writeLeaveSection();
}


BuildingDestroyed::BuildingDestroyed(int id) : id(id)
{

}



boost::logic::tribool BuildingDestroyed::passes(Echo& echo)
{
	if(!echo.get_building_register().is_building_found(id) && !echo.get_building_register().is_building_pending(id))
	{
		return true;
	}
	return false;
}



ConditionType BuildingDestroyed::get_type()
{
	return CBuildingDestroyed;
}



bool BuildingDestroyed::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingDestroyed");
	id=stream->readSint32("id");
	stream->readLeaveSection();
	return true;
}



void BuildingDestroyed::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingDestroyed");
	stream->writeSint32(id, "id");
	stream->writeLeaveSection();
}



EnemyBuildingDestroyed::EnemyBuildingDestroyed(Echo& echo, int gbid) : gbid(gbid)
{
	Building* b=echo.player->game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
	type=b->type->shortTypeNum;
	level=b->type->level;
	location=position(b->posX, b->posY);
}



boost::logic::tribool EnemyBuildingDestroyed::passes(Echo& echo)
{
	Building* b=echo.player->game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
	if(b==NULL)
	{
		return true;
	}
	if(b->posX != location.x || b->posY != location.y)
	{
		return true;
	}
	if(b->type->shortTypeNum != type)
	{
		return true;
	}
	return false;
}



bool EnemyBuildingDestroyed::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("EnemyBuildingDestroyed");
	gbid=stream->readUint32("gbid");
	type=stream->readUint32("type");
	level=stream->readUint32("level");
	int posx=stream->readUint32("posx");
	int posy=stream->readUint32("posy");
	location=position(posx, posy);
	stream->readLeaveSection();
	return true;
}



void EnemyBuildingDestroyed::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("EnemyBuildingDestroyed");
	stream->writeUint32(gbid, "gbid");
	stream->writeUint32(type, "type");
	stream->writeUint32(level, "level");
	stream->writeUint32(location.x, "posx");
	stream->writeUint32(location.y, "posy");
	stream->writeLeaveSection();
}


EitherCondition::EitherCondition(Condition* condition1, Condition* condition2) : condition1(condition1), condition2(condition2)
{

}



EitherCondition::~EitherCondition()
{
	delete condition1;
	delete condition2;
}



boost::logic::tribool EitherCondition::passes(Echo& echo)
{
	tribool p1=condition1->passes(echo);
	tribool p2=condition2->passes(echo);
	if(p1 || p2)
		return true;
	else if(!p1 || !p2)
		return false;
	else
		return indeterminate;
}



ConditionType EitherCondition::get_type()
{
	return CEitherCondition;
}



bool EitherCondition::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("EitherCondition");
	condition1=Condition::load_condition(stream, player, versionMinor);
	condition2=Condition::load_condition(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void EitherCondition::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("EitherCondition");
	Condition::save_condition(condition1, stream);
	Condition::save_condition(condition2, stream);
	stream->writeLeaveSection();
}



Population::Population(bool workers, bool explorers, bool warriors, int num, PopulationMethod method) : workers(workers), explorers(explorers), warriors(warriors), num(num), method(method)
{

}



boost::logic::tribool Population::passes(Echo& echo)
{
	int amount=0;
	if(workers)
		amount+=echo.player->team->stats.getLatestStat()->numberUnitPerType[WORKER];
	if(explorers)
		amount+=echo.player->team->stats.getLatestStat()->numberUnitPerType[EXPLORER];
	if(warriors)
		amount+=echo.player->team->stats.getLatestStat()->numberUnitPerType[WARRIOR];
	if(method==Greater)
	{
		return (amount >= num);
	}
	else if(method==Lesser)
	{
		return (amount <= num);
	}
	return false;
}



ConditionType Population::get_type()
{
	return CPopulation;
}



bool Population::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Population");
	workers=stream->readUint8("workers");
	explorers=stream->readUint8("explorers");
	warriors=stream->readUint8("warriors");
	num=stream->readSint32("num");
	method=static_cast<PopulationMethod>(stream->readUint32("method"));
	stream->readLeaveSection();
	return true;
}



void Population::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Population");
	stream->writeUint8(workers, "workers");
	stream->writeUint8(explorers, "explorers");
	stream->writeUint8(warriors, "warriors");
	stream->writeSint32(num, "num");
	stream->writeUint32(static_cast<Uint32>(method), "method");
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



SpecificBuildingType::SpecificBuildingType(int building_type) : building_type(building_type)
{
	
}



bool SpecificBuildingType::passes(Echo& echo, int id)
{
	if(echo.get_building_register().get_type(id)==building_type)
		return true;
	return false;
}

bool SpecificBuildingType::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("SpecificBuildingType");
	building_type=stream->readUint32("building_type");
	stream->readLeaveSection();
	return true;
}



void SpecificBuildingType::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("SpecificBuildingType");
	stream->writeUint32(building_type, "building_type");
	stream->writeLeaveSection();
}





NotSpecificBuildingType::NotSpecificBuildingType(int building_type) : building_type(building_type)
{

}



bool NotSpecificBuildingType::passes(Echo& echo, int id)
{
	if(echo.get_building_register().get_type(id)!=building_type)
		return true;
	return false;
}

bool NotSpecificBuildingType::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("NotSpecificBuildingType");
	building_type=stream->readUint32("building_type");
	stream->readLeaveSection();
	return true;
}



void NotSpecificBuildingType::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("NotSpecificBuildingType");
	stream->writeUint32(building_type, "building_type");
	stream->writeLeaveSection();
}





bool BeingUpgraded::passes(Echo& echo, int id)
{
	return echo.get_building_register().is_building_upgrading(id);
}




BeingUpgradedTo::BeingUpgradedTo(int level) : level(level)
{

}



bool BeingUpgradedTo::passes(Echo& echo, int id)
{
	Building* b= echo.get_building_register().get_building(id);
	if(!echo.get_building_register().is_building_upgrading(id))
		return false;
	if(b->type->isBuildingSite)
	{
		if(b->type->level==(level-AI_ECHO_LEVEL_OFFSET_USER_TO_ENGINE))
		{
			return true;
		}
	}
	else if(b->type->level==(level-AI_ECHO_LEVEL_OFFSET_FINISHED_TO_TARGET))
	{
		return true;
	}
	return false;
}


bool BeingUpgradedTo::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BeingUpgradedTo");
	level=stream->readUint32("level");
	stream->readLeaveSection();
	return true;
}



void BeingUpgradedTo::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BeingUpgradedTo");
	stream->writeUint32(level, "level");
	stream->writeLeaveSection();
}




BuildingLevel::BuildingLevel(int building_level) : building_level(building_level)
{

}



bool BuildingLevel::passes(Echo& echo, int id)
{
	Building* building = echo.get_building_register().get_building(id);
	if(building->type->level==building_level-AI_ECHO_LEVEL_OFFSET_USER_TO_ENGINE)
		return true;
	return false;
}


bool BuildingLevel::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingLevel");
	building_level=stream->readUint32("building_level");
	stream->readLeaveSection();
	return true;
}



void BuildingLevel::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingLevel");
	stream->writeUint32(building_level, "building_level");
	stream->writeLeaveSection();
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



RessourceTrackerAmount::RessourceTrackerAmount(int amount, TrackerMethod tracker_method) : amount(amount), tracker_method(tracker_method)
{

}



bool RessourceTrackerAmount::passes(Echo& echo, int id)
{
	if(tracker_method==Greater)
	{
		return echo.get_ressource_tracker(id)->get_total_level() > amount;
	}
	else if(tracker_method==Lesser)
	{
		return echo.get_ressource_tracker(id)->get_total_level() < amount;
	}
	return false;
}



BuildingConditionType RessourceTrackerAmount::get_type()
{
	return CRessourceTrackerAmount;
}



bool RessourceTrackerAmount::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTrackerAmount");
	amount=stream->readUint32("amount");
	tracker_method=static_cast<TrackerMethod>(stream->readUint32("tracker_method"));
	stream->readLeaveSection();
	return true;
}



void RessourceTrackerAmount::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTrackerAmount");
	stream->writeUint32(amount, "amount");
	stream->writeUint32(static_cast<Uint32>(tracker_method), "tracker_method");
	stream->writeLeaveSection();
}



RessourceTrackerAge::RessourceTrackerAge(int age, TrackerMethod tracker_method) : age(age), tracker_method(tracker_method)
{

}



bool RessourceTrackerAge::passes(Echo& echo, int id)
{
	if(tracker_method==Greater)
	{
		return echo.get_ressource_tracker(id)->get_age() > age;
	}
	else if(tracker_method==Lesser)
	{
		return echo.get_ressource_tracker(id)->get_age() < age;
	}
	return false;
}



BuildingConditionType RessourceTrackerAge::get_type()
{
	return CRessourceTrackerAge;
}



bool RessourceTrackerAge::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTrackerAge");
	age=stream->readUint32("age");
	tracker_method=static_cast<TrackerMethod>(stream->readUint32("tracker_method"));
	stream->readLeaveSection();
	return true;
}



void RessourceTrackerAge::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTrackerAge");
	stream->writeUint32(age, "age");
	stream->writeUint32(static_cast<Uint32>(tracker_method), "tracker_method");
	stream->writeLeaveSection();
}


