// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "FileFormatVersions.h"
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
	type=b->typeNum;
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
	if (b->typeNum != type)
	{
		// A completed upgrade or repair remains the same target. Resolve only
		// when its concrete variant changes, following explicit transitions.
		const auto& catalog = runtime.player->game->buildingsTypes;
		std::vector<int> pending{type};
		std::vector<bool> seen(catalog.size(), false);
		bool related = false;
		while (!pending.empty())
		{
			const int current = pending.back(); pending.pop_back();
			if (current < 0 || size_t(current) >= catalog.size() || seen[current]) continue;
			seen[current] = true;
			if (current == b->typeNum) { related = true; break; }
			const auto* variant = catalog.get(current);
			pending.push_back(variant->nextLevel);
			pending.push_back(variant->prevLevel);
		}
		if (!related) return true;
		type = b->typeNum;
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
 if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) type=importLegacyBuildingId(*player->game,type,level,false);
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


ProvidesBuildingCapability::ProvidesBuildingCapability(int building_type) : building_type(building_type)
{

}



bool ProvidesBuildingCapability::passes(Runtime& runtime, int id)
{
	if(runtime.get_building_register().provides(id,building_type))
		return true;
	return false;
}

bool ProvidesBuildingCapability::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("SpecificBuildingType");
	building_type=stream->readUint32("building_type");
 if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) building_type=importLegacyBuildingDemand(building_type);
 if(building_type<0 || building_type>BuildingDemand::Count) return false;
	stream->readLeaveSection();
	return true;
}



void ProvidesBuildingCapability::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("SpecificBuildingType");
	stream->writeUint32(building_type, "building_type");
	stream->writeLeaveSection();
}





LacksBuildingCapability::LacksBuildingCapability(int building_type) : building_type(building_type)
{

}



bool LacksBuildingCapability::passes(Runtime& runtime, int id)
{
	if(!runtime.get_building_register().provides(id,building_type))
		return true;
	return false;
}

bool LacksBuildingCapability::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("NotSpecificBuildingType");
	building_type=stream->readUint32("building_type");
 if(versionMinor<FILE_FORMAT_VERSION_BUILDING_CATALOG) building_type=importLegacyBuildingDemand(building_type);
 if(building_type<0 || building_type>BuildingDemand::Count) return false;
	stream->readLeaveSection();
	return true;
}



void LacksBuildingCapability::save(GAGCore::OutputStream *stream)
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
    const int target=b->type->isBuildingSite ? b->typeNum : b->type->nextLevel;
    return target>=0 && runtime.player->game->buildingCapabilities().lineagePosition(target)==level;
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
	if(runtime.player->game->buildingCapabilities().lineagePosition(building->typeNum)==building_level)
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

tribool AttractionRetiredOrDestroyed::passes(Runtime& runtime)
{
    return runtime.attraction_retired_or_destroyed(id,unitMask);
}

bool AttractionRetiredOrDestroyed::load(GAGCore::InputStream* stream,Player*,Sint32)
{
    stream->readEnterSection("AttractionRetiredOrDestroyed");
    id=stream->readSint32("id");unitMask=stream->readUint8("unitMask");
    stream->readLeaveSection();
    return id>=0 && unitMask && !(unitMask&~((1u<<NB_UNIT_TYPE)-1));
}

void AttractionRetiredOrDestroyed::save(GAGCore::OutputStream* stream)
{
    stream->writeEnterSection("AttractionRetiredOrDestroyed");
    stream->writeSint32(id,"id");stream->writeUint8(unitMask,"unitMask");
    stream->writeLeaveSection();
}
