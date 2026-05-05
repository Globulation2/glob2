// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AIEcho.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "building_type.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include <tuple>
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using std::shared_ptr;


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
		constraints[x] = std::shared_ptr<Constraint>(Constraint::load_constraint(stream, player, versionMinor));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	stream->readEnterSection("conditions");
	size = stream->readUint32("size");
	conditions.resize(size);
	for(unsigned x=0; x<size; ++x)
	{
		stream->readEnterSection(x);
		conditions[x] = std::shared_ptr<Condition>(Condition::load_condition(stream, player, versionMinor));
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
	constraints.push_back(std::shared_ptr<Constraint>(constraint));
}


void BuildingOrder::add_condition(Condition* condition)
{
	conditions.push_back(std::shared_ptr<Condition>(condition));
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
			for(std::vector<std::shared_ptr<Constraint> >::iterator i=constraints.begin(); i!=constraints.end(); ++i)
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
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* b=player->team->myBuildings[i];
		if(b!=NULL)
		{
			found_buildings[building_id++]=std::make_tuple(b->posX, b->posY, b->type->shortTypeNum, b->gid, false);
		}
	}
}



unsigned int BuildingRegister::register_building()
{
	pending_buildings[building_id]=std::make_tuple(-1, -1, -1, -1);
	return building_id++;
}



void BuildingRegister::issue_order(int id, int x, int y, int building_type)
{
	pending_buildings[id]=std::make_tuple(x, y, building_type, 0);
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
		pending_buildings[id]=std::make_tuple(x, y, type, ticks);
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
		found_buildings[id]=std::make_tuple(xpos, ypos, building_type, gid, t);
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
		stream->writeSint32(std::get<0>(i->second), "xpos");
		stream->writeSint32(std::get<1>(i->second), "ypos");
		stream->writeSint32(std::get<2>(i->second), "building_type");
		stream->writeSint32(std::get<3>(i->second), "ticks_since_registered");
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
		stream->writeUint32(std::get<0>(i->second), "xpos");
		stream->writeUint32(std::get<1>(i->second), "ypos");
		stream->writeUint32(std::get<2>(i->second), "building_type");
		stream->writeUint32(std::get<3>(i->second), "gid");
		if(std::get<4>(i->second))
			stream->writeUint8(1, "upgrade_status");
		else if(!std::get<4>(i->second))
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
	std::get<4>(found_buildings[id])=indeterminate;
}




void BuildingRegister::tick()
{
	for(pending_iterator i=pending_buildings.begin(); i!=pending_buildings.end();)
	{
		//When get<3>() is -1, it means that the building order hasen't been sent to the glob2 engine yet.
		//This is used when the building is registered, but awaiting conditions to be satisfied.
		if(std::get<3>(i->second)!=-1)
		{
			std::get<3>(i->second)++;
			if(std::get<3>(i->second) > 300)
			{
				pending_iterator current=i;
				++i;
				pending_buildings.erase(current);
				continue;
			}
			int gbid=NOGBID;
			if(std::get<2>(i->second) > IntBuildingType::DEFENSE_BUILDING && std::get<2>(i->second) < IntBuildingType::STONE_WALL)
			{
				gbid=is_flag(echo, std::get<0>(i->second), std::get<1>(i->second));
			}
			else
			{
				gbid=player->map->getBuilding(std::get<0>(i->second), std::get<1>(i->second));
			}
			if(gbid!=NOGBID)
			{
				if(std::get<2>(i->second) > IntBuildingType::DEFENSE_BUILDING && std::get<2>(i->second) < IntBuildingType::STONE_WALL)
				{
					echo.get_flag_map().set_flag(std::get<0>(i->second), std::get<1>(i->second), gbid);
				}
				found_buildings[i->first]=std::make_tuple(std::get<0>(i->second), std::get<1>(i->second), std::get<2>(i->second), gbid, false);
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
		if(std::get<2>(i->second) > IntBuildingType::DEFENSE_BUILDING && std::get<2>(i->second) < IntBuildingType::STONE_WALL)
		{
			if(echo.get_flag_map().get_flag(std::get<0>(i->second), std::get<1>(i->second))==NOGBID)
			{
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
			if(player->team->myBuildings[::Building::GIDtoID(std::get<3>(i->second))]==NULL)
			{
				echo.get_flag_map().set_flag(std::get<0>(i->second), std::get<1>(i->second), NOGBID);
				found_iterator current=i;
				++i;
				found_buildings.erase(current);
				continue;
			}
		}
		else
		{
			const int gbid=player->map->getBuilding(std::get<0>(i->second), std::get<1>(i->second));
			if(gbid==NOGBID || gbid != std::get<3>(i->second))
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
			if(std::get<4>(i->second))
			{
				std::get<0>(i->second)=b->posX;
				std::get<1>(i->second)=b->posY;
				if(b->constructionResultState==::Building::NO_CONSTRUCTION)
				{
					std::get<4>(i->second)=false;
				}
			}
			//False
			else if(!std::get<4>(i->second))
			{

			}
			//Indeterminate
			else
			{
				if(b->constructionResultState!=::Building::NO_CONSTRUCTION)
				{
					std::get<4>(i->second)=true;
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
	
	tribool v=std::get<4>(found_buildings[id]);
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
	return player->team->myBuildings[::Building::GIDtoID(std::get<3>(found_buildings[id]))];
}



BuildingType* BuildingRegister::get_building_type(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return NULL;
	}
	return player->team->myBuildings[::Building::GIDtoID(std::get<3>(found_buildings[id]))]->type;
}



int BuildingRegister::get_type(unsigned int id)
{
	if(found_buildings.find(id)==found_buildings.end())
	{
		return 0;
	}
	return std::get<2>(found_buildings[id]);
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


