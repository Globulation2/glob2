/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
#include <algorithm>
#include "BuildingsTypes.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace AIEcho::UpgradesRepairs;
using namespace boost::logic;



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
		assert(false);
	}
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



Entities::Water:: Water()
{

}



bool Entities::Water:: is_entity(Map* map, int posx, int posy)
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



GradientInfo::GradientInfo()
{
	needs_updated=indeterminate;
}


GradientInfo::~GradientInfo()
{

}


void GradientInfo::add_source(Entities::Entity* source)
{
	sources.push_back(source);
}


void GradientInfo::add_obstacle(Entities::Entity* obstacle)
{
	obstacles.push_back(obstacle);
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


void GradientManager::update()
{

	static unsigned int max=0;
	static unsigned int max_need_updated=0;
	if(gradients.size()>max)
	{
		max=gradients.size();
		std::cout<<"Max gradients "<<max<<std::endl;
	}
	unsigned int need_count=0;
	for(unsigned int x=0; x<gradients.size(); ++x)
	{
		if(gradients[x]->get_gradient_info().needs_updating())
		{
			need_count++;
		}
	}
	if(need_count>max_need_updated)
	{
		max_need_updated=need_count;
		std::cout<<"Max update requiring gradients "<<max_need_updated<<std::endl;
	}


	timer++;
	std::transform(ticks_since_update.begin(), ticks_since_update.end(), ticks_since_update.begin(), increment);
	unsigned int orig_cur_update=cur_update;
	do
	{
		if((timer%8)==0 && !gradients.empty())
		{
			if(ticks_since_update[cur_update]>150 && gradients[cur_update]->get_gradient_info().needs_updating())
			{
				gradients[cur_update]->recalculate(map);
				ticks_since_update[cur_update]=0;
				cur_update++;
				if(cur_update>=gradients.size())
					cur_update=0;
				return;
			}
		}
		cur_update++;
		if(cur_update>=gradients.size())
			cur_update=0;
	}
	while(orig_cur_update!=cur_update);
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



CenteredOn::CenteredOn(int gbid) : gbid(gbid)
{

}



int CenteredOn::calculate_constraint(Echo& echo, int x, int y)
{
	return 0;
}



bool CenteredOn::passes_constraint(Echo& echo, int x, int y)
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



BuildingOrder::BuildingOrder(Player* player, int building_type, int number_of_workers) : building_type(building_type), number_of_workers(number_of_workers), player(player)
{

}


void BuildingOrder::add_constraint(Constraint* constraint)
{
	constraints.push_back(boost::shared_ptr<Constraint>(constraint));
}


position BuildingOrder::find_location(Echo& echo, Map* map, GradientManager& manager)
{
	position best(0,0);
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
							if(!(*i)->passes_constraint(echo, x+x2, y+y2))
								passes=false;
				if(!passes)
					break;

				if(!check_flag && (!map->isMapDiscovered(x, y, player->team->allies) ||
				   !map->isMapDiscovered(x+type->width-1, y+type->height-1, player->team->allies))
				    )
				{
					passes=false;
					break;
				}
				score+=(*i)->calculate_constraint(echo, x, y);
				score+=(*i)->calculate_constraint(echo, x+type->width-1, y+type->height-1);
				score+=(*i)->calculate_constraint(echo, x, y+type->height-1);
				score+=(*i)->calculate_constraint(echo, x+type->width-1, y);
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

/*
	int score=0;
	bool passes=true;
	for(std::vector<boost::shared_ptr<Constraint> >::iterator i=constraints.begin(); i!=constraints.end(); ++i)
	{
		std::cout<<"________final__________"<<std::endl;
		if(!(*i)->passes_constraint(manager, best.x, best.y) ||
		   !(*i)->passes_constraint(manager, best.x+type->width-1, best.y) ||
		   !(*i)->passes_constraint(manager, best.x+type->width-1, best.y+type->height-1) ||
		   !(*i)->passes_constraint(manager, best.x, best.y+type->height-1) 
		    )
		{			
			passes=false;
			break;
		}
		score+=(*i)->calculate_constraint(manager, best.x, best.y);
	}
*/
	return best;
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
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b=player->team->myBuildings[i];
		if(b!=NULL)
		{
			found_buildings[building_id++]=boost::make_tuple(b->posX, b->posY, b->type->shortTypeNum, b->gid, false);
		}
	}
}


unsigned int BuildingRegister::register_building(int x, int y, int building_type)
{
	pending_buildings[building_id]=boost::make_tuple(x, y, building_type, 0);
	return building_id++;
}



bool BuildingRegister::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingRegister");

	signature_check(stream, player, versionMinor);

	stream->readEnterSection("pending_buildings");
	Uint32 pending_size=stream->readUint32("size");
	for(Uint32 pending_index=0; pending_index<pending_size; ++pending_index)
	{
		stream->readEnterSection(pending_index);
		pending_buildings[		stream->readUint32("echo_building_id")]=
			boost::make_tuple(	stream->readUint32("xpos"),
						stream->readUint32("ypos"),
						stream->readUint32("building_type"),
						stream->readUint32("ticks_since_registered"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	signature_check(stream, player, versionMinor);

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

	signature_check(stream, player, versionMinor);

	building_id=stream->readUint32("building_id");
	stream->readLeaveSection();
	return true;
}



void BuildingRegister::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("BuildingRegister");

	signature_write(stream);

	stream->writeEnterSection("pending_buildings");
	unsigned int pending_size=0;
	stream->writeUint32(pending_buildings.size(), "size");
	for(pending_iterator i=pending_buildings.begin(); i!=pending_buildings.end(); ++i)
	{
		stream->writeEnterSection(pending_size);
		stream->writeUint32(i->first, "echo_building_id");
		stream->writeUint32(i->second.get<0>(), "xpos");
		stream->writeUint32(i->second.get<1>(), "ypos");
		stream->writeUint32(i->second.get<2>(), "building_type");
		stream->writeUint32(i->second.get<3>(), "ticks_since_registered");
		stream->writeLeaveSection();
		pending_size++;
	}
	stream->writeLeaveSection();

	signature_write(stream);

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
	}
	stream->writeLeaveSection();

	signature_write(stream);

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
			if(gbid==NOGBID)
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
	return pending_buildings.find(id)!=pending_buildings.end();
}



bool BuildingRegister::is_building_found(unsigned int id)
{
	return found_buildings.find(id)!=found_buildings.end();
}




bool BuildingRegister::is_building_upgrading(unsigned int id)
{
	tribool v=found_buildings[id].get<4>();
	if(v)
		return true;
	else if(!v)
		return false;
	return true;
}



Building* BuildingRegister::get_building(unsigned int id)
{
	return player->team->myBuildings[::Building::GIDtoID(found_buildings[id].get<3>())];
}



int BuildingRegister::get_type(unsigned int id)
{
	return found_buildings[id].get<2>();
}



int BuildingRegister::get_level(unsigned int id)
{
	return get_building(id)->type->level;
}



Condition* Condition::load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Condition");
	ConditionType type=static_cast<ConditionType>(stream->readUint32("type"));
	Condition* condition=NULL;
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
		case CEnemyBuildingDestroyed:
			condition=new EnemyBuildingDestroyed;
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



void Condition::save_condition(Condition* condition, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Condition");
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
	return true;
}



void NotUnderConstruction::save(GAGCore::OutputStream *stream)
{

}




bool UnderConstruction::passes(Echo& echo, int id)
{
	Building* building = echo.get_building_register().get_building(id);
	return building->constructionResultState!=::Building::NO_CONSTRUCTION && building->buildingState==Building::ALIVE;
}


bool UnderConstruction::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	return true;
}



