// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include <limits>
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "GlobalContainer.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Conditions;
using namespace boost::logic;


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
