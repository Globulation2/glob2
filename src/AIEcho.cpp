/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "AIEcho.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingsTypes.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include "boost/tuple/tuple_io.hpp"
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using namespace boost;



void AIEcho::signature_write(GAGCore::OutputStream *stream)
{
	stream->write("EchoSig", 7, "signature");
}



void AIEcho::signature_check(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	char signature[7];
	stream->read(signature, 7, "signature");
	if (memcmp(signature,"EchoSig", 7)!=0)
	{

		std::cerr<<"Signature match failed. Expected \"EchoSig\", recieved \""<<signature<<"\""<<std::endl;
		assert(false);
	}
}



Entities::Entity* Entities::Entity::load_entity(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Entity");
	EntityType type = static_cast<EntityType>(stream->readUint32("type"));
	Entity* entity = NULL;
	switch(type)
	{
		case Entities::EBuilding:
			entity = new Entities::Building;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EAnyTeamBuilding:
			entity = new Entities::AnyTeamBuilding;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EAnyBuilding:
			entity = new Entities::AnyBuilding;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::ERessource:
			entity = new Entities::Ressource;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EAnyRessource:
			entity = new Entities::AnyRessource;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EWater:
			entity = new Entities::Water;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::EPosition:
			entity = new Entities::Position;
			entity->load(stream, player, versionMinor);
		break;
		case Entities::ESand:
			entity = new Entities::Sand;
			entity->load(stream, player, versionMinor);
		break;
	};
	stream->readLeaveSection();
	return entity;
}



void Entities::Entity::save_entity(Entity* entity, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Entity");
	stream->writeUint32(entity->get_type(), "type");
	entity->save(stream);
	stream->writeLeaveSection();
}



Entities::Building::Building(int building_type, int team, bool under_construction) : building_type(building_type), team(team), under_construction(under_construction)
{

}


bool Entities::Building::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id!=NOGBID)
	{
		int team_id=::Building::GIDtoTeam(building_id);
		if(team_id==team &&
		   map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->typeNum==building_type &&
		   (map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->constructionResultState==::Building::NO_CONSTRUCTION || under_construction)
		   )
		{
			return true;
		}
	}
	return false;
}

bool Entities::Building::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Building) &&
	   static_cast<const Entities::Building&>(rhs).building_type==building_type &&
	   static_cast<const Entities::Building&>(rhs).team==team &&
	   static_cast<const Entities::Building&>(rhs).under_construction==under_construction 
	    )
		return true;
	return false;
}



bool Entities::Building::can_change()
{
	return true;
}



Entities::EntityType Entities::Building::get_type()
{
	return Entities::EBuilding;
}



bool Entities::Building::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Building");
	building_type = stream->readSint32("building_type");
	team = stream->readSint32("team");
	under_construction = stream->readUint8("under_construction");
	stream->readLeaveSection();
	return true;
}



void Entities::Building::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Building");
	stream->writeSint32(building_type, "building_type");
	stream->writeSint32(team, "team");
	stream->writeUint8(under_construction, "under_construction");
	stream->writeLeaveSection();
}



Entities::AnyTeamBuilding::AnyTeamBuilding(int team, bool under_construction) : team(team), under_construction(under_construction)
{

}



bool Entities::AnyTeamBuilding::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id!=NOGBID)
	{
		int team_id=::Building::GIDtoTeam(building_id);
		if(team_id==team &&
		   (map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->constructionResultState==::Building::NO_CONSTRUCTION || under_construction)
		   )
		{
			return true;
		}
	}
	return false;
}



bool Entities::AnyTeamBuilding::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::AnyTeamBuilding) &&
	   static_cast<const Entities::AnyTeamBuilding&>(rhs).team==team &&
	   static_cast<const Entities::AnyTeamBuilding&>(rhs).under_construction==under_construction 
	    )
		return true;
	return false;
}



bool Entities::AnyTeamBuilding::can_change()
{
	return true;
}



Entities::EntityType Entities::AnyTeamBuilding::get_type()
{
	return Entities::EAnyTeamBuilding;
}



bool Entities::AnyTeamBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyTeamBuilding");
	team = stream->readSint32("team");
	under_construction = stream->readUint8("under_construction");
	stream->readLeaveSection();
	return true;
}



void Entities::AnyTeamBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyTeamBuilding");
	stream->writeSint32(team, "team");
	stream->writeUint8(under_construction, "under_construction");
	stream->writeLeaveSection();
}



Entities::AnyBuilding::AnyBuilding(bool under_construction) : under_construction(under_construction)
{
}



bool Entities::AnyBuilding::is_entity(Map* map, int posx, int posy)
{
	int building_id=map->getBuilding(posx, posy);
	if(building_id!=NOGBID)
	{
		int team_id=::Building::GIDtoTeam(building_id);
		if(map->game->teams[team_id]->myBuildings[::Building::GIDtoID(building_id)]->constructionResultState==::Building::NO_CONSTRUCTION || under_construction)
			return true;
	}
	return false;
}



bool Entities::AnyBuilding::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::AnyBuilding) &&
	   static_cast<const Entities::AnyBuilding&>(rhs).under_construction==under_construction
           )
		return true;
	return false;
}



bool Entities::AnyBuilding::can_change()
{
	return true;
}



Entities::EntityType Entities::AnyBuilding::get_type()
{
	return Entities::EAnyBuilding;
}



bool Entities::AnyBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyBuilding");
	under_construction = stream->readUint8("under_construction");
	stream->readLeaveSection();
	return true;
}



void Entities::AnyBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyBuilding");
	stream->writeUint8(under_construction, "under_construction");
	stream->writeLeaveSection();
}



Entities::Ressource::Ressource(int ressource_type) : ressource_type(ressource_type)
{

}



bool Entities::Ressource::is_entity(Map* map, int posx, int posy)
{
	if(map->isRessourceTakeable(posx, posy, ressource_type))
	{
		return true;
	}
	return false;
}



bool Entities::Ressource::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Ressource) &&
	   static_cast<const Entities::Ressource&>(rhs).ressource_type==ressource_type
	    )
		return true;
	return false;
}



bool Entities::Ressource::can_change()
{
	if(ressource_type==WOOD || ressource_type==CORN || ressource_type==ALGA)
		return true;
	return false;
}



Entities::EntityType Entities::Ressource::get_type()
{
	return Entities::ERessource;
}



bool Entities::Ressource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Ressource");
	ressource_type = stream->readSint32("ressource_type");
	stream->readLeaveSection();
	return true;
}



void Entities::Ressource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Ressource");
	stream->writeSint32(ressource_type, "ressource_type");
	stream->writeLeaveSection();
}



Entities::AnyRessource:: AnyRessource()
{

}



bool Entities::AnyRessource:: is_entity(Map* map, int posx, int posy)
{
	if(map->isRessource(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::AnyRessource::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::AnyRessource))
		return true;
	return false;
}



bool Entities::AnyRessource::can_change()
{
	return true;
}



Entities::EntityType Entities::AnyRessource::get_type()
{
	return Entities::EAnyRessource;
}



bool Entities::AnyRessource::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AnyRessource");
	stream->readLeaveSection();
	return true;
}



void Entities::AnyRessource::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AnyRessource");
	stream->writeLeaveSection();
}



Entities::Water::Water()
{

}



bool Entities::Water::is_entity(Map* map, int posx, int posy)
{
	if(map->isWater(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::Water::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Water))
		return true;
	return false;
}



bool Entities::Water::can_change()
{
	return false;
}



Entities::EntityType Entities::Water::get_type()
{
	return Entities::EWater;
}



bool Entities::Water::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Water");
	stream->readLeaveSection();
	return true;
}



void Entities::Water::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Water");
	stream->writeLeaveSection();
}


Entities::Position::Position(int x, int y) : x(x), y(y)
{

}


bool Entities::Position::is_entity(Map* map, int posx, int posy)
{
	if(x==posx && y==posy)
	{
		return true;
	}
	return false;
}


bool Entities::Position::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Position) && 
	   static_cast<const Entities::Position&>(rhs).x==x &&
	   static_cast<const Entities::Position&>(rhs).y==y) 
		return true;
	return false;
}


bool Entities::Position::can_change()
{
	return false;
}


Entities::EntityType Entities::Position::get_type()
{
	return Entities::EPosition;
}


bool Entities::Position::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Position");
	x=stream->readSint32("posX");
	y=stream->readSint32("posY");
	stream->readLeaveSection();
	return false;
}


void Entities::Position::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Position");
	stream->writeSint32(x, "posX");
	stream->writeSint32(y, "posy");
	stream->writeLeaveSection();
}



Entities::Sand::Sand()
{

}



bool Entities::Sand::is_entity(Map* map, int posx, int posy)
{
	if(map->hasSand(posx, posy))
	{
		return true;
	}
	return false;
}



bool Entities::Sand::operator==(const Entity& rhs)
{
	if(typeid(rhs)==typeid(Entities::Sand))
		return true;
	return false;
}



bool Entities::Sand::can_change()
{
	return false;
}



Entities::EntityType Entities::Sand::get_type()
{
	return Entities::ESand;
}



bool Entities::Sand::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Sand");
	stream->readLeaveSection();
	return true;
}



void Entities::Sand::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Sand");
	stream->writeLeaveSection();
}



GradientInfo::GradientInfo()
{
	needs_updated=indeterminate;
}


GradientInfo::~GradientInfo()
{

}


void GradientInfo::add_source(Entities::Entity* source)
{
	sources.push_back(boost::shared_ptr<Entities::Entity>(source));
}


void GradientInfo::add_obstacle(Entities::Entity* obstacle)
{
	obstacles.push_back(boost::shared_ptr<Entities::Entity>(obstacle));
}


bool GradientInfo::match_source(Map* map, int posx, int posy)
{
	for(unsigned int x=0; x<sources.size(); ++x)
		if(sources[x]->is_entity(map, posx, posy))
			return true;
	return false;
}


bool GradientInfo::match_obstacle(Map* map, int posx, int posy)
{
	for(unsigned int x=0; x<obstacles.size(); ++x)
		if(obstacles[x]->is_entity(map, posx, posy))
			return true;
	return false;
}


bool GradientInfo::operator==(const GradientInfo& rhs) const
{
	if(sources.size()!=rhs.sources.size() || obstacles.size() != rhs.obstacles.size())
		return false;
	for(unsigned int i=0; i<sources.size(); ++i)
	{
		if(!((*sources[i])==(*rhs.sources[i])))
			return false;
	}

	for(unsigned int i=0; i<obstacles.size(); ++i)
	{
		if(!((*obstacles[i])==(*rhs.obstacles[i])))
			return false;
	}
	return true;
}



bool GradientInfo::needs_updating() const
{
	if(needs_updated)
		return true;
	else if(!needs_updated)
		return false;
	else
	{
		needs_updated=false;
		for(unsigned int i=0; i<sources.size(); ++i)
		{
			if(sources[i]->can_change())
			{
				needs_updated=true;
				return true;
			}
		}

		for(unsigned int i=0; i<obstacles.size(); ++i)
		{
			if(obstacles[i]->can_change())
			{
				needs_updated=true;
				return true;
			}
		}
	}
	return false;
}