void UnderConstruction::save(GAGCore::OutputStream *stream)
{

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
	return true;

}



void BeingUpgraded::save(GAGCore::OutputStream *stream)
{

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
		if(b->type->level==level-1)
			return true;
	else if(b->type->level==level)
		return true;
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
	stream->writeEnterSection("BeingLevel");
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
	return true;
}



void Upgradable::save(GAGCore::OutputStream *stream)
{

}



EnemyBuildingDestroyed::EnemyBuildingDestroyed(Echo& echo, int gbid) : gbid(gbid)
{
	Building* b=echo.player->game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
	type=b->type->shortTypeNum;
	level=b->type->level;
	location=position(b->posX, b->posY);
}



bool EnemyBuildingDestroyed::passes(Echo& echo, int id)
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



void ManagementOrder::add_condition(Condition* condition)
{
	conditions.push_back(boost::shared_ptr<Condition>(condition));
}



bool ManagementOrder::passes_conditions(Echo& echo, int building_id)
{
	for(unsigned int i=0; i<conditions.size(); ++i)
	{
		if(!conditions[i]->passes(echo, building_id))
			return false;
	}
	return true;
}



ManagementOrder* ManagementOrder::load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	signature_check(stream, player, versionMinor);
	ManagementOrderType mot=static_cast<ManagementOrderType>(stream->readUint32("type"));
	ManagementOrder* mo=NULL;
	signature_check(stream, player, versionMinor);
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
	}
	signature_check(stream, player, versionMinor);
	return mo;
}



