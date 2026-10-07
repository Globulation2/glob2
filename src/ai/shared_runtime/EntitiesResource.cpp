// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "ai/observation/AIWorldView.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;


Entities::MaterialSource::MaterialSource(int material) : material(material)
{
}

bool Entities::MaterialSource::is_entity(Map* map, int posx, int posy)
{
	return map->isMaterialTakeableSlot(posx,posy,material);
}

bool Entities::MaterialSource::operator==(const Entity& rhs) const
{
	if(typeid(rhs)!=typeid(Entities::MaterialSource))
		return false;
	return static_cast<const Entities::MaterialSource&>(rhs).material==material;
}

bool Entities::MaterialSource::can_change()
{
	return true;
}

bool Entities::MaterialSource::can_change(const ResourceRegistry& registry)
{
    return material>=0 && material<int(MaterialCount) &&
        (registry.mutableMaterialSources() & (MaterialMask(1)<<material))!=0;
}

Entities::EntityType Entities::MaterialSource::get_type()
{
	return Entities::EMaterialSource;
}

bool Entities::MaterialSource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Ressource");
	material = stream->readSint32("ressource_type");
	stream->readLeaveSection();
	return true;
}

void Entities::MaterialSource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Ressource");
	stream->writeSint32(material, "ressource_type");
	stream->writeLeaveSection();
}


Entities::AnyResource::AnyResource()
{
}

bool Entities::AnyResource::is_entity(Map* map, int posx, int posy)
{
	return map->isResource(posx, posy);
}

bool Entities::AnyResource::operator==(const Entity& rhs) const
{
	return typeid(rhs)==typeid(Entities::AnyResource);
}

bool Entities::AnyResource::can_change()
{
	return true;
}

Entities::EntityType Entities::AnyResource::get_type()
{
	return Entities::EAnyResource;
}

bool Entities::AnyResource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyRessource");
	stream->readLeaveSection();
	return true;
}

void Entities::AnyResource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyRessource");
	stream->writeLeaveSection();
}

bool Entities::MaterialSources::is_entity(Map* map,int x,int y)
{
 if(mask==0) return map->terrainPropertiesAt(x,y).walkable;
 return (map->materialMaskAt(map->coordToIndex(x,y)) & mask)!=0;
}
bool Entities::MaterialSources::can_change(const ResourceRegistry& registry)
{
    // The empty mask matches terrain; terrain generation owns its invalidation.
    return (registry.mutableMaterialSources() & mask)!=0;
}
bool Entities::MaterialSources::operator==(const Entity& other) const
{
 return typeid(other)==typeid(MaterialSources) && static_cast<const MaterialSources&>(other).mask==mask;
}
bool Entities::MaterialSources::load(GAGCore::InputStream* stream,Player*,Sint32)
{
 stream->readEnterSection("ResourceSet"); mask=stream->readUint32("mask"); stream->readLeaveSection();
 return (mask&~((1u<<MaterialCount)-1))==0;
}
void Entities::MaterialSources::save(GAGCore::OutputStream* stream)
{
 stream->writeEnterSection("ResourceSet"); stream->writeUint32(mask,"mask"); stream->writeLeaveSection();
}

bool Entities::MaterialSource::is_entity(const AIEngine::AIWorldView& world,int x,int y)
{ return MapState::hasMaterialSlot(world.state(),world.tileIndex(x,y),material); }
bool Entities::AnyResource::is_entity(const AIEngine::AIWorldView& world,int x,int y)
{ return world.resourceAt(world.tileIndex(x,y)).resource.type!=NO_RES_TYPE; }
bool Entities::MaterialSources::is_entity(const AIEngine::AIWorldView& world,int x,int y)
{
    const auto index=world.tileIndex(x,y);
    if(!mask) return world.state().terrainProperties(index).walkable;
    return (MapState::materialMaskAt(world.state(),index)&mask)!=0;
}
bool Entities::ResourceGroundObstacle::is_entity(Map* map, int x, int y)
{ return map->resourceBlocksGround(map->coordToIndex(x,y)); }
bool Entities::ResourceGroundObstacle::operator==(const Entity& other) const
{ return typeid(other)==typeid(ResourceGroundObstacle); }
bool Entities::ResourceGroundObstacle::load(GAGCore::InputStream* stream, Player*, Sint32)
{ stream->readEnterSection("ResourceGroundObstacle"); stream->readLeaveSection(); return true; }
void Entities::ResourceGroundObstacle::save(GAGCore::OutputStream* stream)
{ stream->writeEnterSection("ResourceGroundObstacle"); stream->writeLeaveSection(); }

bool Entities::ResourceBuildingObstacle::is_entity(Map* map, int x, int y)
{ return map->resourceBlocksBuilding(map->coordToIndex(x,y)); }
bool Entities::ResourceBuildingObstacle::operator==(const Entity& other) const
{ return typeid(other)==typeid(ResourceBuildingObstacle); }
bool Entities::ResourceBuildingObstacle::load(GAGCore::InputStream* stream, Player*, Sint32)
{ stream->readEnterSection("ResourceBuildingObstacle"); stream->readLeaveSection(); return true; }
void Entities::ResourceBuildingObstacle::save(GAGCore::OutputStream* stream)
{ stream->writeEnterSection("ResourceBuildingObstacle"); stream->writeLeaveSection(); }
bool Entities::ResourceGroundObstacle::is_entity(const AIEngine::AIWorldView& world,int x,int y)
{ return MapState::resourceBlocksGround(world.state(),world.tileIndex(x,y)); }
bool Entities::ResourceBuildingObstacle::is_entity(const AIEngine::AIWorldView& world,int x,int y)
{ return MapState::resourceBlocksBuilding(world.state(),world.tileIndex(x,y)); }