bool GradientInfo::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("GradientInfo");

	stream->readEnterSection("sources");
	int size=stream->readUint32("size");
	sources.resize(size);
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		sources[n]=boost::shared_ptr<Entities::Entity>(Entities::Entity::load_entity(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("obstacles");
	size=stream->readUint32("size");
	obstacles.resize(size);
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		obstacles[n]=boost::shared_ptr<Entities::Entity>(Entities::Entity::load_entity(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}



void GradientInfo::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("GradientInfo");

	stream->writeEnterSection("sources");
	stream->writeUint32(sources.size(), "size");
	for(unsigned n=0; n<sources.size(); ++n)
	{
		stream->writeEnterSection(n);
		Entities::Entity::save_entity(sources[n].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("obstacles");
	stream->writeUint32(obstacles.size(), "size");
	for(unsigned n=0; n<obstacles.size(); ++n)
	{
		stream->writeEnterSection(n);
		Entities::Entity::save_entity(obstacles[n].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}



GradientInfo make_gradient_info(Entities::Entity* source)
{
	GradientInfo gi;
	gi.add_source(source);
	return gi;
}



GradientInfo make_gradient_info_obstacle(Entities::Entity* source, Entities::Entity* obstacle)
{
	GradientInfo gi;
	gi.add_source(source);
	gi.add_obstacle(obstacle);
	return gi;
}



GradientInfo make_gradient_info(Entities::Entity* source1, Entities::Entity* source2)
{
	GradientInfo gi;
	gi.add_source(source1);
	gi.add_source(source2);
	return gi;
}



GradientInfo make_gradient_info_obstacle(Entities::Entity* source1, Entities::Entity* source2, Entities::Entity* obstacle)
{
	GradientInfo gi;
	gi.add_source(source1);
	gi.add_source(source2);
	gi.add_obstacle(obstacle);
	return gi;
}



Gradient::Gradient(const GradientInfo& gi) 
{
	gradient_info=gi;
	width=0;
}


void Gradient::recalculate(Map* map)
{
	width=map->getW();
//	if(gradient==NULL)
//		gradient=new Sint16[map->getW()*map->getH()];
//	std::fill(gradient, gradient+(map->getW()*map->getH()),0); 

	gradient.resize(map->getW()*map->getH());
	std::fill(gradient.begin(), gradient.end(),0); 

	std::queue<position> positions;
	for(int x=0; x<map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(gradient_info.match_source(map, x, y))
			{
				gradient[get_pos(x, y)]=2;
				positions.push(position(x, y));
			}
			else if(gradient_info.match_obstacle(map, x, y))
				gradient[get_pos(x, y)]=1;
		}
	}
	while(!positions.empty())
	{
		position p=positions.front();
		positions.pop();

		int left=p.x-1;
		if(left<0)
			left+=map->getW();
		int right=p.x+1;
		if(right>=map->getW())
			right-=map->getW();
		int up=p.y-1;
		if(up<0)
			up+=map->getH();
		int down=p.y+1;
		if(down>=map->getH())
			down-=map->getH();
		int center_h=p.x;
		int center_y=p.y;
		int n=gradient[get_pos(center_h, center_y)];

		if(gradient[get_pos(left, up)]==0)
		{
			gradient[get_pos(left, up)]=n+1;
			positions.push(position(left, up));
		}

		if(gradient[get_pos(center_h, up)]==0)
		{
			gradient[get_pos(center_h, up)]=n+1;
			positions.push(position(center_h, up));
		}

		if(gradient[get_pos(right, up)]==0)
		{
			gradient[get_pos(right, up)]=n+1;
			positions.push(position(right, up));
		}

		if(gradient[get_pos(left, center_y)]==0)
		{
			gradient[get_pos(left, center_y)]=n+1;
			positions.push(position(left, center_y));
		}

		if(gradient[get_pos(right, center_y)]==0)
		{
			gradient[get_pos(right, center_y)]=n+1;
			positions.push(position(right, center_y));
		}

		if(gradient[get_pos(left, down)]==0)
		{
			gradient[get_pos(left, down)]=n+1;
			positions.push(position(left, down));
		}

		if(gradient[get_pos(center_h, down)]==0)
		{
			gradient[get_pos(center_h, down)]=n+1;
			positions.push(position(center_h, down));
		}

		if(gradient[get_pos(right, down)]==0)
		{
			gradient[get_pos(right, down)]=n+1;
			positions.push(position(right, down));
		}

	}
}


int Gradient::get_height(int posx, int posy) const
{
	return gradient[get_pos(posx, posy)]-2;
}



GradientManager::GradientManager(Map* map) : map(map), cur_update(0), timer(0)
{
}


Gradient& GradientManager::get_gradient(const GradientInfo& gi)
{
	for(std::vector<boost::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if(ticks_since_update[i-gradients.begin()]>150)
			{
				ticks_since_update[i-gradients.begin()]=0;
				(*i)->recalculate(map);
			}
			return **i;
		}
	}

	//Did not find a matching gradient
	gradients.push_back(boost::shared_ptr<Gradient>(new Gradient(gi)));
	(*(gradients.end()-1))->recalculate(map);
	ticks_since_update.push_back(0);
	return **(gradients.end()-1);
}


void GradientManager::queue_gradient(const GradientInfo& gi)
{
	for(unsigned i=0; i<gradients.size(); ++i)
	{
		if(gradients[i]->get_gradient_info() == gi)
		{
			if(gi.needs_updating())
			{
				queuedGradients.push(i);
			}
			return;
		}
	}
	//Did not find a matching gradient
	gradients.push_back(boost::shared_ptr<Gradient>(new Gradient(gi)));
	ticks_since_update.push_back(200);
	queuedGradients.push(gradients.size()-1);
}


bool GradientManager::is_updated(const GradientInfo& gi)
{
	for(std::vector<boost::shared_ptr<Gradient> >::iterator i=gradients.begin(); i!=gradients.end(); ++i)
	{
		if((*i)->get_gradient_info() == gi)
		{
			if(ticks_since_update[i-gradients.begin()]>150 && (*i)->get_gradient_info().needs_updating())
			{
				return false;
			}
			return true;
		}
	}
	//If the gradient hasn't been queued to be updated, consider it updated,
	//and it will be calculated on request
	return true;
}


void GradientManager::update()
{
	timer++;
	std::transform(ticks_since_update.begin(), ticks_since_update.end(), ticks_since_update.begin(), increment);

	if((timer%1)==0 && !queuedGradients.empty())
	{
		int g=queuedGradients.front();
		if(ticks_since_update[g]>50)
		{
			gradients[g]->recalculate(map);
			ticks_since_update[g]=0;
		}
		queuedGradients.pop();
		return;
	}
}



Constraint* Constraint::load_constraint(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Constraint");
	ConstraintType type=static_cast<ConstraintType>(stream->readUint32("type"));
	Constraint* constraint=NULL;
	switch(type)
	{
		case CTMinimumDistance:
			constraint=new MinimumDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTMaximumDistance:
			constraint=new MaximumDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTMinimizedDistance:
			constraint=new MinimizedDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTMaximizedDistance:
			constraint=new MaximizedDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTCenterOfBuilding:
			constraint=new CenterOfBuilding;
			constraint->load(stream, player, versionMinor);
		break;
		case CTSinglePosition:
			constraint=new SinglePosition;
			constraint->load(stream, player, versionMinor);
		break;
	}
	stream->readLeaveSection();
	return constraint;
}



void Constraint::save_constraint(Constraint* constraint, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Constraint");
	stream->writeUint32(constraint->get_type(), "type");
	constraint->save(stream);
	stream->writeLeaveSection();
}



MinimumDistance::MinimumDistance(const Gradients::GradientInfo& gi, int distance) : gi(gi), gradient_cache(NULL), distance(distance)
{

}


int MinimumDistance::calculate_constraint(Echo& echo, int x, int y)
{
	return 0;
}


bool MinimumDistance::passes_constraint(Echo& echo, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&echo.get_gradient_manager().get_gradient(gi);
	int height=gradient_cache->get_height(x, y);
	if(height==-2)
		return false;
	if(height>=distance)
		return true;
	return false;
}


ConstraintType MinimumDistance::get_type()
{
	return CTMinimumDistance;
}



bool MinimumDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MinimumDistance");
	distance = stream->readSint32("distance");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MinimumDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MinimumDistance");
	stream->writeSint32(distance, "distance");
	gi.save(stream);
	stream->writeLeaveSection();
}



MaximumDistance::MaximumDistance(const Gradients::GradientInfo& gi, int distance) : gi(gi), gradient_cache(NULL), distance(distance)
{

}


int MaximumDistance::calculate_constraint(Echo& echo, int x, int y)
{
	return 0;
}


bool MaximumDistance::passes_constraint(Echo& echo, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&echo.get_gradient_manager().get_gradient(gi);
	int height=gradient_cache->get_height(x, y);
	if(height==-2)
		return false;
	if(height<=distance)
		return true;
	return false;
}


ConstraintType MaximumDistance::get_type()
{
	return CTMaximumDistance;
}



bool MaximumDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MaximumDistance");
	distance = stream->readSint32("distance");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MaximumDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MaximumDistance");
	stream->writeSint32(distance, "distance");
	gi.save(stream);
	stream->writeLeaveSection();
}



MinimizedDistance::MinimizedDistance(const Gradients::GradientInfo& gi, int weight) : gi(gi), gradient_cache(NULL), weight(weight)
{

}


int MinimizedDistance::calculate_constraint(Echo& echo, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&echo.get_gradient_manager().get_gradient(gi);
	return -(gradient_cache->get_height(x, y) * weight);
}


bool MinimizedDistance::passes_constraint(Echo& echo, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&echo.get_gradient_manager().get_gradient(gi);
	return gradient_cache->get_height(x, y)!=-2;
}


ConstraintType MinimizedDistance::get_type()
{
	return CTMinimizedDistance;
}



bool MinimizedDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MinimizedDistance");
	weight = stream->readSint32("weight");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MinimizedDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MinimizedDistance");
	stream->writeSint32(weight, "weight");
	gi.save(stream);
	stream->writeLeaveSection();
}



MaximizedDistance::MaximizedDistance(const Gradients::GradientInfo& gi, int weight) : gi(gi), gradient_cache(NULL), weight(weight)
{

}


int MaximizedDistance::calculate_constraint(Echo& echo, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&echo.get_gradient_manager().get_gradient(gi);
	return gradient_cache->get_height(x, y) * weight;
}


bool MaximizedDistance::passes_constraint(Echo& echo, int x, int y)
{
	if(gradient_cache==NULL)
		gradient_cache=&echo.get_gradient_manager().get_gradient(gi);
	return gradient_cache->get_height(x, y)!=-2;
}


ConstraintType MaximizedDistance::get_type()
{
	return CTMaximizedDistance;
}



bool MaximizedDistance::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("MaximizedDistance");
	weight = stream->readSint32("weight");
	gi.load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void MaximizedDistance::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MaximizedDistance");
	stream->writeSint32(weight, "weight");
	gi.save(stream);
	stream->writeLeaveSection();
}



CenterOfBuilding::CenterOfBuilding(int gbid) : gbid(gbid)
{

}



int CenterOfBuilding::calculate_constraint(Echo& echo, int x, int y)
{
	return 0;
}



bool CenterOfBuilding::passes_constraint(Echo& echo, int x, int y)
{
	Building* b=echo.player->game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
	if(b)
	{
		if((b->posX+b->type->width/2)==x && (b->posY+b->type->height/2)==y)
		{
			return true;
		}
	}
	return false;
}


ConstraintType CenterOfBuilding::get_type()
{
	return CTCenterOfBuilding;
}



bool CenterOfBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("CenterOfBuilding");
	gbid = stream->readSint32("gbid");
	stream->readLeaveSection();
	return true;
}



void CenterOfBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("CenterOfBuilding");
	stream->writeSint32(gbid, "gbid");
	stream->writeLeaveSection();
}



SinglePosition::SinglePosition(int posx, int posy) : posx(posx), posy(posy)
{

}



int SinglePosition::calculate_constraint(Echo& echo, int x, int y)
{
	return 0;
}



bool SinglePosition::passes_constraint(Echo& echo, int x, int y)
{
	if(posx==x && posy==y)
		return true;
	return false;
}



ConstraintType SinglePosition::get_type()
{
	return CTSinglePosition;
}



bool SinglePosition::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("SinglePosition");
	posx = stream->readSint32("posx");
	posy = stream->readSint32("posy");
	stream->readLeaveSection();
	return true;
}



void SinglePosition::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("SinglePosition");
	stream->writeSint32(posx, "posx");
	stream->writeSint32(posy, "posy");
	stream->writeLeaveSection();
}



BuildingOrder::BuildingOrder(int building_type, int number_of_workers) : building_type(building_type), number_of_workers(number_of_workers)
{

}