void ManagementOrder::save_order(ManagementOrder* mo, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ManagementOrder");
	signature_write(stream);
	stream->writeUint32(mo->get_type(), "type");
	signature_write(stream);
	mo->save(stream);
	signature_write(stream);
	stream->writeLeaveSection();
}



AssignWorkers::AssignWorkers(int number_of_workers) : number_of_workers(number_of_workers)
{

}


void AssignWorkers::modify(Echo& echo, int building_id)
{
	if(echo.get_building_register().is_building_found(building_id))
	{
		echo.push_order(new OrderModifyBuilding(echo.get_building_register().get_building(building_id)->gid, number_of_workers));
	}
}



bool AssignWorkers::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AssignWorkers");
	number_of_workers=stream->readUint32("number_of_workers");
	stream->readLeaveSection();
	return true;
}



void AssignWorkers::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AssignWorkers");
	stream->writeUint32(number_of_workers, "number_of_workers");
	stream->writeLeaveSection();
}



ChangeSwarm::ChangeSwarm(int worker_ratio, int explorer_ratio, int warrior_ratio) : worker_ratio(worker_ratio), explorer_ratio(explorer_ratio), warrior_ratio(warrior_ratio)
{

}


void ChangeSwarm::modify(Echo& echo, int building_id)
{
	if(echo.get_building_register().is_building_found(building_id))
	{
		Sint32 ratio[NB_UNIT_TYPE];
		ratio[0]=worker_ratio;
		ratio[1]=explorer_ratio;
		ratio[2]=warrior_ratio;
		echo.push_order(new OrderModifySwarm(echo.get_building_register().get_building(building_id)->gid, ratio));
	}
}



bool ChangeSwarm::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeSwarm");
	worker_ratio=stream->readUint32("worker_ratio");
	explorer_ratio=stream->readUint32("explorer_ratio");
	warrior_ratio=stream->readUint32("warrior_ratio");
	stream->readLeaveSection();
	return true;

}



void ChangeSwarm::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeSwarm");
	stream->writeUint32(worker_ratio, "worker_ratio");
	stream->writeUint32(explorer_ratio, "explorer_ratio");
	stream->writeUint32(warrior_ratio, "warrior_ratio");
	stream->writeLeaveSection();
}




void DestroyBuilding::modify(Echo& echo, int building_id)
{
	if(echo.get_building_register().is_building_found(building_id))
	{
		echo.push_order(new OrderDelete(echo.get_building_register().get_building(building_id)->gid));
	}
}



bool DestroyBuilding::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	return true;
}



void DestroyBuilding::save(GAGCore::OutputStream *stream)
{

}



RessourceTracker::RessourceTracker(Echo& echo, int building_id) : record(12, 0), position(0), timer(0), echo(echo), building_id(building_id)
{

}



void RessourceTracker::tick()
{
	timer++;
	if((timer%10)==0)
	{
		Building* b = echo.get_building_register().get_building(building_id);
		if(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING || b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING)
		{
			record[position]=b->ressources[CORN];
		}
		position++;
		if(position>=record.size())
			position=0;
	}
}


int RessourceTracker::get_average_level()
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
	Uint32 size=stream->readUint32("size");
	record.resize(size);
	for(unsigned int record_index=0; record_index<size; ++record_index)
	{
		stream->readEnterSection(record_index);
		record[record_index]=stream->readUint32("quantity_of_ressources");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	position=stream->readUint32("position");
	timer=stream->readUint32("timer");
	building_id=stream->readUint32("building_id");
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
	stream->writeLeaveSection();
}



AddRessourceTracker::AddRessourceTracker()
{

}



void AddRessourceTracker::modify(Echo& echo, int building_id)
{
	echo.add_ressource_tracker(new RessourceTracker(echo, building_id), building_id);
}



bool AddRessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	return true;
}



void AddRessourceTracker::save(GAGCore::OutputStream *stream)
{

}



void PauseRessourceTracker::modify(Echo& echo, int building_id)
{
	echo.pause_ressource_tracker(building_id);
}



bool PauseRessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	return true;
}



void PauseRessourceTracker::save(GAGCore::OutputStream *stream)
{

}



void UnPauseRessourceTracker::modify(Echo& echo, int building_id)
{
	echo.unpause_ressource_tracker(building_id);
}



bool UnPauseRessourceTracker::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	return true;
}



void UnPauseRessourceTracker::save(GAGCore::OutputStream *stream)
{

}



ChangeFlagSize::ChangeFlagSize(int size) : size(size)
{

}



void ChangeFlagSize::modify(Echo& echo, int building_id)
{
	if(echo.get_building_register().is_building_found(building_id))
	{
		echo.push_order(new OrderModifyFlag(echo.get_building_register().get_building(building_id)->gid, size));
	}
}



