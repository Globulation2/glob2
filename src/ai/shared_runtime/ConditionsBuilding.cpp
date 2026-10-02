// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Building.h"
#include "Game.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Conditions;


ParticularBuilding::ParticularBuilding(BuildingCondition* condition, int id) : condition(condition), id(id)
{

}



ParticularBuilding::~ParticularBuilding()
{
	delete condition;
}



tribool ParticularBuilding::passes(Runtime& runtime)
{
	if(!runtime.get_building_register().is_building_found(id) && !runtime.get_building_register().is_building_pending(id))
	{
		return indeterminate;
	}
	if(runtime.get_building_register().is_building_found(id))
	{
		bool passes=condition->passes(runtime, id);
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



tribool BuildingDestroyed::passes(Runtime& runtime)
{
	if(!runtime.get_building_register().is_building_found(id) && !runtime.get_building_register().is_building_pending(id))
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



EnemyBuildingDestroyed::EnemyBuildingDestroyed(Runtime& runtime, int gbid) : gbid(gbid)
{
	Building* b=runtime.player->game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
	type=b->type->shortTypeNum;
	level=b->type->level;
	location=position(b->posX, b->posY);
}



tribool EnemyBuildingDestroyed::passes(Runtime& runtime)
{
	Building* b=runtime.player->game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
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
	if (gbid >= Building::MAX_COUNT * Team::MAX_COUNT || !player->game->teams[Building::GIDtoTeam(gbid)]) return false;
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


SpecificBuildingType::SpecificBuildingType(int building_type) : building_type(building_type)
{

}



bool SpecificBuildingType::passes(Runtime& runtime, int id)
{
	if(runtime.get_building_register().get_type(id)==building_type)
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



bool NotSpecificBuildingType::passes(Runtime& runtime, int id)
{
	if(runtime.get_building_register().get_type(id)!=building_type)
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





BeingUpgradedTo::BeingUpgradedTo(int level) : level(level)
{

}



bool BeingUpgradedTo::passes(Runtime& runtime, int id)
{
	Building* b= runtime.get_building_register().get_building(id);
	if(!runtime.get_building_register().is_building_upgrading(id))
		return false;
	if(b->type->isBuildingSite)
	{
		if(b->type->level==(level-AI_SHARED_RUNTIME_LEVEL_OFFSET_USER_TO_ENGINE))
		{
			return true;
		}
	}
	else if(b->type->level==(level-AI_SHARED_RUNTIME_LEVEL_OFFSET_FINISHED_TO_TARGET))
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



bool BuildingLevel::passes(Runtime& runtime, int id)
{
	Building* building = runtime.get_building_register().get_building(id);
	if(building->type->level==building_level-AI_SHARED_RUNTIME_LEVEL_OFFSET_USER_TO_ENGINE)
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