bool BuildingOrder::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingOrder");

	building_type=stream->readUint32("building_type");
	number_of_workers=stream->readUint32("number_of_workers");

	stream->readEnterSection("constraints");
	Uint32 size = stream->readUint32("size");
	constraints.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		constraints[x] = boost::shared_ptr<Constraint>(Constraint::load_constraint(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	stream->readEnterSection("conditions");
	size = stream->readUint32("size");
	conditions.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		conditions[x] = boost::shared_ptr<Condition>(Condition::load_condition(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void BuildingOrder::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingOrder");

	stream->writeUint32(building_type, "building_type");
	stream->writeUint32(number_of_workers, "number_of_workers");

	stream->writeEnterSection("constraints");
	stream->writeUint32(constraints.size(), "size");
	for(unsigned x=0; x<constraints.size(); ++x)
	{
		stream->writeEnterSection(x);
		Constraint::save_constraint(constraints[x].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("conditions");
	stream->writeUint32(conditions.size(), "size");
	for(unsigned x=0; x<conditions.size(); ++x)
	{
		stream->writeEnterSection(x);
		Condition::save_condition(conditions[x].get(), stream);
		stream->writeLeaveSection();
	}

	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void BuildingOrder::add_constraint(Constraint* constraint)
{
	constraints.push_back(boost::shared_ptr<Constraint>(constraint));
}


void BuildingOrder::add_condition(Condition* condition)
{
	conditions.push_back(boost::shared_ptr<Condition>(condition));
}



position BuildingOrder::find_location(Echo& echo, Map* map, GradientManager& manager)
{
	position best(0,0);
	Player* player=echo.player;
	int best_score=std::numeric_limits<int>::min();
	BuildingType* type=globalContainer->buildingsTypes.getByType(IntBuildingType::typeFromShortNumber(building_type), 0, true);
	bool check_flag=false;
	//If theres no type for a construction zone, then this is a flag
	if(type==NULL)
	{
		type=globalContainer->buildingsTypes.getByType(IntBuildingType::typeFromShortNumber(building_type), 0, false);
		check_flag=true;
	}

	for(int x=0; x<map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(!check_flag && !map->isHardSpaceForBuilding(x, y, type->width, type->height))
				continue;

			if(check_flag && echo.get_flag_map().get_flag(x, y)!=NOGBID)
				continue;
			int score=0;
			bool passes=true;
			for(std::vector<boost::shared_ptr<Constraint> >::iterator i=constraints.begin(); i!=constraints.end(); ++i)
			{
				for(int x2=0; x2<type->width && passes; ++x2)
					for(int y2=0; y2<type->height && passes; ++y2)
						if((x2==0 || y2==0 || x2==type->width-1 || y2==type->height-1))
						{
							if(!(*i)->passes_constraint(echo, map->normalizeX(x+x2), map->normalizeY(y+y2)))
							{
									passes=false;
							}
						}
				if(!passes)
				{
					break;
				}

				if(!check_flag && (!map->isMapDiscovered(x, y, player->team->allies) ||
				   !map->isMapDiscovered(x+type->width-1, y+type->height-1, player->team->allies))
				    )
				{
					passes=false;
					break;
				}
				score+=(*i)->calculate_constraint(echo, map->normalizeX(x), map->normalizeY(y));
				score+=(*i)->calculate_constraint(echo, map->normalizeX(x+type->width-1), map->normalizeY(y+type->height-1));
				score+=(*i)->calculate_constraint(echo, map->normalizeX(x), map->normalizeY(y+type->height-1));
				score+=(*i)->calculate_constraint(echo, map->normalizeX(x+type->width-1), map->normalizeY(y));
			}
			if(!passes)
				continue;
			if(score>best_score)
			{
				best=position(x, y);
				best_score=score;
			}
		}
	}

	return best;
}



boost::logic::tribool BuildingOrder::passes_conditions(Echo& echo)
{
	for(unsigned int i=0; i<conditions.size(); ++i)
	{
		boost::logic::tribool passes=conditions[i]->passes(echo);
		if(passes)
			continue;
		else if(!passes)
			return false;
		else
			return indeterminate;

	}

	for(unsigned n=0; n<constraints.size(); ++n)
	{
		if(constraints[n]->get_gradient_info())
		{
			bool is_updated=echo.get_gradient_manager().is_updated(*constraints[n]->get_gradient_info());
			if(!is_updated)
				return false;
		}
	}

	return true;
}



void BuildingOrder::queue_gradients(Gradients::GradientManager& manager)
{
	for(unsigned n=0; n<constraints.size(); ++n)
	{
		if(constraints[n]->get_gradient_info())
		{
			manager.queue_gradient(*constraints[n]->get_gradient_info());
		}
	}
}


FlagMap::FlagMap(Echo& echo) : flagmap(echo.player->map->getW()*echo.player->map->getH(), NOGBID), width(echo.player->map->getW()), echo(echo)
{
}



int FlagMap::get_flag(int x, int y)
{
	return flagmap[y*width+x];
}



void FlagMap::set_flag(int x, int y, int gid)
{
	flagmap[y*width+x]=gid;
}



bool FlagMap::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("FlagMap");
	stream->readEnterSection("flagmap");
	Uint32 size=stream->readUint32("size");
	flagmap.resize(size);
	for (Uint32 flagmap_index = 0; flagmap_index < size; flagmap_index++)
	{
		stream->readEnterSection(flagmap_index);
		flagmap[flagmap_index]=stream->readUint32("gid");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	width=stream->readUint32("width");
	stream->readLeaveSection();
	return true;
}



void FlagMap::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("FlagMap");
	stream->writeEnterSection("flagmap");
	stream->writeUint32(flagmap.size(), "size");
	for (Uint32 flagmap_index = 0; flagmap_index < flagmap.size(); flagmap_index++)
	{
		stream->writeEnterSection(flagmap_index);
		stream->writeUint32(flagmap[flagmap_index], "gid");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint32(width, "width");
	stream->writeLeaveSection();
}



BuildingRegister::BuildingRegister(Player* player, Echo& echo) : building_id(0), player(player), echo(echo)
{

}



void BuildingRegister::initiate()
{
	for(unsigned int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* b=player->team->myBuildings[i];
		if(b!=NULL)
		{
			found_buildings[building_id++]=boost::make_tuple(b->posX, b->posY, b->type->shortTypeNum, b->gid, false);
		}
	}
}



unsigned int BuildingRegister::register_building()
{
	pending_buildings[building_id]=boost::make_tuple(-1, -1, -1, -1);
	return building_id++;
}



void BuildingRegister::issue_order(int id, int x, int y, int building_type)
{
	pending_buildings[id]=boost::make_tuple(x, y, building_type, 0);
}



void BuildingRegister::remove_building(int id)
{
	pending_buildings.erase(id);
}



bool BuildingRegister::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingRegister");

	stream->readEnterSection("pending_buildings");
	Uint32 pending_size=stream->readUint32("size");
	for(Uint32 pending_index=0; pending_index<pending_size; ++pending_index)
	{
		stream->readEnterSection(pending_index);
		Uint32 id=stream->readSint32("echo_building_id");
		Uint32 x=stream->readSint32("xpos");
		Uint32 y=stream->readSint32("ypos");
		Uint32 type=stream->readSint32("building_type");
		Uint32 ticks=stream->readSint32("ticks_since_registered");
		pending_buildings[id]=boost::make_tuple(x, y, type, ticks);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("found_buildings");
	Uint32 found_size=stream->readUint32("size");
	for(Uint32 found_index=0; found_index<found_size; ++found_index)
	{
		stream->readEnterSection(found_index);
		Uint32 id=stream->readUint32("echo_building_id");
		Uint32 xpos=stream->readUint32("xpos");
		Uint32 ypos=stream->readUint32("ypos");
		Uint32 building_type=stream->readUint32("building_type");
		Uint32 gid=stream->readUint32("gid");
		Uint8 upgrade_status=stream->readUint8("upgrade_status");
		boost::logic::tribool t;
		if(upgrade_status==0)
			t=false;
		else if(upgrade_status==1)
			t=true;
		else
			t=indeterminate;
		found_buildings[id]=boost::make_tuple(xpos, ypos, building_type, gid, t);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void BuildingRegister::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingRegister");

	stream->writeEnterSection("pending_buildings");
	unsigned int pending_size=0;
	stream->writeUint32(pending_buildings.size(), "size");
	for(pending_iterator i=pending_buildings.begin(); i!=pending_buildings.end(); ++i)
	{
		stream->writeEnterSection(pending_size);
		stream->writeSint32(i->first, "echo_building_id");
		stream->writeSint32(i->second.get<0>(), "xpos");
		stream->writeSint32(i->second.get<1>(), "ypos");
		stream->writeSint32(i->second.get<2>(), "building_type");
		stream->writeSint32(i->second.get<3>(), "ticks_since_registered");
		stream->writeLeaveSection();
		pending_size++;
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("found_buildings");
	unsigned int found_size=0;
	stream->writeUint32(found_buildings.size(), "size");
	for(found_iterator i=found_buildings.begin(); i!=found_buildings.end(); ++i)
	{
		stream->writeEnterSection(found_size);
		stream->writeUint32(i->first, "echo_building_id");
		stream->writeUint32(i->second.get<0>(), "xpos");
		stream->writeUint32(i->second.get<1>(), "ypos");
		stream->writeUint32(i->second.get<2>(), "building_type");
		stream->writeUint32(i->second.get<3>(), "gid");
		if(i->second.get<4>())
			stream->writeUint8(1, "upgrade_status");
		else if(!i->second.get<4>())
			stream->writeUint8(0, "upgrade_status");
		else
			stream->writeUint8(2, "upgrade_status");
		stream->writeLeaveSection();
		found_size++;
	}
	stream->writeLeaveSection();

	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



void BuildingRegister::set_upgrading(unsigned int id)
{
	found_buildings[id].get<4>()=indeterminate;
}




void BuildingRegister::tick()
{
	for(pending_iterator i=pending_buildings.begin(); i!=pending_buildings.end();)
	{
		//When get<3>() is -1, it means that the building order hasen't been sent to the glob2 engine yet.
		//This is used when the building is registered, but awaiting conditions to be satisfied.
		if(i->second.get<3>()!=-1)
		{
			i->second.get<3>()++;
			if(i->second.get<3>() > 300)
			{
				pending_iterator current=i;
				++i;
				pending_buildings.erase(current);
				continue;
			}
			int gbid=NOGBID;
			if(i->second.get<2>() > IntBuildingType::DEFENSE_BUILDING && i->second.get<2>() < IntBuildingType::STONE_WALL)
			{
				gbid=is_flag(echo, i->second.get<0>(), i->second.get<1>());
			}
			else
			{
				gbid=player->map->getBuilding(i->second.get<0>(), i->second.get<1>());
			}
			if(gbid!=NOGBID)
			{
				if(i->second.get<2>() > IntBuildingType::DEFENSE_BUILDING && i->second.get<2>() < IntBuildingType::STONE_WALL)
				{
					echo.get_flag_map().set_flag(i->second.get<0>(), i->second.get<1>(), gbid);
				}
				found_buildings[i->first]=boost::make_tuple(i->second.get<0>(), i->second.get<1>(), i->second.get<2>(), gbid, false);
				pending_iterator current=i;
				++i;
				pending_buildings.erase(current);
				continue;
			}
		}
		++i;
	}
	for(found_iterator i = found_buildings.begin(); i!=found_buildings.end();)
	{
		if(i->second.get<2>() > IntBuildingType::DEFENSE_BUILDING && i->second.get<2>() < IntBuildingType::STONE_WALL)
		{
			if(echo.get_flag_map().get_flag(i->second.get<0>(), i->second.get<1>())==NOGBID)
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			if(player->team->myBuildings[::Building::GIDtoID(i->second.get<3>())]==NULL)
			{
				echo.get_flag_map().set_flag(i->second.get<0>(), i->second.get<1>(), NOGBID);
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
		}
		else
		{
			const int gbid=player->map->getBuilding(i->second.get<0>(), i->second.get<1>());
			if(gbid==NOGBID || gbid != i->second.get<3>())
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			Building* b=player->team->myBuildings[::Building::GIDtoID(gbid)];
			if(b==NULL)
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			//True
			if(i->second.get<4>())
			{
				i->second.get<0>()=b->posX;
				i->second.get<1>()=b->posY;
				if(b->constructionResultState==::Building::NO_CONSTRUCTION)
				{
					i->second.get<4>()=false;
				}
			}
			//False
			else if(!i->second.get<4>())
			{

			}
			//Indeterminate
			else
			{
				if(b->constructionResultState!=::Building::NO_CONSTRUCTION)
				{
					i->second.get<4>()=true;
				}
			}
		}
		++i;
	}
}

bool BuildingRegister::is_building_pending(unsigned int id)
{
	if(pending_buildings.find(id)!=pending_buildings.end())
	{
		return true;
	}
	return false;
}



bool BuildingRegister::is_building_found(unsigned int id)
{
	if(found_buildings.find(id)!=found_buildings.end())
	{
		return true;
	}
	return false;
}




bool BuildingRegister::is_building_upgrading(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return false;
	}
	
	tribool v=found_buildings[id].get<4>();
	if(v)
		return true;
	else if(!v)
		return false;
	return true;
}



Building* BuildingRegister::get_building(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return NULL;
	}
	return player->team->myBuildings[::Building::GIDtoID(found_buildings[id].get<3>())];
}



BuildingType* BuildingRegister::get_building_type(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return NULL;
	}
	return player->team->myBuildings[::Building::GIDtoID(found_buildings[id].get<3>())]->type;
}



int BuildingRegister::get_type(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return found_buildings[id].get<2>();
}



int BuildingRegister::get_level(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return get_building(id)->type->level+1;
}



int BuildingRegister::get_assigned(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return get_building(id)->maxUnitWorking;
}



Condition* Condition::load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Condition");
	ConditionType type=static_cast<ConditionType>(stream->readUint32("type"));
	Condition* condition=NULL;
	switch(type)
	{
		case CParticularBuilding:
			condition=new ParticularBuilding;
			condition->load(stream, player, versionMinor);
		break;
		case CBuildingDestroyed:
			condition=new BuildingDestroyed;
			condition->load(stream, player, versionMinor);
		break;
		case CEnemyBuildingDestroyed:
			condition=new EnemyBuildingDestroyed;
			condition->load(stream, player, versionMinor);
		break;
		case CEitherCondition:
			condition=new EitherCondition;
			condition->load(stream, player, versionMinor);
		break;
		case CAllConditions:
			condition=new AllConditions;
			condition->load(stream, player, versionMinor);
		break;
		case CPopulation:
			condition=new Population;
			condition->load(stream, player, versionMinor);
		break;
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



ParticularBuilding::ParticularBuilding() : condition(NULL), id(-1)
{

}



ParticularBuilding::ParticularBuilding(BuildingCondition* condition, int id) : condition(condition), id(id)
{

}



ParticularBuilding::~ParticularBuilding()
{
	if(condition)
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



EitherCondition::EitherCondition()
{

}


AllConditions::AllConditions(Condition* a, Condition* b, Condition* c, Condition* d) : a(a), b(b), c(c), d(d)
{

}



AllConditions::~AllConditions()
{
	if(a)
		delete a;
	if(b)
		delete b;
	if(c)
		delete c;
	if(d)
		delete d;

}



boost::logic::tribool AllConditions::passes(Echo& echo)
{
	tribool a2=true;
	tribool b2=true;
	tribool c2=true;
	tribool d2=true;

	if(a)
		a2=a->passes(echo);
	if(b)
		b2=b->passes(echo);
	if(c)
		c2=c->passes(echo);
	if(d)
		d2=d->passes(echo);

	if(a2 && b2 && c2 && d2)
		return true;

	if(a2==indeterminate)
		return indeterminate;
	if(b2==indeterminate)
		return indeterminate;
	if(c2==indeterminate)
		return indeterminate;
	if(d2==indeterminate)
		return indeterminate;

	return false;
}



ConditionType AllConditions::get_type()
{
	return CAllConditions;
}



bool AllConditions::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("EitherCondition");
	bool condition_is_null=stream->readUint8("condition_is_null");
	if(condition_is_null)
		a=NULL;
	else
		a=Condition::load_condition(stream, player, versionMinor);

	condition_is_null=stream->readUint8("condition_is_null");
	if(condition_is_null)
		b=NULL;
	else
		b=Condition::load_condition(stream, player, versionMinor);

	condition_is_null=stream->readUint8("condition_is_null");
	if(condition_is_null)
		c=NULL;
	else
		c=Condition::load_condition(stream, player, versionMinor);

	condition_is_null=stream->readUint8("condition_is_null");
	if(condition_is_null)
		d=NULL;
	else
		d=Condition::load_condition(stream, player, versionMinor);

	stream->readLeaveSection();
	return true;
}



void AllConditions::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AllConditions");
	if(a)
	{
		stream->writeUint8(false, "condition_is_null");
		Condition::save_condition(a, stream);
	}
	else
		stream->writeUint8(true, "condition_is_null");

	if(b)
	{
		stream->writeUint8(false, "condition_is_null");
		Condition::save_condition(b, stream);
	}
	else
		stream->writeUint8(true, "condition_is_null");

	if(c)
	{
		stream->writeUint8(false, "condition_is_null");
		Condition::save_condition(c, stream);
	}
	else
		stream->writeUint8(true, "condition_is_null");

	if(d)
	{
		stream->writeUint8(false, "condition_is_null");
		Condition::save_condition(d, stream);
	}
	else
		stream->writeUint8(true, "condition_is_null");

	stream->writeLeaveSection();
}



AllConditions::AllConditions()
{

}



Population::Population(bool workers, bool explorers, bool warriors, int num, PopulationMethod method) : workers(workers), explorers(explorers), warriors(warriors), num(num), method(method)
{

}



Population::~Population()
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



Population::Population()
{

}



BuildingCondition* BuildingCondition::load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingCondition");
	BuildingConditionType type=static_cast<BuildingConditionType>(stream->readUint32("type"));
	BuildingCondition* condition=NULL;
	switch(type)
	{
		case CNotUnderConstruction:
			condition=new NotUnderConstruction;
			condition->load(stream, player, versionMinor);
		break;
		case CUnderConstruction:
			condition=new UnderConstruction;
			condition->load(stream, player, versionMinor);
		break;
		case CBeingUpgraded:
			condition=new BeingUpgraded;
			condition->load(stream, player, versionMinor);
		break;
		case CBeingUpgradedTo:
			condition=new BeingUpgradedTo;
			condition->load(stream, player, versionMinor);
		break;
		case CSpecificBuildingType:
			condition=new SpecificBuildingType;
			condition->load(stream, player, versionMinor);
		break;
		case CNotSpecificBuildingType:
			condition=new NotSpecificBuildingType;
			condition->load(stream, player, versionMinor);
		break;
		case CBuildingLevel:
			condition=new BuildingLevel;
			condition->load(stream, player, versionMinor);
		break;
		case CUpgradable:
			condition=new Upgradable;
			condition->load(stream, player, versionMinor);
		break;
		case CRessourceTrackerAmount:
			condition=new RessourceTrackerAmount;
			condition->load(stream, player, versionMinor);
		break;
		case CRessourceTrackerAge:
			condition=new RessourceTrackerAge;
			condition->load(stream, player, versionMinor);
		break;
		case CTicksPassed:
			condition=new TicksPassed;
			condition->load(stream, player, versionMinor);
		break;

	}
	stream->readLeaveSection();
	return condition;
}



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



bool NotUnderConstruction::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("NotUnderConstruction");
	stream->readLeaveSection();
	return true;
}



void NotUnderConstruction::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("NotUnderConstruction");
	stream->writeLeaveSection();

}



bool UnderConstruction::passes(Echo& echo, int id)
{
	Building* building = echo.get_building_register().get_building(id);
	return building->constructionResultState!=::Building::NO_CONSTRUCTION && building->buildingState==Building::ALIVE;
}



bool UnderConstruction::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("UnderConstruction");
	stream->readLeaveSection();
	return true;
}



void UnderConstruction::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("UnderConstruction");
	stream->writeLeaveSection();
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

bool BeingUpgraded::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BeingUpgraded");
	stream->readLeaveSection();
	return true;
}



void BeingUpgraded::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BeingUpgraded");
	stream->writeLeaveSection();
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
		if(b->type->level==(level-1))
		{
			return true;
		}
	}
	else if(b->type->level==(level-2))
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
	if(building->type->level==building_level-1)
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
	   building->type->level!=2 &&
	   building->isHardSpaceForBuildingSite(Building::UPGRADE) &&
	   building->hp == building->type->hpMax
	    )
		return true;
	return false;
}