bool ChangeFlagSize::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagSize");
	size=stream->readUint32("size");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagSize::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagSize");
	stream->writeUint32(size, "size");
	stream->writeLeaveSection();
}



ChangeFlagMinimumLevel::ChangeFlagMinimumLevel(int minimum_level) : minimum_level(minimum_level)
{

}



void ChangeFlagMinimumLevel::modify(Echo& echo, int building_id)
{
	if(echo.get_building_register().is_building_found(building_id))
	{
		echo.push_order(new OrderModifyMinLevelToFlag(echo.get_building_register().get_building(building_id)->gid, minimum_level));
	}
}



bool ChangeFlagMinimumLevel::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeFlagMinimumLevel");
	minimum_level=stream->readUint32("minimum_level");
	stream->readLeaveSection();
	return true;
}



void ChangeFlagMinimumLevel::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeFlagMinimumLevel");
	stream->writeUint32(minimum_level, "minimum_level");
	stream->writeLeaveSection();
}



GlobalManagementOrder* GlobalManagementOrder::load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("GlobalManagementOrder");
	GlobalManagementOrderType mot=static_cast<GlobalManagementOrderType>(stream->readUint32("type"));
	GlobalManagementOrder* mo=NULL;
	switch(mot)
	{
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
	}
	return mo;
}



void GlobalManagementOrder::save_order(GlobalManagementOrder* mo, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("GlobalManagementOrder");
	stream->writeUint32(mo->get_type(), "type");
	mo->save(stream);
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
		acc.applyBrush(echo.player->map, BrushApplication(i->x, i->y, 0));
	}
	if(acc.getApplicationCount()>0)
	{
		switch(areatype)
		{
			case ClearingArea:
				echo.push_order(new OrderAlterateClearArea(echo.player->team->teamNumber, BrushTool::MODE_ADD, &acc));
				break;
			case ForbiddenArea:
				echo.push_order(new OrderAlterateForbidden(echo.player->team->teamNumber, BrushTool::MODE_ADD, &acc));
				break;
			case GuardArea:
				echo.push_order(new OrderAlterateGuardArea(echo.player->team->teamNumber, BrushTool::MODE_ADD, &acc));
				break;
		}
	}
}



bool AddArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AddArea");
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
		acc.applyBrush(echo.player->map, BrushApplication(i->x, i->y, 0));
	}
	if(acc.getApplicationCount()>0)
	{
		switch(areatype)
		{
			case ClearingArea:
				echo.push_order(new OrderAlterateClearArea(echo.player->team->teamNumber, BrushTool::MODE_DEL, &acc));
				break;
			case ForbiddenArea:
				echo.push_order(new OrderAlterateForbidden(echo.player->team->teamNumber, BrushTool::MODE_DEL, &acc));
				break;
			case GuardArea:
				echo.push_order(new OrderAlterateGuardArea(echo.player->team->teamNumber, BrushTool::MODE_DEL, &acc));
				break;
		}
	}
}



bool RemoveArea::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RemoveArea");
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

	echo.push_order(new SetAllianceOrder(echo.player->team->teamNumber, alliedmask, enemymask, market_mask, inn_mask, other_mask));
}



bool ChangeAlliances::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeAlliances");
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



UpgradeRepairOrder::UpgradeRepairOrder(Echo& echo, int id, int number_of_workers): echo(echo), id(id), number_of_workers(number_of_workers)
{

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
	if(is_end)
		return;
	if(found_id==-1)
	{
		position=search->echo.get_building_register().begin();
	}
	else
		position++;
	for(; position!=search->echo.get_building_register().end() &&
	      !search->passes_conditions(position->first);
             position++)
	{
	}
	if(position==search->echo.get_building_register().end())
	{
		is_end=true;
		return;
	}
	found_id=position->first;
}



BuildingSearch::BuildingSearch(Echo& echo) : echo(echo)
{

}



void BuildingSearch::add_condition(Conditions::Condition* condition)
{
	conditions.push_back(boost::shared_ptr<Conditions::Condition>(condition));
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
	for(std::vector<boost::shared_ptr<Conditions::Condition> >::iterator i = conditions.begin();  i!=conditions.end(); ++i)
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
	for(int n=0; n<1024; ++n)
	{
		Building* b=buildings[n];
		if(b)
		{
			if(b->posX==x && b->posY==y)
			{
				if(b->type->shortTypeNum > IntBuildingType::DEFENSE_BUILDING && b->type->shortTypeNum < IntBuildingType::STONE_WALL)
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

	while(current_index<1024)
	{
		Building* b=echo->player->game->teams[team]->myBuildings[current_index];
		if(b)
		{
			if( (b->seenByMask&echo->player->team->me || echo->get_starting_buildings().find(b->gid)!=echo->get_starting_buildings().end()) &&
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

	if(current_index==1024)
		is_end=true;
}



Echo::Echo(EchoAI* echoai, Player* player) : player(player), echoai(echoai), gm(), br(player, *this), fm(*this), timer(0)
{
}


unsigned int Echo::add_building_order(Construction::BuildingOrder& bo)
{
	position p=bo.find_location(*this, player->map, *gm);	
	if(p.x != 0 || p.y != 0)
	{
		unsigned int id=br.register_building(p.x, p.y, bo.get_building_type());
		Sint32 type=-1;
		if(bo.get_building_type()>IntBuildingType::DEFENSE_BUILDING && bo.get_building_type() <IntBuildingType::STONE_WALL)
		{
			type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[bo.get_building_type()], 0, false);
			ManagementOrder* mo_flag=new AssignWorkers(bo.get_number_of_workers());
			add_management_order(mo_flag, id);
		}
		else
		{
			type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[bo.get_building_type()], 0, true);
			ManagementOrder* mo_during_construction=new AssignWorkers(bo.get_number_of_workers());
			mo_during_construction->add_condition(new UnderConstruction);
			add_management_order(mo_during_construction, id);
		}
		orders.push(new OrderCreate(player->team->teamNumber, p.x, p.y, type));
//		std::cout<<"constructing building x="<<p.x<<" y="<<p.y<<"  id="<<id<<std::endl;
		return id;
	}
	return INVALID_BUILDING;
}


void Echo::add_management_order(Management::ManagementOrder* mo, unsigned int id)
{
	management_orders.push_back(boost::make_tuple(boost::shared_ptr<ManagementOrder>(mo), id));
}


void Echo::update_management_orders()
{
	for(std::vector<boost::tuple<boost::shared_ptr<Management::ManagementOrder>,int> >::iterator i=management_orders.begin(); i!=management_orders.end();)
	{
		bool is_found=br.is_building_found(i->get<1>());
		if(!br.is_building_pending(i->get<1>()) && !is_found)
		{
			i=management_orders.erase(i);
//			std::cout<<"Erasing management order!"<<std::endl;
			continue;
		}
		if(is_found)
		{
			if(i->get<0>()->passes_conditions(*this, i->get<1>()))
			{
				i->get<0>()->modify(*this, i->get<1>());
				i=management_orders.erase(i);
				continue;
			}
		}
		++i;
	}
}



void Echo::add_global_management_order(Management::GlobalManagementOrder* gmo)
{
	global_management_orders.push_back(boost::shared_ptr<GlobalManagementOrder>(gmo));
}



void Echo::update_global_management_orders()
{
	for(std::vector<boost::shared_ptr<GlobalManagementOrder> >::iterator i=global_management_orders.begin(); i!=global_management_orders.end();)
	{
		(*i)->modify(*this);
		i=global_management_orders.erase(i);
		continue;
	}
}



void Echo::add_upgrade_repair_order(UpgradesRepairs::UpgradeRepairOrder* uro)
{
	const int id=uro->get_id();
	if(br.is_building_found(id))
	{
		orders.push(new OrderConstruction(br.get_building(id)->gid));
		br.set_upgrading(id);

		ManagementOrder* mo_during_construction=new AssignWorkers(uro->get_number_of_workers());
		mo_during_construction->add_condition(new UnderConstruction);
		add_management_order(mo_during_construction, id);

		//Change the number of units assigned when the building is finished
		ManagementOrder* mo_completion=new AssignWorkers(br.get_building(id)->maxUnitWorking);
		mo_completion->add_condition(new NotUnderConstruction);
		add_management_order(mo_completion, id);
	}
	delete uro;
}



void Echo::add_ressource_tracker(Management::RessourceTracker* rt, int building_id)
{
	ressource_trackers[building_id]=boost::make_tuple(boost::shared_ptr<RessourceTracker>(rt), true);
}



boost::shared_ptr<Management::RessourceTracker> Echo::get_ressource_tracker(int building_id)
{
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
	for(std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> >::iterator i = ressource_trackers.begin(); i!=ressource_trackers.end(); ++i)
	{
		if(!br.is_building_found(i->first) && !br.is_building_pending(i->first))
		{
			std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> >::iterator current=i;
			++i;
			ressource_trackers.erase(current);
		}
		else
		{
			if(i->second.get<1>())
				i->second.get<0>()->tick();
		}
	}
}



void Echo::init_starting_buildings()
{
	for(int t=0; t<32; ++t)
	{
		if(player->game->teams[t])
		{
			for(int bu=0; bu<1024; ++bu)
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
		Uint8* buffer = new Uint8[size];
		stream->read(buffer, size, "data");
		orders.push(Order::getOrder(buffer, size));
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
		ManagementOrder* mo=ManagementOrder::load_order(stream, player, versionMinor);
		Uint32 id=stream->readUint32("echo_building_id");
		management_orders.push_back(boost::make_tuple(mo, id));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	signature_check(stream, player, versionMinor);

	stream->readEnterSection("global_management_orders");
	Uint32 globalManagementSize=stream->readUint32("size");
	for(Uint32 globalManagementIndex = 0; globalManagementIndex < globalManagementSize; ++globalManagementIndex)
	{
		stream->readEnterSection(globalManagementIndex);
		global_management_orders.push_back(boost::shared_ptr<Management::GlobalManagementOrder>(Management::GlobalManagementOrder::load_order(stream, player, versionMinor)));
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
	for (Uint32 ordersIndex = 0; ordersIndex < orders.size(); ordersIndex++)
	{
		stream->writeEnterSection(ordersIndex);
		Order* order = orders.front();
		orders.pop();
		stream->writeUint32(order->getDataLength()+1, "size");
		stream->write(order->getData(), order->getDataLength(), "data");
		delete order;
		stream->writeLeaveSection();
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
		Management::ManagementOrder::save_order(management_orders[managementIndex].get<0>().get(), stream);
		stream->writeUint32(management_orders[managementIndex].get<1>(), "echo_building_id");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();


	signature_write(stream);

	stream->writeEnterSection("global_management_orders");
	stream->writeUint32(global_management_orders.size(), "size");
	for(Uint32 globalManagementIndex = 0; globalManagementIndex < global_management_orders.size(); ++globalManagementIndex)
	{
		stream->writeEnterSection(globalManagementIndex);
		Management::GlobalManagementOrder::save_order(global_management_orders[globalManagementIndex].get(), stream);
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



Order* Echo::getOrder(void)
{
//	for(int x=0; x<player->map->getW(); ++x)
//	{
//		for(int y=0; y<player->map->getH(); ++y)
//		{
//			player->map->setMapDiscovered(x, y, player->team->me);
//		}
//	}
	if(!gm)
	{
		gm.reset(new GradientManager(player->map));
		update_gm=true;
		for(int x=0; x<32; ++x)
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
		Order* order=orders.front();
		orders.pop();
		return order;
	}

	if(update_gm)
		gm->update();
	br.tick();
	update_ressource_trackers();
	update_management_orders();
	echoai->tick(*this);
	update_management_orders();
	update_global_management_orders();
	timer++;
	return new NullOrder;
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
				ManagementOrder* mo_completion=new AssignWorkers(5);
				echo.add_management_order(mo_completion, *i);

				ManagementOrder* mo_ratios=new ChangeSwarm(15, 1, 0);
				mo_ratios->add_condition(new NotUnderConstruction);
				echo.add_management_order(mo_ratios, *i);

				ManagementOrder* mo_tracker=new AddRessourceTracker;
				echo.add_management_order(mo_tracker, *i);
			}
			if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
			{
				ManagementOrder* mo_tracker=new AddRessourceTracker;
				echo.add_management_order(mo_tracker, *i);
			}
		}
	}


	//Explorer flags on the three nearest fruit trees
	if((timer%100)==0)
	{
//		BuildingSearch bs_flag(echo);
//		bs_flag.add_condition(new SpecificBuildingType(IntBuildingType::EXPLORATION_FLAG));
//		const int number=bs_flag.count_buildings();
		if(echo.player->team->stats.getLatestStat()->numberUnitPerType[EXPLORER]>=3 && !flag_on_cherry && !flag_on_orange && !flag_on_prune)
		{
			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);

			if(!flag_on_cherry)
			{
				//The main order for the exploration flag
				AIEcho::Construction::BuildingOrder bo_cherry(echo.player, IntBuildingType::EXPLORATION_FLAG, 2);

				//You want the closest fruit to your settlement possible
				bo_cherry.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

				//Constraint arround the location of fruit
				AIEcho::Gradients::GradientInfo gi_cherry;
				gi_cherry.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
				//You want to be ontop of the cherry trees
				bo_cherry.add_constraint(new AIEcho::Construction::MaximumDistance(gi_cherry, 1));

				//Add the building order to the list of orders
				unsigned int id_cherry=echo.add_building_order(bo_cherry);

				if(id_cherry!=INVALID_BUILDING)
				{
					ManagementOrder* mo_completion=new ChangeFlagSize(4);
					echo.add_management_order(mo_completion, id_cherry);
					flag_on_cherry=true;

					for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
					{
						GlobalManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
						echo.add_global_management_order(mo_alliance);
					}
				}
			}

			if(!flag_on_orange)
			{
				//The main order for the exploration flag
				AIEcho::Construction::BuildingOrder bo_orange(echo.player, IntBuildingType::EXPLORATION_FLAG, 2);

				//You want the closest fruit to your settlement possible
				bo_orange.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

				//Constraints arround the location of fruit
				AIEcho::Gradients::GradientInfo gi_orange;
				gi_orange.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
				//You want to be ontop of the cherry trees
				bo_orange.add_constraint(new AIEcho::Construction::MaximumDistance(gi_orange, 1));

				unsigned int id_orange=echo.add_building_order(bo_orange);

				if(id_orange!=INVALID_BUILDING)
				{
					ManagementOrder* mo_completion=new ChangeFlagSize(4);
					echo.add_management_order(mo_completion, id_orange);
					flag_on_orange=true;

					for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
					{
						GlobalManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
						echo.add_global_management_order(mo_alliance);
					}
				}
			}

			if(!flag_on_prune)
			{

				//The main order for the exploration flag
				AIEcho::Construction::BuildingOrder bo_prune(echo.player, IntBuildingType::EXPLORATION_FLAG, 2);

				//You want the closest fruit to your settlement possible
				bo_prune.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

				AIEcho::Gradients::GradientInfo gi_prune;
				gi_prune.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
				//You want to be ontop of the cherry trees
				bo_prune.add_constraint(new AIEcho::Construction::MaximumDistance(gi_prune, 1));

				//Add the building order to the list of orders
				unsigned int id_prune=echo.add_building_order(bo_prune);

				if(id_prune!=INVALID_BUILDING)
				{
					ManagementOrder* mo_completion=new ChangeFlagSize(4);
					echo.add_management_order(mo_completion, id_prune);
					flag_on_prune=true;

					for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
					{
						GlobalManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
						echo.add_global_management_order(mo_alliance);
					}
				}
			}
		}
	}

	//Place exploration flags on the enemy swarms
	if((timer%120)==0)
	{
		if(echo.player->team->stats.getLatestStat()->numberUnitPerType[EXPLORER]>=3)
		{
			for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
			{
				for(enemy_building_iterator ebi(echo, *i, IntBuildingType::SWARM_BUILDING, -1, false); ebi!=enemy_building_iterator(); ++ebi)
				{
					if(flags_on_enemy.find(*i)!=flags_on_enemy.end())
						continue;

					AIEcho::Construction::BuildingOrder bo(echo.player, IntBuildingType::EXPLORATION_FLAG, 1);
					bo.add_constraint(new CenteredOn(*ebi));
					unsigned int id=echo.add_building_order(bo);

					if(id!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(12);
						echo.add_management_order(mo_completion, id);

						ManagementOrder* mo_destroyed=new DestroyBuilding;
						mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, *ebi));
						echo.add_management_order(mo_destroyed, id);

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
			AIEcho::Construction::BuildingOrder bo(echo.player, IntBuildingType::FOOD_BUILDING, 2);

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));
			//You can't be farther than 10 units from wheat
			bo.add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

			//Constraints arround the location of fruit
			AIEcho::Gradients::GradientInfo gi_fruit;
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
			//You want to be reasnobly close to fruit, closer if possible
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, 1));

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);
	
			ManagementOrder* mo_completion=new AssignWorkers(1);
			mo_completion->add_condition(new NotUnderConstruction);
			echo.add_management_order(mo_completion, id);

			ManagementOrder* mo_tracker=new AddRessourceTracker;
			mo_tracker->add_condition(new NotUnderConstruction);
			echo.add_management_order(mo_tracker, id);
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
			AIEcho::Construction::BuildingOrder bo(echo.player, IntBuildingType::SWARM_BUILDING, 3);
	
			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);

//			std::cout<<"Swarm ordered, id="<<id<<std::endl;

			//Change the number of workers assigned when the building is finished
			ManagementOrder* mo_completion=new AssignWorkers(5);
			mo_completion->add_condition(new NotUnderConstruction);
			echo.add_management_order(mo_completion, id);

			//Change the ratio of the swarm when its finished
			ManagementOrder* mo_ratios=new ChangeSwarm(15, 1, 0);
			mo_ratios->add_condition(new NotUnderConstruction);
			echo.add_management_order(mo_ratios, id);

			//Add a tracker
			ManagementOrder* mo_tracker=new AddRessourceTracker;
			mo_tracker->add_condition(new NotUnderConstruction);
			echo.add_management_order(mo_tracker, id);

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
			//The main order for the race track
			AIEcho::Construction::BuildingOrder bo(echo.player, IntBuildingType::WALKSPEED_BUILDING, 6);
	
			//Constraints arround the location of wood
			AIEcho::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
			//You want to be close to wood
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 4));

			//Constraints arround the location of stone
			AIEcho::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
			//You want to be close to stone
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, 1));
			//But not to close, so you have room to upgrade
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, 2));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

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
			//The main order for the swimming pool
			AIEcho::Construction::BuildingOrder bo(echo.player, IntBuildingType::SWIMSPEED_BUILDING, 6);
	
			//Constraints arround the location of wood
			AIEcho::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
			//You want to be close to wood
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, 4));

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 1));

			//Constraints arround the location of stone
			AIEcho::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
			//You don't want to be too close, so you have room to upgrade
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, 2));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

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
			AIEcho::Construction::BuildingOrder bo(echo.player, IntBuildingType::SCIENCE_BUILDING, 5);

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo.add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo.add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 4));

			//Constraints arround the enemy
			AIEcho::Gradients::GradientInfo gi_enemy;
			for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
			{
				gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
			}
			gi_enemy.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			bo.add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, 3));

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
				UpgradeRepairOrder* uro = new UpgradeRepairOrder(echo, buildings[chosen], 8);
				echo.add_upgrade_repair_order(uro);

				if(echo.get_building_register().get_type(buildings[chosen])==IntBuildingType::FOOD_BUILDING)
				{
					ManagementOrder* mo_tracker_pause=new PauseRessourceTracker;
					mo_tracker_pause->add_condition(new UnderConstruction);
					echo.add_management_order(mo_tracker_pause, buildings[chosen]);

					ManagementOrder* mo_tracker_unpause=new UnPauseRessourceTracker;
					mo_tracker_unpause->add_condition(new NotUnderConstruction);
					echo.add_management_order(mo_tracker_unpause, buildings[chosen]);

					ManagementOrder* mo_completion=new AssignWorkers(3);
					mo_completion->add_condition(new NotUnderConstruction);
					echo.add_management_order(mo_completion, buildings[chosen]);
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
				UpgradeRepairOrder* uro = new UpgradeRepairOrder(echo, buildings[chosen], 8);
				echo.add_upgrade_repair_order(uro);

				if(echo.get_building_register().get_type(buildings[chosen])==IntBuildingType::FOOD_BUILDING)
				{
					ManagementOrder* mo_tracker_pause=new PauseRessourceTracker;
					mo_tracker_pause->add_condition(new UnderConstruction);
					echo.add_management_order(mo_tracker_pause, buildings[chosen]);

					ManagementOrder* mo_tracker_unpause=new UnPauseRessourceTracker;
					mo_tracker_unpause->add_condition(new NotUnderConstruction);
					echo.add_management_order(mo_tracker_unpause, buildings[chosen]);

					ManagementOrder* mo_completion=new AssignWorkers(6);
					mo_completion->add_condition(new NotUnderConstruction);
					echo.add_management_order(mo_completion, buildings[chosen]);
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
			if(rt->get_age()>1500)
			{
				if(rt->get_average_level() < 24*(echo.get_building_register().get_level(*i)+1))
				{
					ManagementOrder* mo_destroy=new DestroyBuilding;
					echo.add_management_order(mo_destroy, *i);
				}
			}
		}


		BuildingSearch swarms(echo);
		swarms.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
		swarms.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=swarms.begin(); i!=swarms.end(); ++i)
		{
			boost::shared_ptr<RessourceTracker> rt=echo.get_ressource_tracker(*i);
			if(rt->get_age()>2500)
			{
				if(rt->get_average_level() < 18)
				{
					ManagementOrder* mo_destroy=new DestroyBuilding;
					echo.add_management_order(mo_destroy, *i);
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
		for(int x=0; x<echo.player->map->getW(); ++x)
		{

			for(int y=0; y<echo.player->map->getH(); ++y)
			{
				if((x%2==1 && y%2==1))
				{
					if((!echo.player->map->isRessourceTakeable(x, y, WOOD) &&
					    !echo.player->map->isRessourceTakeable(x, y, CORN)) &&
					    echo.player->map->isForbidden(x, y, echo.player->team->me))
					{
						mo_non_farming->add_location(x, y);
					}
					else
					{
						if((echo.player->map->isRessourceTakeable(x, y, WOOD) ||
						    echo.player->map->isRessourceTakeable(x, y, CORN)) &&
						    echo.player->map->isMapDiscovered(x, y, echo.player->team->me) &&
						    !echo.player->map->isForbidden(x, y, echo.player->team->me) &&
						    gradient.get_height(x, y)<10)
						{
							mo_farming->add_location(x, y);
						}
					}
				}
			}
		}
		echo.add_global_management_order(mo_farming);
		echo.add_global_management_order(mo_non_farming);
	}
}