bool Upgradable::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Upgradable");
	stream->readLeaveSection();
	return true;
}



void Upgradable::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Upgradable");
	stream->writeLeaveSection();
}



RessourceTrackerAmount::RessourceTrackerAmount(int amount, TrackerMethod tracker_method) : amount(amount), tracker_method(tracker_method)
{

}



RessourceTrackerAmount::RessourceTrackerAmount()
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



RessourceTrackerAge::RessourceTrackerAge()
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



bool ManagementOrder::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	stream->readEnterSection("conditions");
	Uint32 size = stream->readUint32("size");
	conditions.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		conditions[x] = boost::shared_ptr<Condition>(Condition::load_condition(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void ManagementOrder::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ManagementOrder");
	stream->writeEnterSection("conditions");
	stream->writeUint32(conditions.size(), "size");
	for(unsigned x=0; x<conditions.size(); ++x)
	{
		stream->writeEnterSection(x);
		Condition::save_condition(conditions[x].get(), stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void ManagementOrder::add_condition(Condition* condition)
{
	conditions.push_back(boost::shared_ptr<Condition>(condition));
}



boost::logic::tribool ManagementOrder::passes_conditions(Echo& echo)
{
	for(unsigned int i=0; i<conditions.size(); ++i)
	{
		boost::logic::tribool passes=conditions[i]->passes(echo);
		if(passes)
			continue;
		else if(!passes)
			return false;
		else
			return indeterminate;

	}

	boost::logic::tribool passes=wait(echo);
	if(passes)
		return true;
	if(!passes)
		return false;
	else
		return indeterminate;

	return true;
}



ManagementOrder* ManagementOrder::load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	ManagementOrderType mot=static_cast<ManagementOrderType>(stream->readUint32("type"));
	ManagementOrder* mo=NULL;
	switch(mot)
	{
		case MAssignWorkers:
			mo=new AssignWorkers;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeSwarm:
			mo=new ChangeSwarm;
			mo->load(stream, player, versionMinor);
			break;
		case MDestroyBuilding:
			mo=new DestroyBuilding;
			mo->load(stream, player, versionMinor);
			break;
		case MAddRessourceTracker:
			mo=new AddRessourceTracker;
			mo->load(stream, player, versionMinor);
			break;
		case MPauseRessourceTracker:
			mo=new PauseRessourceTracker;
			mo->load(stream, player, versionMinor);
			break;
		case MUnPauseRessourceTracker:
			mo=new UnPauseRessourceTracker;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeFlagSize:
			mo=new ChangeFlagSize;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeFlagMinimumLevel:
			mo=new ChangeFlagMinimumLevel;
			mo->load(stream, player, versionMinor);
			break;
		case MAddArea:
			mo=new AddArea;
			mo->load(stream, player, versionMinor);
			break;
		case MRemoveArea:
			mo=new RemoveArea;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeAlliances:
			mo=new ChangeAlliances;
			mo->load(stream, player, versionMinor);
			break;
		case MUpgradeRepair:
			mo=new UpgradeRepair;
			mo->load(stream, player, versionMinor);
			break;
		case MSendMessage:
			mo=new SendMessage;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeFlagPosition:
			mo=new ChangeFlagPosition;
			mo->load(stream, player, versionMinor);
			break;
		case MAdjustPriority:
			mo=new AdjustPriority;
			mo->load(stream, player, versionMinor);
			break;
	}
	return mo;
}



void ManagementOrder::save_order(ManagementOrder* mo, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ManagementOrder");
	stream->writeUint32(mo->get_type(), "type");
	mo->save(stream);
	stream->writeLeaveSection();
}



AssignWorkers::AssignWorkers(int number_of_workers, int building_id) : number_of_workers(number_of_workers), building_id(building_id)
{

}


void AssignWorkers::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderModifyBuilding(echo.get_building_register().get_building(building_id)->gid, number_of_workers)));
}



boost::logic::tribool AssignWorkers::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool AssignWorkers::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AssignWorkers");
	ManagementOrder::load(stream, player, versionMinor);
	number_of_workers=stream->readUint32("number_of_workers");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void AssignWorkers::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AssignWorkers");
	ManagementOrder::save(stream);
	stream->writeUint32(number_of_workers, "number_of_workers");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeSwarm::ChangeSwarm(int worker_ratio, int explorer_ratio, int warrior_ratio, int building_id) : worker_ratio(worker_ratio), explorer_ratio(explorer_ratio), warrior_ratio(warrior_ratio), building_id(building_id)
{

}


void ChangeSwarm::modify(Echo& echo)
{
	Sint32 ratio[NB_UNIT_TYPE];
	ratio[0]=worker_ratio;
	ratio[1]=explorer_ratio;
	ratio[2]=warrior_ratio;
	echo.push_order(shared_ptr<Order>(new OrderModifySwarm(echo.get_building_register().get_building(building_id)->gid, ratio)));
}



boost::logic::tribool ChangeSwarm::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool ChangeSwarm::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeSwarm");
	ManagementOrder::load(stream, player, versionMinor);
	worker_ratio=stream->readUint32("worker_ratio");
	explorer_ratio=stream->readUint32("explorer_ratio");
	warrior_ratio=stream->readUint32("warrior_ratio");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;

}



void ChangeSwarm::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeSwarm");
	ManagementOrder::save(stream);
	stream->writeUint32(worker_ratio, "worker_ratio");
	stream->writeUint32(explorer_ratio, "explorer_ratio");
	stream->writeUint32(warrior_ratio, "warrior_ratio");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



DestroyBuilding::DestroyBuilding(int building_id) : building_id(building_id)
{

}



void DestroyBuilding::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderDelete(echo.get_building_register().get_building(building_id)->gid)));
}



boost::logic::tribool DestroyBuilding::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool DestroyBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DestroyBuilding");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void DestroyBuilding::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("DestroyBuilding");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



RessourceTracker::RessourceTracker(Echo& echo, int building_id, int length, int ressource) : record(length, 0), position(0), timer(0), length(length), echo(echo), building_id(building_id), ressource(ressource)
{

}



void RessourceTracker::tick()
{
	timer++;
	if((timer%10)==0)
	{
		Building* b = echo.get_building_register().get_building(building_id);
		record[position]=b->ressources[ressource];
		position++;
		if(position>=record.size())
			position=0;
	}
}


int RessourceTracker::get_total_level()
{
	int sum=0;
	for(unsigned int n=0; n<record.size(); ++n)
	{
		sum+=record[n];
	}
	return sum;
}



bool RessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RessourceTracker");
	stream->readEnterSection("record");
	Uint32 recordsize=stream->readUint32("size");
	record.resize(recordsize);
	for(unsigned int record_index=0; record_index<recordsize; ++record_index)
	{
		stream->readEnterSection(record_index);
		record[record_index]=stream->readUint32("quantity_of_ressources");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	position=stream->readUint32("position");
	timer=stream->readUint32("timer");
	building_id=stream->readUint32("building_id");
	length=stream->readUint32("length");
	ressource=stream->readUint32("ressource");
	stream->readLeaveSection();
	return true;
}



void RessourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RessourceTracker");
	stream->writeEnterSection("record");
	stream->writeUint32(record.size(), "size");
	for(unsigned int record_index=0; record_index<record.size(); ++record_index)
	{
		stream->writeEnterSection(record_index);
		stream->writeUint32(record[record_index], "quantity_of_ressources");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint32(position, "position");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(building_id, "building_id");
	stream->writeUint32(length, "length");
	stream->writeUint32(ressource, "ressource");
	stream->writeLeaveSection();
}



AddRessourceTracker::AddRessourceTracker(int length, int ressource, int building_id) : length(length), building_id(building_id), ressource(ressource)
{
	
}



void AddRessourceTracker::modify(Echo& echo)
{
	echo.add_ressource_tracker(new RessourceTracker(echo, building_id, length, ressource), building_id);
}



boost::logic::tribool AddRessourceTracker::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool AddRessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	length=stream->readUint32("length");
	building_id=stream->readUint32("building_id");
	ressource=stream->readUint32("ressource");
	stream->readLeaveSection();
	return true;
}



void AddRessourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AddRessourceTracker");
	ManagementOrder::save(stream);
	stream->writeUint32(length, "length");
	stream->writeUint32(building_id, "building_id");
	stream->writeUint32(ressource, "ressource");
	stream->writeLeaveSection();
}



PauseRessourceTracker::PauseRessourceTracker(int building_id) : building_id(building_id)
{

}



void PauseRessourceTracker::modify(Echo& echo)
{
	echo.pause_ressource_tracker(building_id);
}



boost::logic::tribool PauseRessourceTracker::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool PauseRessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("PauseRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void PauseRessourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("PauseRessourceTracker");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



UnPauseRessourceTracker::UnPauseRessourceTracker(int building_id) : building_id(building_id)
{

}



void UnPauseRessourceTracker::modify(Echo& echo)
{
	echo.unpause_ressource_tracker(building_id);
}



boost::logic::tribool UnPauseRessourceTracker::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool UnPauseRessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("UnPauseRessourceTracker");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void UnPauseRessourceTracker::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("UnPauseRessourceTracker");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeFlagSize::ChangeFlagSize(int size, int building_id) : size(size), building_id(building_id)
{

}



void ChangeFlagSize::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderModifyFlag(echo.get_building_register().get_building(building_id)->gid, size)));
}



boost::logic::tribool ChangeFlagSize::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
	{
		return true;
	}
	else if(echo.get_building_register().is_building_pending(building_id))
	{
		return false;
	}
	else
	{
		return indeterminate;
	}
}



bool ChangeFlagSize::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagSize");
	ManagementOrder::load(stream, player, versionMinor);
	size=stream->readUint32("size");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagSize::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagSize");
	ManagementOrder::save(stream);
	stream->writeUint32(size, "size");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeFlagMinimumLevel::ChangeFlagMinimumLevel(int minimum_level, int building_id) : minimum_level(minimum_level), building_id(building_id)
{

}



void ChangeFlagMinimumLevel::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderModifyMinLevelToFlag(echo.get_building_register().get_building(building_id)->gid, minimum_level-1)));
}



boost::logic::tribool ChangeFlagMinimumLevel::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



bool ChangeFlagMinimumLevel::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagMinimumLevel");
	ManagementOrder::load(stream, player, versionMinor);
	minimum_level=stream->readUint32("minimum_level");
	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagMinimumLevel::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagMinimumLevel");
	ManagementOrder::save(stream);
	stream->writeUint32(minimum_level, "minimum_level");
	stream->writeUint32(building_id, "building_id");
	stream->writeLeaveSection();
}



ChangeFlagPosition::ChangeFlagPosition(int x, int y, int building_id)
	: x(x), y(y), building_id(building_id)
{

}


void ChangeFlagPosition::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderMoveFlag(echo.get_building_register().get_building(building_id)->gid, x, y, true)));
}



boost::logic::tribool ChangeFlagPosition::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



ManagementOrderType ChangeFlagPosition::get_type()
{
	return MChangeFlagPosition;
}



bool ChangeFlagPosition::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagPosition");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	x=stream->readUint32("x");
	y=stream->readUint32("y");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagPosition::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagPosition");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	stream->writeUint32(x, "x");
	stream->writeUint32(y, "y");
	stream->writeLeaveSection();
}



AdjustPriority::AdjustPriority(int building_id, AdjustPriority::BuildingPriority priority)
	: building_id(building_id), priority(priority)
{

}


void AdjustPriority::modify(Echo& echo)
{
	int p=0;
	if(priority == Low)
		p=-1;
	else if(priority == Medium)
		p=0;
	else if(priority == High)
		p=1;
	echo.push_order(shared_ptr<Order>(new OrderChangePriority(echo.get_building_register().get_building(building_id)->gid, p)));
}



boost::logic::tribool AdjustPriority::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(building_id))
		return true;
	else if(echo.get_building_register().is_building_pending(building_id))
		return false;
	else
		return indeterminate;
}



ManagementOrderType AdjustPriority::get_type()
{
	return MAdjustPriority;
}



bool AdjustPriority::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AdjustPriority");
	ManagementOrder::load(stream, player, versionMinor);
	building_id=stream->readUint32("building_id");
	int p = stream->readSint32("p");
	if(p==-1)
		priority = Low;
	else if(p==0)
		priority = Medium;
	else if(p==1)
		priority = High;
	stream->readLeaveSection();
	return true;
}



void AdjustPriority::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AdjustPriority");
	ManagementOrder::save(stream);
	stream->writeUint32(building_id, "building_id");
	int p=0;
	if(priority == Low)
		p=-1;
	else if(priority == Medium)
		p=0;
	else if(priority == High)
		p=1;
	stream->writeSint32(p, "priority");
	stream->writeLeaveSection();
}




AddArea::AddArea(AreaType areatype) : areatype(areatype)
{

}



void AddArea::add_location(int x, int y)
{
	locations.push_back(position(x, y));
}



void AddArea::modify(Echo& echo)
{
	BrushAccumulator acc;
	for(std::vector<position>::iterator i=locations.begin(); i!=locations.end(); ++i)
	{
		acc.applyBrush(BrushApplication(echo.player->map->normalizeX(i->x), echo.player->map->normalizeY(i->y), 0), echo.player->map);
	}
	if(acc.getApplicationCount()>0)
	{
		switch(areatype)
		{
			case ClearingArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateClearArea(echo.player->team->teamNumber, BrushTool::MODE_ADD, &acc, echo.player->map)));
				break;
			case ForbiddenArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateForbidden(echo.player->team->teamNumber, BrushTool::MODE_ADD, &acc, echo.player->map)));
				break;
			case GuardArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateGuardArea(echo.player->team->teamNumber, BrushTool::MODE_ADD, &acc, echo.player->map)));
				break;
		}
	}
}



boost::logic::tribool AddArea::wait(Echo& echo)
{
	return true;
}



bool AddArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddArea");
	ManagementOrder::load(stream, player, versionMinor);
	areatype=static_cast<AreaType>(stream->readUint32("area_type"));
	stream->readEnterSection("locations");
	Uint32 size=stream->readUint32("size");
	locations.resize(size);
	for(Uint32 location_index=0; location_index<size; ++location_index)
	{
		stream->readEnterSection(location_index);
		locations[location_index]=position(stream->readUint32("posx"), stream->readUint32("posy"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void AddArea::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AddArea");
	ManagementOrder::save(stream);
	stream->writeUint32(areatype, "area_type");
	stream->writeEnterSection("locations");
	stream->writeUint32(locations.size(), "size");
	for(Uint32 location_index=0; location_index<locations.size(); ++location_index)
	{
		stream->writeEnterSection(location_index);
		stream->writeUint32(locations[location_index].x, "posx");
		stream->writeUint32(locations[location_index].y, "posy");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



RemoveArea::RemoveArea(AreaType areatype) : areatype(areatype)
{

}



void RemoveArea::add_location(int x, int y)
{
	locations.push_back(position(x, y));
}



void RemoveArea::modify(Echo& echo)
{
	BrushAccumulator acc;
	for(std::vector<position>::iterator i=locations.begin(); i!=locations.end(); ++i)
	{
		acc.applyBrush(BrushApplication(echo.player->map->normalizeX(i->x), echo.player->map->normalizeY(i->y), 0), echo.player->map);
	}
	if(acc.getApplicationCount()>0)
	{
		switch(areatype)
		{
			case ClearingArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateClearArea(echo.player->team->teamNumber, BrushTool::MODE_DEL, &acc, echo.player->map)));
				break;
			case ForbiddenArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateForbidden(echo.player->team->teamNumber, BrushTool::MODE_DEL, &acc, echo.player->map)));
				break;
			case GuardArea:
				echo.push_order(shared_ptr<Order>(new OrderAlterateGuardArea(echo.player->team->teamNumber, BrushTool::MODE_DEL, &acc, echo.player->map)));
				break;
		}
	}
}



boost::logic::tribool RemoveArea::wait(Echo& echo)
{
	return true;
}



bool RemoveArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RemoveArea");
	ManagementOrder::load(stream, player, versionMinor);
	areatype=static_cast<AreaType>(stream->readUint32("area_type"));
	stream->readEnterSection("locations");
	Uint32 size=stream->readUint32("size");
	locations.resize(size);
	for(Uint32 location_index=0; location_index<size; ++location_index)
	{
		stream->readEnterSection(location_index);
		locations[location_index]=position(stream->readUint32("posx"), stream->readUint32("posy"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}



void RemoveArea::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("RemoveArea");
	ManagementOrder::save(stream);
	stream->writeUint32(areatype, "area_type");
	stream->writeEnterSection("locations");
	stream->writeUint32(locations.size(), "size");
	for(Uint32 location_index=0; location_index<locations.size(); ++location_index)
	{
		stream->writeEnterSection(location_index);
		stream->writeUint32(locations[location_index].x, "posx");
		stream->writeUint32(locations[location_index].y, "posy");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}


ChangeAlliances::ChangeAlliances(int team, boost::logic::tribool is_allied, boost::logic::tribool is_enemy, boost::logic::tribool view_market, boost::logic::tribool view_inn, boost::logic::tribool view_other) : team(team), is_allied(is_allied), is_enemy(is_enemy), view_market(view_market), view_inn(view_inn), view_other(view_other)
{

}



void ChangeAlliances::modify(Echo& echo)
{
	Uint32 alliedmask=echo.allies;
	Uint32 enemymask=echo.enemies;
	Uint32 market_mask=echo.market_view;
	Uint32 inn_mask=echo.inn_view;
	Uint32 other_mask=echo.other_view;
	Team* t=echo.player->game->teams[team];
	if(is_allied)
		alliedmask|=t->me;
	else if(!is_allied)
		if(alliedmask&t->me)
			alliedmask^=t->me;

	if(is_enemy)
		enemymask|=t->me;
	else if(!is_enemy)
		if(enemymask&t->me)
			enemymask^=t->me;

	if(view_market)
		market_mask|=t->me;
	else if(!view_market)
		if(market_mask&t->me)
			market_mask^=t->me;

	if(view_inn)
		inn_mask|=t->me;
	else if(!view_inn)
		if(inn_mask&t->me)
			inn_mask^=t->me;

	if(view_other)
		other_mask|=t->me;
	else if(!view_other)
		if(other_mask&t->me)
			other_mask^=t->me;

	echo.allies=alliedmask;
	echo.enemies=enemymask;
	echo.market_view=market_mask;
	echo.inn_view=inn_mask;
	echo.other_view=other_mask;

	echo.push_order(shared_ptr<Order>(new SetAllianceOrder(echo.player->team->teamNumber, alliedmask, enemymask, market_mask, inn_mask, other_mask)));
}



boost::logic::tribool ChangeAlliances::wait(Echo& echo)
{
	return true;
}



bool ChangeAlliances::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeAlliances");
	ManagementOrder::load(stream, player, versionMinor);
	team=stream->readUint32("team");

	Uint8 tmp=stream->readUint8("is_allied");
	if(tmp==1)
		is_allied=true;
	else if(tmp==0)
		is_allied=false;
	else if(tmp==2)
		is_allied=indeterminate;

	tmp=stream->readUint8("is_enemy");
	if(tmp==1)
		is_enemy=true;
	else if(tmp==0)
		is_enemy=false;
	else if(tmp==2)
		is_enemy=indeterminate;

	tmp=stream->readUint8("view_market");
	if(tmp==1)
		view_market=true;
	else if(tmp==0)
		view_market=false;
	else if(tmp==2)
		view_market=indeterminate;

	tmp=stream->readUint8("view_inn");
	if(tmp==1)
		view_inn=true;
	else if(tmp==0)
		view_inn=false;
	else if(tmp==2)
		view_inn=indeterminate;

	tmp=stream->readUint8("view_other");
	if(tmp==1)
		view_other=true;
	else if(tmp==0)
		view_other=false;
	else if(tmp==2)
		view_other=indeterminate;

	return true;
}



void ChangeAlliances::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeAlliances");
	ManagementOrder::save(stream);
	stream->writeUint32(team, "team");

	if(is_allied)
		stream->writeUint8(1, "is_allied");
	else if(!is_allied)
		stream->writeUint8(0, "is_allied");
	else
		stream->writeUint8(2, "is_allied");

	if(is_enemy)
		stream->writeUint8(1, "is_enemy");
	else if(!is_enemy)
		stream->writeUint8(0, "is_enemy");
	else
		stream->writeUint8(2, "is_enemy");

	if(view_market)
		stream->writeUint8(1, "view_market");
	else if(!view_market)
		stream->writeUint8(0, "view_market");
	else
		stream->writeUint8(2, "view_market");

	if(view_inn)
		stream->writeUint8(1, "view_inn");
	else if(!view_inn)
		stream->writeUint8(0, "view_inn");
	else
		stream->writeUint8(2, "view_inn");

	if(view_other)
		stream->writeUint8(1, "view_other");
	else if(!view_other)
		stream->writeUint8(0, "view_other");
	else
		stream->writeUint8(2, "view_other");

	stream->writeLeaveSection();
}

UpgradeRepair::UpgradeRepair(int id) : id(id)
{

}



void UpgradeRepair::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderConstruction(echo.get_building_register().get_building(id)->gid,1,1)));
	echo.get_building_register().set_upgrading(id);
}



boost::logic::tribool UpgradeRepair::wait(Echo& echo)
{
	if(echo.get_building_register().is_building_found(id))
		return true;
	else if(echo.get_building_register().is_building_pending(id))
		return false;
	else
		return indeterminate;
}



ManagementOrderType UpgradeRepair::get_type()
{
	return MUpgradeRepair;
}



bool UpgradeRepair::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("UpgradeRepair");
	ManagementOrder::load(stream, player, versionMinor);
	id=stream->readUint32("id");
	stream->readLeaveSection();
	return true;
}



void UpgradeRepair::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("UpgradeRepair");
	ManagementOrder::save(stream);
	stream->writeUint32(id, "id");
	stream->writeLeaveSection();
}


SendMessage::SendMessage(const std::string& message) : message(message)
{

}



void SendMessage::modify(Echo& echo)
{
	echo.echoai->handle_message(echo, message);
}



boost::logic::tribool SendMessage::wait(Echo& echo)
{
	return true;
}



ManagementOrderType SendMessage::get_type()
{
	return MSendMessage;
}



bool SendMessage::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("SendMessage");
	ManagementOrder::load(stream, player, versionMinor);
	message=stream->readText("message");
	stream->readLeaveSection();
	return true;
}



void SendMessage::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("SendMessage");
	ManagementOrder::save(stream);
	stream->writeText(message, "message");
	stream->writeLeaveSection();
}



building_search_iterator::building_search_iterator() : found_id(-1), is_end(true), search(NULL)
{

}


building_search_iterator::building_search_iterator(BuildingSearch& search) : found_id(-1), is_end(false), search(&search)
{
	set_to_next();
}



const unsigned int building_search_iterator::operator*()
{
	return found_id;
}



building_search_iterator& building_search_iterator::operator++()
{
	set_to_next();
	return *this;
}



building_search_iterator building_search_iterator::operator++(int)
{
	building_search_iterator copy(*this);
	set_to_next();
	return copy;
}



bool building_search_iterator::operator!=(const building_search_iterator& rhs) const
{
	if(is_end==rhs.is_end)
		return false;
	return is_end!=rhs.is_end || position!=rhs.position || found_id!=rhs.found_id ;
}



void building_search_iterator::set_to_next()
{
	Construction::BuildingRegister::found_iterator positionSaved = position;
	if(is_end)
		return;
	if(found_id==-1)
	{
		position=search->echo.get_building_register().begin();
	}
	else
		position++;
	for(; position!=search->echo.get_building_register().end() && !search->passes_conditions(position->first); position++)
	{
	}
	if(position==search->echo.get_building_register().end())
	{
		is_end=true;
		return;
	}
	if(position->first==-1 && positionSaved==position)
	{                        // This fixes an infinit loop.
		is_end=true;     // In some special cases the program Logic 
		return;          // must have been wrong.
	}
	found_id=position->first;
}



BuildingSearch::BuildingSearch(Echo& echo) : echo(echo)
{

}



void BuildingSearch::add_condition(Conditions::BuildingCondition* condition)
{
	conditions.push_back(boost::shared_ptr<Conditions::BuildingCondition>(condition));
}



int BuildingSearch::count_buildings()
{
	int count=0;
	for(Construction::BuildingRegister::found_iterator i=echo.get_building_register().begin(); i!=echo.get_building_register().end(); ++i)
	{
		if(passes_conditions(i->first))
		{
			count++;
		}
	}
	return count;
}



building_search_iterator BuildingSearch::begin()
{
	return building_search_iterator(*this);
}



building_search_iterator BuildingSearch::end()
{
	return building_search_iterator();
}



bool BuildingSearch::passes_conditions(int b)
{
	for(std::vector<boost::shared_ptr<Conditions::BuildingCondition> >::iterator i = conditions.begin();  i!=conditions.end(); ++i)
	{
		if(!(*i)->passes(echo, b))
			return false;
	}
	return true;
}


enemy_team_iterator::enemy_team_iterator(Echo& echo) :  team_number(-1), is_end(false), echo(&echo)
{
	set_to_next();
}


enemy_team_iterator::enemy_team_iterator() : team_number(-1), is_end(true), echo(NULL)
{

}


const unsigned int enemy_team_iterator::operator*()
{
	return team_number;
}


enemy_team_iterator& enemy_team_iterator::operator++()
{
	set_to_next();
	return *this;
}


enemy_team_iterator enemy_team_iterator::operator++(int)
{
	enemy_team_iterator copy(*this);
	set_to_next();
	return copy;
}


bool enemy_team_iterator::operator!=(const enemy_team_iterator& rhs) const
{
	if(rhs.is_end && is_end)
		return false;
	return rhs.is_end != is_end || rhs.team_number!=team_number;
}


void enemy_team_iterator::set_to_next()
{
	if(is_end)
		return;
	if(team_number==-1)
	{
		team_number=0;
	}
	else
		team_number++;
	for(; echo->player->team->game->teams[team_number]!=NULL && !(echo->player->team->enemies & echo->player->team->game->teams[team_number]->me); team_number++)
	{
	}

	if(echo->player->team->game->teams[team_number]==NULL)
	{
		is_end=true;
		return;
	}

}


int SearchTools::is_flag(Echo& echo, int x, int y)
{
	Building** buildings=echo.player->team->myBuildings;
	for(int n=0; n<Building::MAX_COUNT; ++n)
	{
		Building* b=buildings[n];
		if(b)
		{
			if(b->posX==x && b->posY==y)
			{
				if(b->type->shortTypeNum > (int)(IntBuildingType::DEFENSE_BUILDING) && b->type->shortTypeNum < (int)(IntBuildingType::STONE_WALL))
				{
					return b->gid;
				}
			}
		}
	}
	return NOGBID;
}




enemy_building_iterator::enemy_building_iterator() : is_end(true)
{

}



enemy_building_iterator::enemy_building_iterator(Echo& echo, int team, int building_type, int level, boost::logic::tribool construction_site) : current_gid(-1), team(team), building_type(building_type), level(level), construction_site(construction_site), is_end(false), echo(&echo)
{
	set_to_next();
}



const unsigned int enemy_building_iterator::operator*()
{
	return current_gid;
}



enemy_building_iterator& enemy_building_iterator::operator++()
{
	set_to_next();
	return *this;
}



enemy_building_iterator enemy_building_iterator::operator++(int)
{
	enemy_building_iterator copy;
	set_to_next();
	return copy;
}



bool enemy_building_iterator::operator!=(const enemy_building_iterator& rhs) const
{
	if(is_end && rhs.is_end)
		return false;
	return is_end!=rhs.is_end || team!=rhs.team || building_type!=rhs.building_type || level!=rhs.level || construction_site!=rhs.construction_site;
}



void enemy_building_iterator::set_to_next()
{
	if(current_gid==-1)
	{
		current_index=0;
	}
	else
		current_index++;

	while(current_index<Building::MAX_COUNT)
	{
		Building* b=echo->player->game->teams[team]->myBuildings[current_index];
		if(b)
		{
			if( (b->seenByMask&echo->player->team->me
                             // Don't allow AIs to cheat!!!!!!
                             // || echo->get_starting_buildings().find(b->gid)!=echo->get_starting_buildings().end()
                             ) &&
				(building_type==-1 || b->type->shortTypeNum==building_type) &&
				(level==-1 || b->type->level==(level-1)))
			{
				if(construction_site)
				{
					if(b->type->isBuildingSite)
					{
						current_gid=b->gid;
						break;
					}
				}
				else if(!construction_site)
				{
					if(!b->type->isBuildingSite)
					{
						current_gid=b->gid;
						break;
					}
				}
				else
				{
					current_gid=b->gid;
					break;
				}
			}
		}
		current_index++;
	}

	if(current_index==Building::MAX_COUNT)
		is_end=true;
}




MapInfo::MapInfo(Echo& echo) : echo(echo)
{

}



int MapInfo::get_width()
{
	return echo.player->map->getW();
}



int MapInfo::get_height()
{
	return echo.player->map->getH();
}



bool MapInfo::is_forbidden_area(int x, int y)
{
	return echo.player->map->isForbidden(x, y, echo.player->team->me);
}



bool MapInfo::is_guard_area(int x, int y)
{
	return echo.player->map->isGuardArea(x, y, echo.player->team->me);
}



bool MapInfo::is_clearing_area(int x, int y)
{
	return echo.player->map->isClearArea(x, y, echo.player->team->me);
}



bool MapInfo::is_discovered(int x, int y)
{
	return echo.player->map->isMapDiscovered(x, y, echo.player->team->me);
}



bool MapInfo::is_ressource(int x, int y, int type)
{
	return echo.player->map->isRessourceTakeable(x, y, type);
}



bool MapInfo::is_water(int x, int y)
{
	return echo.player->map->isWater(x, y);
}



bool MapInfo::backs_onto_sand(int x, int y)
{
	if(echo.player->map->hasSand(x-1, y))
		return true;
	if(echo.player->map->hasSand(x+1, y))
		return true;
	if(echo.player->map->hasSand(x-1, y-1))
		return true;
	if(echo.player->map->hasSand(x, y-1))
		return true;
	if(echo.player->map->hasSand(x+1, y-1))
		return true;
	if(echo.player->map->hasSand(x-1, y+1))
		return true;
	if(echo.player->map->hasSand(x, y+1))
		return true;
	if(echo.player->map->hasSand(x+1, y+1))
		return true;
	return false;
}



int MapInfo::get_ammount_ressource(int x, int y)
{
	return echo.player->map->getRessource(x, y).amount;
}



Echo::Echo(EchoAI* echoai, Player* player) : player(player), echoai(echoai), gm(), br(player, *this), fm(*this), timer(0)
{
	previous_building_id=-1;
	from_load_timer=0;
	is_fruit=false;
}


unsigned int Echo::add_building_order(Construction::BuildingOrder* bo)
{
	building_orders.push_back(boost::shared_ptr<Construction::BuildingOrder>(bo));
	bo->queue_gradients(get_gradient_manager());
	unsigned int id=br.register_building();
	bo->id=id;
	return id;
}


void Echo::add_management_order(Management::ManagementOrder* mo)
{
	management_orders.push_back(boost::shared_ptr<Management::ManagementOrder>(mo));
}


void Echo::update_management_orders()
{
	for(std::vector<boost::shared_ptr<Management::ManagementOrder> >::iterator i=management_orders.begin(); i!=management_orders.end();)
	{
		boost::logic::tribool passes=(*i)->passes_conditions(*this);
		if(passes)
		{
			size_t pos = i - management_orders.begin();
			(*i)->modify(*this);
			management_orders.erase(management_orders.begin() + pos);
			i = management_orders.begin() + pos;
			continue;
		}
		else if(!passes)
		{
		}
		else
		{
			size_t pos = i - management_orders.begin();
			management_orders.erase(i);
			i = management_orders.begin() + pos;
			continue;
		}
		++i;
	}
}



void Echo::add_ressource_tracker(Management::RessourceTracker* rt, int building_id)
{
	ressource_trackers[building_id]=boost::make_tuple(boost::shared_ptr<RessourceTracker>(rt), true);
}



boost::shared_ptr<Management::RessourceTracker> Echo::get_ressource_tracker(int building_id)
{
	if(ressource_trackers.find(building_id)==ressource_trackers.end())
		return boost::shared_ptr<Management::RessourceTracker>();
	return ressource_trackers[building_id].get<0>();
}



void Echo::pause_ressource_tracker(int building_id)
{
	ressource_trackers[building_id].get<1>()=false;
}



void Echo::unpause_ressource_tracker(int building_id)
{
	ressource_trackers[building_id].get<1>()=true;
}



void Echo::update_ressource_trackers()
{
	for(std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> >::iterator i = ressource_trackers.begin(); i!=ressource_trackers.end();)
	{
		if(!br.is_building_found(i->first) && !br.is_building_pending(i->first))
		{
			std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> >::iterator current=i;
			++i;
			ressource_trackers.erase(current);
			continue;
		}
		else if(br.is_building_found(i->first))
		{
			if(i->second.get<1>())
				i->second.get<0>()->tick();
		}
		++i;
	}
}



void Echo::update_building_orders()
{
	for(std::vector<boost::shared_ptr<Construction::BuildingOrder> >::iterator i=building_orders.begin(); i!=building_orders.end();)
	{
		boost::logic::tribool passes=(*i)->passes_conditions(*this);
		if(passes)
		{
			if(!(previous_building_id==-1 || br.is_building_found(previous_building_id) || !br.is_building_pending(previous_building_id)))
				break;
			position p=(*i)->find_location(*this, player->map, *gm);
			if(p.x != 0 || p.y != 0)
			{
				br.issue_order((*i)->id, p.x, p.y, (*i)->get_building_type());
				Sint32 type=-1;
				if((*i)->get_building_type()>IntBuildingType::DEFENSE_BUILDING && (*i)->get_building_type() <IntBuildingType::STONE_WALL)
				{
					type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[(*i)->get_building_type()], 0, false);
					ManagementOrder* mo_flag=new AssignWorkers((*i)->get_number_of_workers(), (*i)->id);
					add_management_order(mo_flag);
				}
				else
				{
					type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[(*i)->get_building_type()], 0, true);
					ManagementOrder* mo_during_construction=new AssignWorkers((*i)->get_number_of_workers(), (*i)->id);
					mo_during_construction->add_condition(new ParticularBuilding(new UnderConstruction, (*i)->id));
					add_management_order(mo_during_construction);
				}
				orders.push_back(shared_ptr<Order>(new OrderCreate(player->team->teamNumber, p.x, p.y, type, 1, 1)));
				previous_building_id=(*i)->id;
				i=building_orders.erase(i);
				break;
			}
			else
			{
				br.remove_building((*i)->id);
				i=building_orders.erase(i);
				continue;
			}
		}
		else if(!passes)
		{
		}
		else
		{
			br.remove_building((*i)->id);
			i=building_orders.erase(i);
			continue;
		}
		++i;
	}
}



void Echo::init_starting_buildings()
{
	for(int t=0; t<Team::MAX_COUNT; ++t)
	{
		if(player->game->teams[t])
		{
			for(int bu=0; bu<Building::MAX_COUNT; ++bu)
			{
				Building* b=player->game->teams[t]->myBuildings[bu];
				if(b)
				{
					starting_buildings.insert(b->gid);
				}
			}
		}
	}
}

void Echo::check_fruit()
{
	MapInfo mi(*this);
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(mi.is_ressource(x, y, CHERRY))
				is_fruit=true;
			if(mi.is_ressource(x, y, ORANGE))
				is_fruit=true;
			if(mi.is_ressource(x, y, PRUNE))
				is_fruit=true;
			if(is_fruit)
				return;
		}
	}
}

bool Echo::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("EchoAI");
	signature_check(stream, player, versionMinor);

	stream->readEnterSection("orders");
	Uint32 ordersSize = stream->readUint32("size");
	for (Uint32 ordersIndex = 0; ordersIndex < ordersSize; ordersIndex++)
	{
		stream->readEnterSection(ordersIndex);
		size_t size=stream->readUint32("size");
		Uint8* buffer = new Uint8[size+1];
		stream->read(buffer, size+1, "data");
		orders.push_back(Order::getOrder(buffer, size+1, versionMinor));
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	br.load(stream, player, versionMinor);

	signature_check(stream, player, versionMinor);

	fm.load(stream, player, versionMinor);

	signature_check(stream, player, versionMinor);


	stream->readEnterSection("management_orders");
	Uint32 managementSize=stream->readUint32("size");
	for(Uint32 managementIndex = 0; managementIndex < managementSize; ++managementIndex)
	{
		stream->readEnterSection(managementIndex);
		signature_check(stream, player, versionMinor);
		signature_check(stream, player, versionMinor);
		boost::shared_ptr<ManagementOrder> mo=boost::shared_ptr<ManagementOrder>(ManagementOrder::load_order(stream, player, versionMinor));
		management_orders.push_back(mo);
		signature_check(stream, player, versionMinor);
		signature_check(stream, player, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	stream->readEnterSection("building_orders");
	Uint32 buildingSize=stream->readUint32("size");
	building_orders.resize(buildingSize);
	for(Uint32 buildingIndex = 0; buildingIndex < buildingSize; ++buildingIndex)
	{
		stream->readEnterSection(buildingIndex);
		building_orders[buildingIndex]=boost::shared_ptr<BuildingOrder>(new BuildingOrder);
		building_orders[buildingIndex]->load(stream, player, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	stream->readEnterSection("ressource_trackers");
	Uint32 ressourceTrackerSize=stream->readUint32("size");
	for(Uint32 ressourceTrackerIndex=0; ressourceTrackerIndex<ressourceTrackerSize; ++ressourceTrackerIndex)
	{
		stream->readEnterSection(ressourceTrackerIndex);
		int id=stream->readUint32("echo_building_id");
		boost::shared_ptr<RessourceTracker> rt(new RessourceTracker(*this, stream, player, versionMinor));
		bool activated=stream->readUint8("active");
		ressource_trackers[id]=boost::make_tuple(rt, activated);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

	stream->readEnterSection("starting_buildings");
	Uint32 startingBuildingSize=stream->readUint32("size");
	for(Uint32 startingBuildingIndex=0; startingBuildingIndex<startingBuildingSize; ++startingBuildingIndex)
	{
		stream->readEnterSection(startingBuildingIndex);
		starting_buildings.insert(stream->readUint32("gid"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	timer=stream->readUint32("timer");
	update_gm=stream->readUint8("update_gm");

	allies=stream->readUint32("allies");
	enemies=stream->readUint32("enemies");
	inn_view=stream->readUint32("inn_view");
	market_view=stream->readUint32("market_view");
	other_view=stream->readUint32("other_view");

	signature_check(stream, player, versionMinor);

	echoai->load(stream, player, versionMinor);


	signature_check(stream, player, versionMinor);

	stream->readLeaveSection();
	signature_check(stream, player, versionMinor);


	return true;
}



void Echo::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("EchoAI");

	signature_write(stream);
		
	stream->writeEnterSection("orders");
	stream->writeUint32((Uint32)orders.size(), "size");
	Uint32 ordersIndex = 0;
	for (std::list<boost::shared_ptr<Order> >::iterator i = orders.begin(); i!=orders.end(); ++i)
	{
		stream->writeEnterSection(ordersIndex);
		stream->writeUint32((*i)->getDataLength(), "size");
		///one byte indicating the type is required to be written for order.
		stream->writeUint8((*i)->getOrderType(), "type");
		stream->write((*i)->getData(), (*i)->getDataLength(), "data");
		stream->writeLeaveSection();
		ordersIndex++;
	}
	stream->writeLeaveSection();

	signature_write(stream);

	br.save(stream);

	signature_write(stream);

	fm.save(stream);

	signature_write(stream);


	stream->writeEnterSection("management_orders");
	stream->writeUint32(management_orders.size(), "size");
	for(Uint32 managementIndex = 0; managementIndex < management_orders.size(); ++managementIndex)
	{
		stream->writeEnterSection(managementIndex);
		signature_write(stream);
		signature_write(stream);
		Management::ManagementOrder::save_order(management_orders[managementIndex].get(), stream);
		signature_write(stream);
		signature_write(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeEnterSection("building_orders");
	stream->writeUint32(building_orders.size(), "size");
	for(Uint32 buildingIndex = 0; buildingIndex < building_orders.size(); ++buildingIndex)
	{
		stream->writeEnterSection(buildingIndex);
		building_orders[buildingIndex]->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeEnterSection("ressource_trackers");
	stream->writeUint32(ressource_trackers.size(), "size");
	Uint32 ressourceTrackerIndex=0;
	for(tracker_iterator i=ressource_trackers.begin(); i!=ressource_trackers.end(); ++ressourceTrackerIndex, ++i)
	{
		stream->writeEnterSection(ressourceTrackerIndex);
		stream->writeUint32(i->first, "echo_building_id");
		i->second.get<0>()->save(stream);
		stream->writeUint8(i->second.get<1>(), "active");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeEnterSection("starting_buildings");
	Uint32 startingBuildingIndex=0;
	stream->writeUint32(starting_buildings.size(), "size");
	for(std::set<int>::iterator i=starting_buildings.begin(); i!=starting_buildings.end(); ++i, ++startingBuildingIndex)
	{
		stream->writeEnterSection(startingBuildingIndex);
		stream->writeUint32(*i, "gid");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	signature_write(stream);

	stream->writeUint32(timer, "timer");
	stream->writeUint8(update_gm, "update_gm");

	stream->writeUint32(allies, "allies");
	stream->writeUint32(enemies, "enemies");
	stream->writeUint32(inn_view, "inn_view");
	stream->writeUint32(market_view, "market_view");
	stream->writeUint32(other_view, "other_view");

	signature_write(stream);

	echoai->save(stream);


	signature_write(stream);

	stream->writeLeaveSection();
	signature_write(stream);
}

#include "TextStream.h"

boost::shared_ptr<Order> Echo::getOrder(void)
{
//	for(int x=0; x<player->map->getW(); ++x)
//	{
//		for(int y=0; y<player->map->getH(); ++y)
//		{
//			player->map->setMapDiscovered(x, y, player->team->me);
//		}
//	}
/*
	if(timer%128==0)
	{
		OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("glob2.world-desynchronization.dump.txt"));
		player->game->save(stream, false, "glob2.world-desynchronization.dump.txt");
		delete stream;
	}
*/
	if(!gm)
	{
		gm.reset(new GradientManager(player->map));
		update_gm=true;
		for(int x=0; x<player->team->game->gameHeader.getNumberOfPlayers(); ++x)
		{
			if(player->team->game->players[x]!=NULL)
			{
				if(player->team->game->players[x]->type>=BasePlayer::P_AI)
				{
					Echo* other=dynamic_cast<Echo*>(player->team->game->players[x]->ai->aiImplementation);
					if(other)
					{
						if(!other->gm)
						{
							other->gm=gm;
							other->update_gm=false;
//							std::cout<<"Linked with another AI, number "<<x<<std::endl;
						}
					}
				}
			}
		}
	}

	if(from_load_timer==0)
	{
		check_fruit();
	}

	if(timer==0)
	{
		br.initiate();
		init_starting_buildings();
		allies=player->team->allies;
		enemies=player->team->enemies;
		market_view=player->team->sharedVisionExchange;
		inn_view=player->team->sharedVisionFood;
		other_view=player->team->sharedVisionOther;
	}

	if(!orders.empty())
	{
		boost::shared_ptr<Order> order=orders.front();
		orders.erase(orders.begin());
		return order;
	}
	if(update_gm)
		gm->update();
	br.tick();
	update_ressource_trackers();
	update_management_orders();
	echoai->tick(*this);
	update_management_orders();
	update_building_orders();
	timer++;
	from_load_timer++;
	return boost::shared_ptr<Order>(new NullOrder());
}



ReachToInfinity::ReachToInfinity()
{
	timer=0;
	flag_on_cherry=false;
	flag_on_orange=false;
	flag_on_prune=false;
}


bool ReachToInfinity::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ReachToInfinity");
	timer=stream->readUint32("timer");
	flag_on_cherry=stream->readUint32("flag_on_cherry");
	flag_on_orange=stream->readUint32("flag_on_orange");
	flag_on_prune=stream->readUint32("flag_on_prune");

	stream->readEnterSection("flags_on_enemy");
	Uint32 flagsOnEnemySize=stream->readUint32("size");
	for(Uint32 flagsOnEnemyIndex=0; flagsOnEnemyIndex<flagsOnEnemySize; ++flagsOnEnemyIndex)
	{
		stream->readEnterSection(flagsOnEnemyIndex);
		flags_on_enemy.insert(stream->readUint32("gid"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}


void ReachToInfinity::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ReachToInfinity");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(flag_on_cherry, "flag_on_cherry");
	stream->writeUint32(flag_on_orange, "flag_on_orange");
	stream->writeUint32(flag_on_prune, "flag_on_prune");

	stream->writeEnterSection("flags_on_enemy");
	Uint32 flagsOnEnemyIndex=0;
	stream->writeUint32(flags_on_enemy.size(), "size");
	for(std::set<int>::iterator i=flags_on_enemy.begin(); i!=flags_on_enemy.end(); ++i, ++flagsOnEnemyIndex)
	{
		stream->writeEnterSection(flagsOnEnemyIndex);
		stream->writeUint32(*i, "gid");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}


void ReachToInfinity::tick(Echo& echo)
{
	timer++;
	if(timer==1)
	{
		BuildingSearch bs(echo);
		for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
		{	
			if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
			{
				ManagementOrder* mo_completion=new AssignWorkers(5, *i);
				echo.add_management_order(mo_completion);

				ManagementOrder* mo_ratios=new ChangeSwarm(15, 1, 0, *i);
				mo_ratios->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
				echo.add_management_order(mo_ratios);

				ManagementOrder* mo_tracker=new AddRessourceTracker(12, CORN, *i);
				echo.add_management_order(mo_tracker);
			}
			if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
			{
				ManagementOrder* mo_tracker=new AddRessourceTracker(12, CORN, *i);
				echo.add_management_order(mo_tracker);
			}
		}
	}

/*
	///This is demonstration code for the advanced use of Conditions
	if(timer==100)
	{
		for(int g=0; g<1; ++g)
		{
			int prev_id=-1;
			int first_id=-1;
			int fifth_id=-1;
			for(int n=0; n<15; ++n)
			{
				//The main order for the inn
				BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);
	
				//Constraints arround the location of wheat
				AIEcho::Gradients::GradientInfo gi_wheat;
				gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
				//You want to be close to wheat
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));
				//You can't be farther than 10 units from wheat
				bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));
	
				//Constraints arround nearby settlement
				AIEcho::Gradients::GradientInfo gi_building;
				gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
				gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
				//You want to be close to other buildings, but wheat is more important
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

				AIEcho::Gradients::GradientInfo gi_building_construction;
				gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
				gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
				//You don't want to be too close
				bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

				//Constraints arround the location of fruit
				AIEcho::Gradients::GradientInfo gi_fruit;
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
				//You want to be reasnobly close to fruit, closer if possible
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, 1));
	
				if(prev_id!=-1)
				{
					bo->add_condition(new EitherCondition(new ParticularBuilding(new NotUnderConstruction, prev_id), new BuildingDestroyed(prev_id)));
				}
				else
					bo->add_condition(new Population(true, true, true, 5, Population::Greater));
	
				//Add the building order to the list of orders
				unsigned int id=echo.add_building_order(bo);

				if(prev_id!=-1)
				{
					ManagementOrder* mo_upgrade = new UpgradeRepair(id);
					mo_upgrade->add_condition(new ParticularBuilding(new NotUnderConstruction, prev_id));
					mo_upgrade->add_condition(new ParticularBuilding(new BuildingLevel(2), prev_id));
					echo.add_management_order(mo_upgrade);

					ManagementOrder* mo_assign=new AssignWorkers(6, id);
					mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
					mo_assign->add_condition(new ParticularBuilding(new BuildingLevel(2), id));
					echo.add_management_order(mo_assign);

					ManagementOrder* mo_finish=new AssignWorkers(2, id);
					mo_finish->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
					mo_finish->add_condition(new ParticularBuilding(new BuildingLevel(2), id));
					echo.add_management_order(mo_finish);
				}
				if(n==0)
				{
					first_id=id;
				}
				if(n==4)
				{
					fifth_id=id;
				}
				
				ManagementOrder* mo_completion=new AssignWorkers(1, id);
				mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
				echo.add_management_order(mo_completion);
	
				ManagementOrder* mo_tracker=new AddRessourceTracker(12, id, CORN);
				mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
				echo.add_management_order(mo_tracker);

				ManagementOrder* mo_delete=new DestroyBuilding(id);
				mo_delete->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
				mo_delete->add_condition(new ParticularBuilding(new RessourceTrackerAge(500, RessourceTrackerAge::Greater), id));
				mo_delete->add_condition(new ParticularBuilding(new RessourceTrackerAmount(48, RessourceTrackerAmount::Lesser), id));
				echo.add_management_order(mo_delete);

				ManagementOrder* mo_reconstruct = new SendMessage("construct inn");
				mo_reconstruct->add_condition(new BuildingDestroyed(id));
				echo.add_management_order(mo_reconstruct);
	
				prev_id=id;
			}

			ManagementOrder* mo_upgrade = new UpgradeRepair(first_id);
			mo_upgrade->add_condition(new ParticularBuilding(new NotUnderConstruction, fifth_id));
			echo.add_management_order(mo_upgrade);

			ManagementOrder* mo_assign=new AssignWorkers(6, first_id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, first_id));
			mo_assign->add_condition(new ParticularBuilding(new BuildingLevel(2), first_id));
			echo.add_management_order(mo_assign);

			ManagementOrder* mo_finish=new AssignWorkers(2, first_id);
			mo_finish->add_condition(new ParticularBuilding(new NotUnderConstruction, first_id));
			mo_finish->add_condition(new ParticularBuilding(new BuildingLevel(2), first_id));
			echo.add_management_order(mo_finish);
		}
	}

*/



	//Explorer flags on the three nearest fruit trees
	if((timer%100)==0)
	{
		if(echo.is_fruit_on_map())
		{
	//		BuildingSearch bs_flag(echo);
	//		bs_flag.add_condition(new SpecificBuildingType(IntBuildingType::EXPLORATION_FLAG));
	//		const int number=bs_flag.count_buildings();
			if(echo.get_team_stats().numberUnitPerType[EXPLORER]>=6 && !flag_on_cherry && !flag_on_orange && !flag_on_prune)
			{
				//Constraints arround nearby settlement
				AIEcho::Gradients::GradientInfo gi_building;
				gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));

				if(!flag_on_cherry)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_cherry = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);

					//You want the closest fruit to your settlement possible
					bo_cherry->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

					//Constraint arround the location of fruit
					AIEcho::Gradients::GradientInfo gi_cherry;
					gi_cherry.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
					//You want to be ontop of the cherry trees
					bo_cherry->add_constraint(new AIEcho::Construction::MaximumDistance(gi_cherry, 0));

					//Add the building order to the list of orders
					unsigned int id_cherry=echo.add_building_order(bo_cherry);

					if(id_cherry!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(4, id_cherry);
						echo.add_management_order(mo_completion);
						flag_on_cherry=true;

						for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							echo.add_management_order(mo_alliance);
						}
					}
				}

				if(!flag_on_orange)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_orange = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);

					//You want the closest fruit to your settlement possible
					bo_orange->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

					//Constraints arround the location of fruit
					AIEcho::Gradients::GradientInfo gi_orange;
					gi_orange.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
					//You want to be ontop of the orange trees
					bo_orange->add_constraint(new AIEcho::Construction::MaximumDistance(gi_orange, 0));

					unsigned int id_orange=echo.add_building_order(bo_orange);

					if(id_orange!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(4, id_orange);
						echo.add_management_order(mo_completion);
						flag_on_orange=true;

						for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							echo.add_management_order(mo_alliance);
						}
					}
				}

				if(!flag_on_prune)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_prune = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);

					//You want the closest fruit to your settlement possible
					bo_prune->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

					AIEcho::Gradients::GradientInfo gi_prune;
					gi_prune.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
					//You want to be ontop of the prune trees
					bo_prune->add_constraint(new AIEcho::Construction::MaximumDistance(gi_prune, 0));

					//Add the building order to the list of orders
					unsigned int id_prune=echo.add_building_order(bo_prune);

					if(id_prune!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(4, id_prune);
						echo.add_management_order(mo_completion);
						flag_on_prune=true;

						for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							echo.add_management_order(mo_alliance);
						}
					}
				}
			}
		}
	}

	//Place exploration flags on the enemy swarms
	if((timer%120)==0)
	{
		if(echo.get_team_stats().numberUnitPerType[EXPLORER]>=3)
		{
			for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
			{
				for(enemy_building_iterator ebi(echo, *i, IntBuildingType::SWARM_BUILDING, -1, false); ebi!=enemy_building_iterator(); ++ebi)
				{
					if(flags_on_enemy.find(*i)!=flags_on_enemy.end())
						continue;

					BuildingOrder* bo = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 1);
					bo->add_constraint(new CenterOfBuilding(*ebi));
					unsigned int id=echo.add_building_order(bo);

					if(id!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(12, id);
						echo.add_management_order(mo_completion);

						ManagementOrder* mo_destroyed=new DestroyBuilding(id);
						mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, *ebi));
						echo.add_management_order(mo_destroyed);

						flags_on_enemy.insert(*i);
					}
				}
			}
		}
	}



	//Standard Inns near wheat
	if((timer%200)==0 && (timer%2000)!=0)
	{
		BuildingSearch bs_level1(echo);
		bs_level1.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		bs_level1.add_condition(new BuildingLevel(1));
		const int number1=bs_level1.count_buildings();

		BuildingSearch bs_level2(echo);
		bs_level2.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		bs_level2.add_condition(new BuildingLevel(2));
		const int number2=bs_level2.count_buildings();

		BuildingSearch bs_level3(echo);
		bs_level3.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		bs_level3.add_condition(new BuildingLevel(3));
		const int number3=bs_level3.count_buildings();

		if((echo.player->team->stats.getLatestStat()->totalUnit)>=(number1*8 + number2*12 + number3*16))
		{
			//The main order for the inn
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));
			//You can't be farther than 10 units from wheat
			bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

			if(echo.is_fruit_on_map())
			{
				//Constraints arround the location of fruit
				AIEcho::Gradients::GradientInfo gi_fruit;
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
				//You want to be reasnobly close to fruit, closer if possible
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, 1));
			}

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);

//			std::cout<<"inn ordered, id="<<id<<std::endl;
	
			ManagementOrder* mo_completion=new AssignWorkers(1, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);

			ManagementOrder* mo_tracker=new AddRessourceTracker(12, CORN, id);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_tracker);
		}
	}

	//Standard swarms near wheat. Uses special mechanism, builds more swarms early on.
	if((timer%2000)==0)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
		const int number=bs.count_buildings();
		if((number<=3 && (echo.player->team->stats.getLatestStat()->totalUnit/20)>=number) ||
		   (echo.player->team->stats.getLatestStat()->totalUnit/50)>=number)
		{
//			std::cout<<"Constructing swarm"<<std::endl;
			//The main order for the swarm
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWARM_BUILDING, 3);
	
			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);

//			std::cout<<"Swarm ordered, id="<<id<<std::endl;

			//Change the number of workers assigned when the building is finished
			ManagementOrder* mo_completion=new AssignWorkers(5, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);

			//Change the ratio of the swarm when its finished
			ManagementOrder* mo_ratios=new ChangeSwarm(15, 1, 0, id);
			mo_ratios->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_ratios);

			//Add a tracker
			ManagementOrder* mo_tracker=new AddRessourceTracker(12, CORN, id);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_tracker);

		}
	}

	//Standard racetrack near stone and wood
	if((timer%2000)==500)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::WALKSPEED_BUILDING));
		const int number=bs.count_buildings();
		if((echo.player->team->stats.getLatestStat()->totalUnit/60)>=number && number<3)
		{
			//The main order for the racetrack
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::WALKSPEED_BUILDING, 6);
	
			//Constraints arround the location of wood
			AIEcho::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
			//You want to be close to wood
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 4));

			//Constraints arround the location of stone
			AIEcho::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
			//You want to be close to stone
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, 1));
			//But not to close, so you have room to upgrade
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, 2));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

			//Add the building order to the list of orders
			echo.add_building_order(bo);
		}
	}

	//Standard swimming pool near wheat and wood
	if((timer%2000)==1000)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
		const int number=bs.count_buildings();
		if((echo.player->team->stats.getLatestStat()->totalUnit/60)>=number && number<3)
		{
			//The main order for the swimmingpool
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWIMSPEED_BUILDING, 6);
	
			//Constraints arround the location of wood
			AIEcho::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
			//You want to be close to wood
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 4));

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 1));

			//Constraints arround the location of stone
			AIEcho::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
			//You don't want to be too close, so you have room to upgrade
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, 2));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

			//Add the building order to the list of orders
			echo.add_building_order(bo);
		}
	}


	//Standard school inland away from the enemies
	if((timer%2000)==1500)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		const int number=bs.count_buildings();
		if((echo.player->team->stats.getLatestStat()->totalUnit/60)>=number && number<4)
		{
			//The main order for the school
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::SCIENCE_BUILDING, 5);

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

			//Constraints arround the enemy
			AIEcho::Gradients::GradientInfo gi_enemy;
			for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
			{
				gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
			}
			gi_enemy.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			bo->add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, 3));

			//Add the building order to the list of orders
			echo.add_building_order(bo);
		}
	}


	//Level 1 to level 2 upgrades
	if((timer%300)==0)
	{
		BuildingSearch level_twos(echo);
		level_twos.add_condition(new BeingUpgradedTo(2));
		const int level_two_counts=level_twos.count_buildings();

		BuildingSearch schools(echo);
		schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		schools.add_condition(new NotUnderConstruction);
		const int school_counts=schools.count_buildings();

		BuildingSearch buildings(echo);
		buildings.add_condition(new BuildingLevel(1));
		const int total_buildings=buildings.count_buildings();
		if(level_two_counts<=(total_buildings/15) && school_counts>0)
		{
			BuildingSearch bs(echo);
			bs.add_condition(new Upgradable);
			bs.add_condition(new BuildingLevel(1));
			if(school_counts<2)
				bs.add_condition(new NotSpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
			std::vector<int> buildings;
			std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));

			if(buildings.size()!=0)
			{
				int chosen=syncRand()%buildings.size();
				ManagementOrder* uro = new UpgradeRepair(buildings[chosen]);
				echo.add_management_order(uro);

				int assigned=echo.get_building_register().get_assigned(buildings[chosen]);

				ManagementOrder* mo_assign=new AssignWorkers(8, buildings[chosen]);
				mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
				echo.add_management_order(mo_assign);

				if(echo.get_building_register().get_type(buildings[chosen])==IntBuildingType::FOOD_BUILDING)
				{
					ManagementOrder* mo_tracker_pause=new PauseRessourceTracker(buildings[chosen]);
					mo_tracker_pause->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_pause);

					ManagementOrder* mo_tracker_unpause=new UnPauseRessourceTracker(buildings[chosen]);
					mo_tracker_unpause->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_unpause);

					ManagementOrder* mo_completion=new AssignWorkers(3, buildings[chosen]);
					mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_completion);
				}
				else
				{
					ManagementOrder* mo_assign=new AssignWorkers(assigned, buildings[chosen]);
					mo_assign->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_assign);
				}
			}
		}
	}

	//Level 2 to level 3 upgrades
	if((timer%300)==0)
	{
		BuildingSearch level_threes(echo);
		level_threes.add_condition(new BeingUpgradedTo(3));
		const int level_three_counts=level_threes.count_buildings();

		BuildingSearch schools(echo);
		schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		schools.add_condition(new NotUnderConstruction);
		schools.add_condition(new BuildingLevel(2));
		int school_counts=schools.count_buildings();

		BuildingSearch schools2(echo);
		schools2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		schools2.add_condition(new NotUnderConstruction);
		schools2.add_condition(new BuildingLevel(3));
		school_counts+=schools2.count_buildings();

		BuildingSearch buildings(echo);
		buildings.add_condition(new BuildingLevel(2));
		const int total_buildings=buildings.count_buildings();
		if(level_three_counts<=(total_buildings/15) && school_counts>0)
		{
			BuildingSearch bs(echo);
			bs.add_condition(new Upgradable);
			bs.add_condition(new BuildingLevel(2));
			if(school_counts<2)
				bs.add_condition(new NotSpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
			std::vector<int> buildings;
			std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));

			if(buildings.size()!=0)
			{
				int chosen=syncRand()%buildings.size();
				ManagementOrder* uro = new UpgradeRepair(buildings[chosen]);
				echo.add_management_order(uro);

				int assigned=echo.get_building_register().get_assigned(buildings[chosen]);

				ManagementOrder* mo_assign=new AssignWorkers(8, buildings[chosen]);
				mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
				echo.add_management_order(mo_assign);

				if(echo.get_building_register().get_type(buildings[chosen])==IntBuildingType::FOOD_BUILDING)
				{
					ManagementOrder* mo_tracker_pause=new PauseRessourceTracker(buildings[chosen]);
					mo_tracker_pause->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_pause);

					ManagementOrder* mo_tracker_unpause=new UnPauseRessourceTracker(buildings[chosen]);
					mo_tracker_unpause->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_unpause);

					ManagementOrder* mo_completion=new AssignWorkers(6, buildings[chosen]);
					mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_completion);
				}
				else
				{
					ManagementOrder* mo_assign=new AssignWorkers(assigned, buildings[chosen]);
					mo_assign->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_assign);
				}
			}
		}
	}



	//Delete old inns and swarms that are hard to keep full of wheat
	if((timer%500)==0)
	{
		BuildingSearch inns(echo);
		inns.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		inns.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=inns.begin(); i!=inns.end(); ++i)
		{
			boost::shared_ptr<RessourceTracker> rt=echo.get_ressource_tracker(*i);
			if(rt)
			{
				if(rt->get_age()>1500)
				{
					if(rt->get_total_level() < 24*echo.get_building_register().get_level(*i))
					{
						ManagementOrder* mo_destroy=new DestroyBuilding(*i);
						echo.add_management_order(mo_destroy);
					}
				}
			}
		}


		BuildingSearch swarms(echo);
		swarms.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
		swarms.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=swarms.begin(); i!=swarms.end(); ++i)
		{
			boost::shared_ptr<RessourceTracker> rt=echo.get_ressource_tracker(*i);
			if(rt)
			{
				if(rt->get_age()>2500)
				{
					if(rt->get_total_level() < 18)
					{
						ManagementOrder* mo_destroy=new DestroyBuilding(*i);
						echo.add_management_order(mo_destroy);
					}
				}
			}
		}
	}

	//Farming wheat and wood near water
	if((timer%250)==0)
	{
		AddArea* mo_farming=new AddArea(ForbiddenArea);
		RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
		AIEcho::Gradients::GradientInfo gi_water;
		gi_water.add_source(new Entities::Water);
		Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_water);
		MapInfo mi(echo);
		for(int x=0; x<mi.get_width(); ++x)
		{
			for(int y=0; y<mi.get_height(); ++y)
			{
				if((x%2==1 && y%2==1))
				{
					if((!mi.is_ressource(x, y, WOOD) &&
					    !mi.is_ressource(x, y, CORN)) &&
					    mi.is_forbidden_area(x, y))
					{
						mo_non_farming->add_location(x, y);
					}
					else
					{
						if((mi.is_ressource(x, y, WOOD) ||
						    mi.is_ressource(x, y, CORN)) &&
						    mi.is_discovered(x, y) &&
						    !mi.is_forbidden_area(x, y) &&
						    gradient.get_height(x, y)<10)
						{
							mo_farming->add_location(x, y);
						}
					}
				}
			}
		}
		echo.add_management_order(mo_farming);
		echo.add_management_order(mo_non_farming);
	}
}


void ReachToInfinity::handle_message(Echo& echo, const std::string& message)
{
	if(message=="construct inn")
	{
		//The main order for the inn
		BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);
	
		//Constraints arround the location of wheat
		AIEcho::Gradients::GradientInfo gi_wheat;
		gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
		//You want to be close to wheat
		bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));
		//You can't be farther than 10 units from wheat
		bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));

		//Constraints arround nearby settlement
		AIEcho::Gradients::GradientInfo gi_building;
		gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
		//You want to be close to other buildings, but wheat is more important
		bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

		AIEcho::Gradients::GradientInfo gi_building_construction;
		gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
		//You don't want to be too close
		bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

		//Constraints arround the location of fruit
		if(echo.is_fruit_on_map())
		{
			AIEcho::Gradients::GradientInfo gi_fruit;
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
			//You want to be reasnobly close to fruit, closer if possible
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, 1));
		}

		//Add the building order to the list of orders
		unsigned int id=echo.add_building_order(bo);

//				std::cout<<"inn ordered, id="<<id<<std::endl;
		
		ManagementOrder* mo_completion=new AssignWorkers(1, id);
		mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		echo.add_management_order(mo_completion);

		ManagementOrder* mo_tracker=new AddRessourceTracker(12, CORN, id);
		mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		echo.add_management_order(mo_tracker);
	}
}

