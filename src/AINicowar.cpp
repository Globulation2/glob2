/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include <Stream.h>

#include "AINicowar.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"
#include <algorithm>
#include <iterator>

using namespace std;

AINicowar::AINicowar(Player *player)
{
	init(player);
}




AINicowar::AINicowar(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	bool goodLoad=load(stream, player, versionMinor);
	assert(goodLoad);
}




void AINicowar::init(Player *player)
{
	timer=0;
	iteration=0;
	desired_explorers=0;
	developing_attack_explorers=false;
	explorer_attacking=false;
	enemy=NULL;
	changeUnits("upgrade-repair manager", WORKER, static_cast<int>(std::max(MAXIMUM_TO_REPAIR, MAXIMUM_TO_UPGRADE)*MAX_CONSTRUCTION_AT_ONCE*1.5), std::max(MAXIMUM_TO_REPAIR, MAXIMUM_TO_UPGRADE), 0);

	changeUnits("defense", WARRIOR, BASE_DEFENSE_WARRIORS, 0, 0);

	changeUnits("attack", WARRIOR, BASE_ATTACK_WARRIORS, 0, 0);

	assert(player);

	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}




AINicowar::~AINicowar()
{
}




bool AINicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	stream->readEnterSection("AINicowar");
	timer=stream->readSint32("timer");
	iteration=stream->readSint32("iteration");

	stream->readEnterSection("orders");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		size_t size=stream->readUint32();
		Uint8* buffer = new Uint8[size];
		stream->read(buffer, size);
		orders.push(Order::getOrder(buffer, size));
	}
	stream->readLeaveSection();

	stream->readEnterSection("active_construction");
	n = stream->readUint32();
	while(n--)
	{
		constructionRecord cr;
		cr.building=getBuildingFromGid(stream->readUint32());
		cr.assigned=stream->readUint32();
		cr.original=stream->readUint32();
		cr.is_repair=stream->readUint8();
		active_construction.push_back(cr);
	}
	stream->readLeaveSection();

	stream->readEnterSection("pending_construction");
	n = stream->readUint32();
	while(n--)
	{
		constructionRecord cr;
		cr.building=getBuildingFromGid(stream->readUint32());
		cr.assigned=stream->readUint32();
		cr.original=stream->readUint32();
		cr.is_repair=stream->readUint8();
		pending_construction.push_back(cr);
	}
	stream->readLeaveSection();

	stream->readEnterSection("active_exploration");
	n = stream->readUint32();
	while(n--)
	{
		explorationRecord er;
		unsigned int gid=stream->readUint32();
		if(!gid)
			er.flag=NULL;
		else
			er.flag=getBuildingFromGid(gid);
		er.flag_x=stream->readUint32();
		er.flag_y=stream->readUint32();
		er.zone_x=stream->readUint32();
		er.zone_y=stream->readUint32();
		er.width=stream->readUint32();
		er.height=stream->readUint32();
		er.assigned=stream->readUint32();
		er.radius=stream->readUint32();
		er.isAssaultFlag=stream->readUint8();
		active_exploration.push_back(er);
	}
	stream->readLeaveSection();
	desired_explorers=stream->readUint32("desired_explorers");
	developing_attack_explorers=stream->readUint8("developing_attack_explorers");
	explorer_attacking=stream->readUint8("explorer_attacking");

	stream->readEnterSection("attacks");
	n = stream->readUint32();
	while(n--)
	{
		attackRecord ar;
		ar.target=stream->readUint32();
		unsigned int gbid=stream->readUint32();
		if(gbid==NOGBID)
			ar.flag=NULL;
		else
			ar.flag=getBuildingFromGid(gbid);
		ar.flagx=stream->readUint32();
		ar.flagy=stream->readUint32();
		ar.zonex=stream->readUint32();
		ar.zoney=stream->readUint32();
		ar.width=stream->readUint32();
		ar.height=stream->readUint32();
		ar.unitx=stream->readUint32();
		ar.unity=stream->readUint32();
		ar.unit_width=stream->readUint32();
		ar.unit_height=stream->readUint32();
		ar.assigned_units=stream->readUint32();
		ar.assigned_level=stream->readUint32();
		attacks.push_back(ar);
	}
	stream->readLeaveSection();

	stream->readEnterSection("module_demands");
	n = stream->readUint32();
	while(n--)
	{
		unitRecord ur;
		string name=stream->readText();
		for(int i=0; i<NB_UNIT_TYPE; ++i)
		{
			ur.desired_units[i]=stream->readUint32();
			ur.required_units[i]=stream->readUint32();
			ur.emergency_units[i]=stream->readUint32();
		}
		module_demands[name]=ur;
	}
	stream->readLeaveSection();

	stream->readEnterSection("inns");
	n = stream->readUint32();
	while(n--)
	{
		innRecord ir;
		unsigned int gid=stream->readUint32();
		ir.pos=stream->readUint32();
		unsigned int size=stream->readUint32();
		for(unsigned int i=0; i<size; ++i)
		{
			ir.records[i].food_amount=stream->readUint32();
			ir.records[i].units_eating=stream->readUint32();
		}
		inns[gid]=ir;
	}
	stream->readLeaveSection();

	stream->readEnterSection("defending_zones");
	n = stream->readUint32();
	while(n--)
	{
		defenseRecord dr;
		unsigned int gbid=stream->readUint32();
		if(gbid==NOGBID)
			dr.flag=NULL;
		else
			dr.flag=getBuildingFromGid(gbid);
		dr.flagx=stream->readUint32();
		dr.flagy=stream->readUint32();
		dr.zonex=stream->readUint32();
		dr.zoney=stream->readUint32();
		dr.width=stream->readUint32();
		dr.height=stream->readUint32();
		dr.assigned=stream->readUint32();
		defending_zones.push_back(dr);
	}
	stream->readLeaveSection();

	stream->readEnterSection("new_buildings");
	n = stream->readUint32();
	while(n--)
	{
		newConstructionRecord ncr;
		ncr.building = stream->readUint32();
		ncr.x = stream->readUint32();
		ncr.y = stream->readUint32();
		ncr.assigned = stream->readUint32();
		ncr.building_type = stream->readUint32();
		new_buildings.push_back(ncr);
	}

	stream->readEnterSection("num_buildings_wanted");
	n = stream->readUint32();
	while(n--)
	{
		num_buildings_wanted[stream->readUint32()] = stream->readUint32();
	}

	stream->readLeaveSection();
	return true;
}




void AINicowar::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINicowar");
	stream->writeSint32(timer, "timer");
	stream->writeSint32(iteration, "iteration");

	stream->writeEnterSection("orders");
	stream->writeUint32(orders.size());
	while(orders.size())
	{
		Order* order = orders.front();
		orders.pop();
		stream->writeUint32(order->getDataLength()+1);
		stream->writeSint8(order->getOrderType());
		stream->write(order->getData(), order->getDataLength());
		delete order;
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("active_construction");
	stream->writeUint32(active_construction.size());
	for(std::list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); ++i)
	{
		stream->writeUint32(i->building->gid);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->original);
		stream->writeUint8(i->is_repair);
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("pending_construction");
	stream->writeUint32(pending_construction.size());
	for(std::list<constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end(); ++i)
	{
		stream->writeUint32(i->building->gid);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->original);
		stream->writeUint8(i->is_repair);
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("active_exploration");
	stream->writeUint32(active_exploration.size());
	for(std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
	{
		if(i->flag!=NULL)
			stream->writeUint32(i->flag->gid);
		else
			stream->writeUint32(0);
		stream->writeUint32(i->flag_x);
		stream->writeUint32(i->flag_y);
		stream->writeUint32(i->zone_x);
		stream->writeUint32(i->zone_y);
		stream->writeUint32(i->width);
		stream->writeUint32(i->height);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->radius);
		stream->writeUint8(i->isAssaultFlag);
	}
	stream->writeLeaveSection();
	stream->writeUint32(desired_explorers, "desired_explorers");
	stream->writeUint8(developing_attack_explorers, "developing_attack_explorers");
	stream->writeUint8(explorer_attacking, "explorer_attacking");

	stream->writeEnterSection("attacks");
	stream->writeUint32(attacks.size());
	for(std::vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
	{
		stream->writeUint32(i->target);
		if(i->flag==NULL)
			stream->writeUint32(NOGBID);
		else
			stream->writeUint32(i->flag->gid);
		stream->writeUint32(i->flagx);
		stream->writeUint32(i->flagy);
		stream->writeUint32(i->zonex);
		stream->writeUint32(i->zoney);
		stream->writeUint32(i->width);
		stream->writeUint32(i->height);
		stream->writeUint32(i->unitx);
		stream->writeUint32(i->unity);
		stream->writeUint32(i->unit_width);
		stream->writeUint32(i->unit_height);
		stream->writeUint32(i->assigned_units);
		stream->writeUint32(i->assigned_level);
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("module_demands");
	stream->writeUint32(module_demands.size());
	for(std::map<string, unitRecord>::iterator i = module_demands.begin(); i!=module_demands.end(); ++i)
	{
		stream->writeText(i->first);
		for(int n=0; n<NB_UNIT_TYPE; ++n)
		{
			stream->writeUint32(i->second.desired_units[n]);
			stream->writeUint32(i->second.required_units[n]);
			stream->writeUint32(i->second.emergency_units[n]);
		}
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("inns");
	stream->writeUint32(inns.size());
	for(std::map<int, innRecord>::iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		stream->writeUint32(i->first);
		stream->writeUint32(i->second.pos);
		stream->writeUint32(i->second.records.size());
		for(std::vector<singleInnRecord>::iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			stream->writeUint32(record->food_amount);
			stream->writeUint32(record->units_eating);
		}
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("defending_zones");
	stream->writeUint32(defending_zones.size());
	for(std::vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end(); ++i)
	{
		if(i->flag==NULL)
			stream->writeUint32(NOGBID);
		else
			stream->writeUint32(i->flag->gid);
		stream->writeUint32(i->flagx);
		stream->writeUint32(i->flagy);
		stream->writeUint32(i->zonex);
		stream->writeUint32(i->zoney);
		stream->writeUint32(i->width);
		stream->writeUint32(i->height);
		stream->writeUint32(i->assigned);
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("new_buildings");
	stream->writeUint32(new_buildings.size());
	for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i!=new_buildings.end(); ++i)
	{
		stream->writeUint32(i->building);
		stream->writeUint32(i->x);
		stream->writeUint32(i->y);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->building_type);
	}

	stream->writeEnterSection("num_buildings_wanted");
	stream->writeUint32(num_buildings_wanted.size());
	for(std::map<unsigned int, unsigned int>::iterator i = num_buildings_wanted.begin(); i!=num_buildings_wanted.end(); ++i)
	{
		stream->writeUint32(i->first);
		stream->writeUint32(i->second);
	}
}




Order *AINicowar::getOrder(void)
{
	//This code enables the whole map to be seen for testing.
	/*	if(timer==1)
		{
			for(int x=0; x<map->getW(); ++x)
			{
				for(int y=0; y<map->getH(); ++y)
				{
					map->setMapDiscovered(x, y, team->me);
				}
			}
		}
	*/

	//See if there is an existing order that the AI wanted to have done
	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}
	//Putting this at the start allowed for certain bugs to creep in where it would skip past
	//the time where it should have done something because there where items already in the queue.
	timer++;
	//Waits for atleast one iteration before it does anything, because of a few off, 'goes to fast' bugs.
	if (timer<TIMER_ITERATION)
		return new NullOrder();

	if(timer%TIMER_ITERATION==0)
	{
		iteration+=1;
		std::cout<<"AINicowar: getOrder: ******Entering iteration "<<iteration<<" at tick #"<<timer<<". ******"<<endl;
	}

	//The ai does stuff in iterations.
	switch(timer%TIMER_ITERATION)
	{
		case removeOldConstruction_TIME:
			//Remove any upgrade records for upgrades that where finished.
			removeOldConstruction();
			break;

		case updatePendingConstruction_TIME:
			//Update unit assignments on buildings that have gone into construction mode.
			updatePendingConstruction();
			break;

		case reassignConstruction_TIME:
			//Reassign the units on theupgrades on buildings
			reassignConstruction();
			break;

		case startNewConstruction_TIME:
			//Try and make an upgrade to a building.
			startNewConstruction();
			break;
		case exploreWorld_TIME:
			//Create, move, or destroy flags as neccecary to explore the world
			exploreWorld();
			break;
		case findCreatedFlags_TIME:
			//Find flags created by exploreWorld and add them to the record books
			findCreatedFlags();
			break;
		case moderateSwarmsForExplorers_TIME:
			//Modify swarm ratios in order to maintain the desired number of explorers.
			moderateSwarmsForExplorers();
			break;
		case explorerAttack_TIME:
			//Attack using explorers if possible
			explorerAttack();
			break;
		case targetEnemy_TIME:
			//Chooses an enemy to attack
			targetEnemy();
			break;
		case attack_TIME:
			//Targets and attacks a building
			attack();
			break;
		case updateAttackFlags_TIME:
			//Finds flags that attack() created and reassigns them as neccecary
			updateAttackFlags();
			break;
		case moderateSwarms_TIME:
			//Change the ratios on the swarms if neccecary
			moderateSwarms();
			break;
		case recordInns_TIME:
			//Record the number of units and amount of food in each inn
			recordInns();
			break;
		case modifyInns_TIME:
			//Modify the top and bottom usage inns appropriettly
			modifyInns();
			break;
		case controlTowers_TIME:
			//Assigns units to towers
			controlTowers();
			break;
		case updateFlags_TIME:
			//updates defense flags as neccecary
			updateFlags();
			break;
		case findDefense_TIME:
			//Looks for enemy units attacking, and defends that zone if it isn't already
			findDefense();
			break;
		case findCreatedDefenseFlags_TIME:
			//Finds flags that findDefense() created and puts them into the records
			findCreatedDefenseFlags();
			break;
		case constructBuildings_TIME:
			//Constructs new buildings that have been queued.
			constructBuildings();
			break;
		case updateBuildings_TIME:
			//Updates buildings that constructBuildings created.
			updateBuildings();
			break;
		case calculateBuildings_TIME:
			//calculates the number of buildings the player should have
			calculateBuildings();
			break;
	}

	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}

	return new NullOrder();

}




bool AINicowar::isFreeOfFlags(unsigned int x, unsigned int y)
{
	Building** myBuildings=team->myBuildings;
	for (int i=0; i<1024; i+=1)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG ||
				b->type->shortTypeNum==IntBuildingType::WAR_FLAG ||
				b->type->shortTypeNum==IntBuildingType::CLEARING_FLAG)
			{
				if (b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
				{
					return false;
				}
			}
		}
	}
	return true;
}




bool AINicowar::buildingStillExists(Building* building)
{
	for (int i=0; i<32; i++)
	{
		Team* t = game->teams[i];
		if(t)
		{
			for(int i=0; i<1024; i++)
			{
				Building* b = t->myBuildings[i];
				if (b)
				{
					if(b == building)
						return true;
				}
			}
		}
	}
	return false;
}




bool AINicowar::buildingStillExists(unsigned int gid)
{
	return getBuildingFromGid(gid)!=NULL;
}




unsigned int AINicowar::pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollModifier mod, pollType poll_type)
{
	if (poll_type == NONE)
		return 0;

	unsigned int bound_h=x+width;
	if(static_cast<int>(bound_h)>map->getW())
		bound_h-=map->getW();

	unsigned int bound_y=y+height;
	if(static_cast<int>(bound_y)>map->getH())
		bound_y-=map->getH();

	unsigned int score=0;
	for(; x!=bound_h; ++x)
	{
		if(static_cast<int>(x) >= map->getW())
			x=0;

		for(; y!=bound_y; ++y)
		{
			if(static_cast<int>(y) >= map->getH())
				y=0;

			Unit* u;
			Building* b;
			switch (poll_type)
			{

				case HIDDEN_SQUARES:
					if(!map->isMapDiscovered(x, y, team->me))
					{
						score++;
					}
					break;
				case VISIBLE_SQUARES:
					if(map->isMapDiscovered(x, y, team->me))
					{
						score++;
					}
					break;
				case ENEMY_BUILDINGS:
					b = getBuildingFromGid(map->getBuilding(x, y));
					if (b)
					{
						if(b->owner->me&team->enemies)
						{
							score++;
						}
					}
					break;
				case FRIENDLY_BUILDINGS:
					b = getBuildingFromGid(map->getBuilding(x, y));
					if (b)
					{
						if(b->owner->me == team->me)
						{
							score++;
						}
					}
					break;
				case ENEMY_UNITS:
					u = getUnitFromGid(map->getGroundUnit(x, y));
					if (u)
					{
						if(u->owner->me&team->enemies)
						{
							score++;
						}
					}
					break;
				case POLL_CORN:
					if (map->isRessource(x, y, CORN))
						score++;
					break;
				case POLL_TREES:
					if (map->isRessource(x, y, WOOD))
						score++;
					break;
				case POLL_STONE:
					if (map->isRessource(x, y, STONE))
						score++;
					break;
				case NONE:
					break;
			}
		}
	}
	switch (mod)
	{
		case MAXIMUM:
			return score;
			break;
		case MINIMUM:
			return width*height-score;
			break;
	}
	return 0;
}




AINicowar::zone AINicowar::getZone(unsigned int x, unsigned int y, unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap)
{
	int free_width=area_width-horizontal_overlap;
	int free_height=area_height-vertical_overlap;
	zone z;
	z.x = x-(x%free_width);
	z.y = y-(x%free_height);
	z.width=area_width;
	z.height=area_height;
	return z;
}




std::vector<AINicowar::pollRecord> AINicowar::pollMap(unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap, unsigned int requested_spots, pollModifier mod, pollType poll_type)
{

	vector<AINicowar::pollRecord> best;
	best.reserve((map->getW()/(area_width-horizontal_overlap))*
		map->getH()/(area_height-vertical_overlap));

	for (unsigned int x=0; x<=map->getW()-area_width; x+=(area_width)-(horizontal_overlap))
	{
		for (unsigned int y=0; y<=map->getH()-area_height; y+=(area_height)-(vertical_overlap))
		{
			int score=pollArea(x, y, area_width, area_height, mod, poll_type);
			best.push_back(pollRecord(x, y, area_width, area_height, score, poll_type));
		}
	}
	std::sort(best.begin(), best.end(), greater<pollRecord>());
	best.erase(best.begin()+requested_spots, best.end());
	return best;
}




int AINicowar::getPositionScore(const std::vector<pollRecord>& polls, const std::vector<pollRecord>::const_iterator& iter)
{
	int score=0;
	for(std::vector<pollRecord>::const_iterator i = polls.begin(); (i+1)!=polls.end(); ++i)
	{
		if(i==iter)
		{
			break;
		}
		if(i->score != (i+1)->score)
		{
			score++;
		}
	}
	return score;
}




std::vector<AINicowar::zone> AINicowar::getBestZones(pollModifier amod, pollType a, pollModifier bmod, pollType b, pollModifier cmod, pollType c, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height, unsigned int minimum_friendly_buildings)
{
	std::vector<pollRecord> a_list;
	std::vector<pollRecord> b_list;
	std::vector<pollRecord> c_list;

	for (unsigned int x=0; x<=map->getW()-width; x+=width-horizontal_overlap)
	{
		for (unsigned int y=0; y<=map->getH()-height; y+=height-vertical_overlap)
		{
			int full_x=x-extention_width;
			if(full_x<0)
				full_x=map->getW()+full_x;
			int full_y=y-extention_height;
			if(full_y<0)
				full_y=map->getH()+full_y;

			int full_w=width+2*extention_width;
			int full_h=height+2*extention_height;
			if(pollArea(full_x, full_y, full_w, full_h, MAXIMUM, FRIENDLY_BUILDINGS)<minimum_friendly_buildings)
				continue;
			pollRecord pr_a;
			pr_a.x=x;
			pr_a.y=y;
			pr_a.width=width;
			pr_a.height=height;
			pollRecord pr_b=pr_a;
			pollRecord pr_c=pr_b;
			pr_a.score=pollArea(full_x, full_y, full_w, full_h, amod, a);
			pr_b.score=pollArea(full_x, full_y, full_w, full_h, bmod, b);
			pr_c.score=pollArea(full_x, full_y, full_w, full_h, cmod, c);
			a_list.push_back(pr_a);
			b_list.push_back(pr_b);
			c_list.push_back(pr_c);
		}
	}

	std::sort(a_list.begin(), a_list.end());
	std::sort(b_list.begin(), b_list.end());
	std::sort(c_list.begin(), c_list.end());

	std::vector<threeTierRecord> final;
	for (std::vector<pollRecord>::iterator i1=a_list.begin(); i1!=a_list.end(); ++i1)
	{
		threeTierRecord ttr;
		ttr.x=i1->x;
		ttr.y=i1->y;
		ttr.width=i1->width;
		ttr.height=i1->height;
		ttr.score_a=getPositionScore(a_list, i1);
		for (std::vector<pollRecord>::iterator i2=b_list.begin(); i2!=b_list.end(); ++i2)
		{
			if(i2->x==ttr.x && i2->y==ttr.y)
				ttr.score_b=getPositionScore(b_list, i2);
		}

		for (std::vector<pollRecord>::iterator i3=c_list.begin(); i3!=c_list.end(); ++i3)
		{
			if(i3->x==ttr.x && i3->y==ttr.y)
				ttr.score_c=getPositionScore(c_list, i3);
		}
		final.push_back(ttr);
	}

	std::sort(final.begin(), final.end());

	std::vector<zone> return_list;
	//For some reason, the final list is being sorted backwards. It seems to be because of my lack of thouroughly thinking out
	//how the various comparisons relate.
	for(std::vector<threeTierRecord>::reverse_iterator i=final.rbegin(); i!=final.rend(); ++i)
	{
		zone z;
		z.x=i->x;
		z.y=i->y;
		z.width=i->width;
		z.height=i->height;
		return_list.push_back(z);
	}

	return return_list;
}




void AINicowar::removeOldConstruction(void)
{
	for (std::list<AINicowar::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(b))
		{
			i=active_construction.erase(i);
			continue;
		}

		int original = i->original;
		if (b->constructionResultState==Building::NO_CONSTRUCTION)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: removeOldConstruction: Removing an old "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" from the active construction list, changing assigned number of units back to "<<original<<" from "<<i->assigned<<"."<<endl;
			i=active_construction.erase(i);
			orders.push(new OrderModifyBuilding(b->gid, original));
			continue;
		}
		i++;
	}
}




void AINicowar::updatePendingConstruction(void)
{
	for (std::list<AINicowar::constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(b))
		{
			i=active_construction.erase(i);
			continue;
		}
		int assigned = i->assigned;
		if (b->buildingState != Building::WAITING_FOR_CONSTRUCTION && b->buildingState != Building::WAITING_FOR_CONSTRUCTION_ROOM)
		{
			AINicowar::constructionRecord u=*i;
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updatePendingConstruction: The "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" was found that it is no longer pending construction, I am assigning number of requested units, "<<assigned<<", to it."<<endl;
			active_construction.push_back(u);
			i=pending_construction.erase(i);
			orders.push(new OrderModifyBuilding(b->gid, assigned));
			continue;
		}
		i++;
	}
}




int AINicowar::getAvailableUnitsForConstruction(int level)
{
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(BUILD, j+1);
	}

	int total_free = 0;
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		if ((j+1)>=level)
			total_free+=free_workers[j];
	}
	return total_free;
}




void AINicowar::reassignConstruction(void)
{
	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(BUILD, j+1);
	}

	//Add in the numbers of units that are already working
	for (list<AINicowar::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		for (std::list<Unit*>::iterator i = b->unitsWorking.begin(); i!=b->unitsWorking.end(); i++)
		{
			Unit* u=*i;
			free_workers[u->level[BUILD]]+=1;
		}
	}

	//Finally, iterate through the shuffled list of records changing the number of units allocated to upgrade the buildings.
	for (list<AINicowar::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		unsigned int assigned=i->assigned;
		bool is_repair=i->is_repair;

		//Find the number of workers with enough of a level to upgrade this building
		int available_upgrade = 0;
		int available_repair  = 0;
		for (int j = 0; j<NB_UNIT_LEVELS; j++)
		{
			//Important, b->type->level takes on the level number of the building its being upgraded to.
			//So in this function, b->type->level will be different then in startNewConstruction for
			//buildings being upgraded. I have copy and pasted this section of code a thousand times
			//between the two when I change it and each time it causes me trouble.
			if (j > (b->type->level)-1 )
			{
				available_upgrade+=free_workers[j];
			}
			if (j >= (b->type->level) )
			{
				available_repair+=free_workers[j];
			}
		}

		if (!is_repair && available_upgrade==0)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: There are not enough available units. Canceling upgrade on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		else if (is_repair && available_repair==0)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: There are not enough available units. Canceling repair on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		//Issue the command to change the number of units working on the building to the new amount
		unsigned int num_to_assign=0;
		unsigned int generic_available=0;
		if (!is_repair)
		{
			num_to_assign=available_upgrade;
			generic_available=available_upgrade;
			if (num_to_assign>MAXIMUM_TO_UPGRADE)
				num_to_assign=MAXIMUM_TO_UPGRADE;
		}
		else if (is_repair)
		{
			num_to_assign=available_repair;
			generic_available=available_repair;
			if (num_to_assign>MAXIMUM_TO_REPAIR)
				num_to_assign=MAXIMUM_TO_REPAIR;
		}

		if (num_to_assign != assigned)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: Retasking "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" that is under construction. Number of units available: "<<generic_available<< ". Number of units originally assigned: "<<assigned<<". Number of units assigning: "<<num_to_assign<<"."<<endl;
			orders.push(new OrderModifyBuilding(b->gid, num_to_assign));
			i->assigned=num_to_assign;
		}
		reduce(free_workers, b->type->level, num_to_assign);
	}
}




int AINicowar::getFreeUnits(int ability, int level)
{
	level-=1;
	//This is because ability levels are counted from 0, not 1. I'm not sure why,
	//though, because 0 would mean that the unit does not have that skill, which
	//would be more appropriette.
	int free_workers=0;
	Unit **myUnits=team->myUnits;

	for (int i=0; i<1024; i++)
	{
		Unit* u = myUnits[i];
		if (u)
		{
			if ( (u->activity==Unit::ACT_RANDOM || u->movement==Unit::MOV_RANDOM_GROUND) && u->level[ability]==level && u->medical==Unit::MED_FREE)
			{
				free_workers+=1;
			}
		}
	}
	return free_workers;
}




void AINicowar::startNewConstruction(void)
{
	//If we already have more than our max, don't do any more
	if (active_construction.size()+pending_construction.size()>=MAX_CONSTRUCTION_AT_ONCE)
		return;

	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(BUILD, j+1);
	}

	//Look through each building we previoussly assigned to be upgraded, and subtract their requested number of units from the free ones (ones that they haven't recieved yet but wanted.). Also take not of the type of building, counting up the numbers for each type.
	int construction_counts[IntBuildingType::NB_BUILDING];
	for (int j=0; j!=IntBuildingType::NB_BUILDING; j++)
		construction_counts[j]=0;

	for (list<AINicowar::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		int assigned = i->assigned;
		reduce(free_workers, b->type->level, assigned-b->unitsWorking.size());
		construction_counts[b->type->shortTypeNum]+=1;
	}

	for (list<AINicowar::constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end(); i++)
	{
		Building *b=i->building;
		int assigned = i->assigned;
		reduce(free_workers, b->type->level, assigned-b->unitsWorking.size());
		construction_counts[b->type->shortTypeNum]+=1;
	}

	//Make a copy of the array of buildings, then shuffle it so that every building has an equal chance of being upgraded with the spare units.
	vector<Building*> buildings;
	buildings.reserve(1024);
	for (int i=0; i<1024; i+=1)
	{
		Building *b=team->myBuildings[i];
		if (b)
			buildings.push_back(b);
	}
	random_shuffle(buildings.begin(), buildings.end());

	//Look through the buildings and find one that needs to be upgraded or repaired, and if their are atleast 4 of the correct unit type available to upgrade it, then do it.
	for (vector<Building*>::iterator i = buildings.begin(); i!=buildings.end(); i++)
	{
		Building *b=*i;
		if (b)
		{
			//Check if its worthy of having anything done to it.
			if (b->constructionResultState==Building::NO_CONSTRUCTION      &&
				b->buildingState==Building::ALIVE                          &&
				construction_counts[b->type->shortTypeNum] != MAX_BUILDING_SPECIFIC_CONSTRUCTION_LIMITS[b->type->shortTypeNum])
			{
				//Find the number of workers with enough of a level to upgrade and repair this building (if it will need it).
				unsigned int available_upgrade = 0;
				unsigned int available_repair  = 0;
				for (int j = 0; j<NB_UNIT_LEVELS; j++)
				{
					if (j > (b->type->level) )
					{
						available_upgrade+=free_workers[j];
					}
					if (j >= (b->type->level) )
					{
						available_repair+=free_workers[j];
					}
				}

				//See if the building needs repair. Note the code for initializing a repair and the code for initializing an upgrade are very similiar.
				if (b->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING         &&
					b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG       &&
					b->type->shortTypeNum!=IntBuildingType::WAR_FLAG               &&
					b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG          &&
					available_repair>=MINIMUM_TO_REPAIR                            &&
					b->hp  <  b->type->hpMax)
				{
					unsigned int num_to_assign=available_repair;
					if( num_to_assign > MAXIMUM_TO_REPAIR)
						num_to_assign=MAXIMUM_TO_REPAIR;
					if(AINicowar_DEBUG)
						cout<<"AINicowar: startNewConstruction: Found "<<available_repair<<" available workers, assigning "<<num_to_assign<<" workers to repair the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" with "<<b->maxUnitWorking<<" units already working on it."<<endl;
					AINicowar::constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=true;
					pending_construction.push_back(u);
					orders.push(new OrderConstruction(b->gid));
					reduce(free_workers, b->type->level, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}

				//If we don't need to repair, see if we can upgrade
				else if (b->type->level!=2                                     &&
					(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING     ||
					b->type->shortTypeNum==IntBuildingType::HEAL_BUILDING      ||
					b->type->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING ||
					b->type->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING ||
					b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING    ||
					b->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING   ||
					b->type->shortTypeNum==IntBuildingType::DEFENSE_BUILDING  )&&
					available_upgrade>=MINIMUM_TO_UPGRADE)
				{

					//If we have a minimum of 4 available workers, upgrade the building.
					unsigned int num_to_assign=available_upgrade;
					if( num_to_assign > MAXIMUM_TO_UPGRADE)
						num_to_assign=MAXIMUM_TO_UPGRADE;
					if(AINicowar_DEBUG)
						cout<<"AINicowar: startNewConstruction: Found "<<available_upgrade<<" available workers, assigning "<<num_to_assign<<" workers to upgrade the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" with "<<b->maxUnitWorking<<" units already working on it."<<endl;
					AINicowar::constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=false;
					pending_construction.push_back(u);
					orders.push(new OrderConstruction(b->gid));
					reduce(free_workers, b->type->level+1, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}
			}
		}
	}
}




void AINicowar::exploreWorld(void)
{
	std::vector<Building*> orphaned_flags;

	unsigned int exploration_count=0;
	unsigned int attack_count=0;
	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end();)
	{
		bool was_orphaned=false;
		if(!i->isAssaultFlag || explorer_attacking)
		{
			int score=0;
			if(i->isAssaultFlag)
				score=pollArea(i->zone_x, i->zone_y, i->width, i->height, MAXIMUM, ENEMY_BUILDINGS);
			else
				score=pollArea(i->zone_x, i->zone_y, i->width, i->height, MAXIMUM, HIDDEN_SQUARES);

			if(score==0)
			{
				orphaned_flags.push_back(i->flag);
				was_orphaned=true;
				i=active_exploration.erase(i);
			}
			else
			{
				if(i->isAssaultFlag)
					attack_count++;
				else
					exploration_count++;
			}
		}
		if (!was_orphaned)
		{
			++i;
		}
	}

	if (exploration_count==EXPLORER_MAX_REGIONS_AT_ONCE && attack_count==EXPLORER_ATTACKS_AT_ONCE)
		return;

	int wanted_exploration=EXPLORER_MAX_REGIONS_AT_ONCE-exploration_count;
	int wanted_attack=EXPLORER_ATTACKS_AT_ONCE-attack_count;
	if(! explorer_attacking )
		wanted_attack=0;
	vector<pollRecord> best = pollMap(EXPLORER_REGION_WIDTH, EXPLORER_REGION_HEIGHT, EXPLORER_REGION_HORIZONTAL_OVERLAP, EXPLORER_REGION_VERTICAL_OVERLAP, exploration_count+wanted_exploration+1, MAXIMUM, HIDDEN_SQUARES);

	vector<pollRecord> best_attack = pollMap(EXPLORER_ATTACK_AREA_WIDTH, EXPLORER_ATTACK_AREA_HEIGHT, EXPLORER_ATTACK_AREA_HORIZONTAL_OVERLAP, EXPLORER_ATTACK_AREA_VERTICAL_OVERLAP, attack_count+wanted_attack+1, MAXIMUM, ENEMY_BUILDINGS);

	std::copy(best_attack.begin(), best_attack.end(), back_insert_iterator<vector<pollRecord> >(best));

	for(vector<pollRecord>::iterator i = best.begin(); i!=best.end();)
	{
		if(!isFreeOfFlags(i->x+i->width/2, i->y+i->height/2))
		{
			i = best.erase(i);
		}
		else
		{
			++i;
		}
	}

	if (best.at(0).score==0)
	{
		for(std::vector<Building*>::iterator i = orphaned_flags.begin(); i!=orphaned_flags.end(); i++)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: exploreWorld: Removing an existing explorer flag, it is no longer needed."<<endl;
			desired_explorers-=EXPLORERS_PER_REGION;
			orders.push(new OrderDelete((*i)->gid));
		}
		return;
	}

	else
	{
		for (int i=0; i<wanted_exploration+wanted_attack; ++i)
		{
			pollRecord pr = best.at(0);
			best.erase(best.begin());
			explorationRecord exploration_record;
			//This flag will be picked up by findCreatedFlags
			exploration_record.flag=NULL;
			exploration_record.flag_x=pr.x+(pr.width/2);
			exploration_record.flag_y=pr.y+(pr.height/2);
			exploration_record.zone_x=pr.x;
			exploration_record.zone_y=pr.y;
			exploration_record.width=pr.width;
			exploration_record.height=pr.height;
			if(pr.poll_type == HIDDEN_SQUARES)
			{
				exploration_record.assigned=EXPLORERS_PER_REGION;
				exploration_record.radius=EXPLORATION_FLAG_RADIUS;
				exploration_record.isAssaultFlag=false;
			}
			else
			{
				exploration_record.assigned=EXPLORERS_PER_ATTACK;
				exploration_record.radius=EXPLORATION_FLAG_ATTACK_RADIUS;
				exploration_record.isAssaultFlag=true;
			}

			active_exploration.push_back(exploration_record);
			if(orphaned_flags.size()>0)
			{
				Building* orphaned_flag = orphaned_flags.at(0);
				orphaned_flags.erase(orphaned_flags.begin());
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: exploreWorld: Moving an existing explorer flag from position "<<orphaned_flag->posX<<","<<orphaned_flag->posY<<" to position "<<exploration_record.flag_x<<","<<exploration_record.flag_y<<"."<<endl;
				orders.push(new OrderMoveFlag(orphaned_flag->gid, exploration_record.flag_x, exploration_record.flag_y, true));
				return;
			}
			else
			{
				Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: exploreWorld: Creating a new flag at position "<<exploration_record.flag_x<<","<<exploration_record.flag_y<<"."<<endl;
				desired_explorers+=EXPLORERS_PER_REGION;
				orders.push(new OrderCreate(team->teamNumber, exploration_record.flag_x, exploration_record.flag_y,typeNum));
			}
		}
	}
}




void AINicowar::findCreatedFlags(void)
{
	//Find the record that has NULL for a flag, if it exists.
	std::vector<explorationRecord*> explr;
	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
	{
		if(i->flag == NULL)
			explr.push_back(&(*i));
	}
	if (explr.size()==0)
		return;

	//Iterate through the buildings (which includes flags), looking for a flag that is not on the lists, that is in the right
	//spot to be one of our null records flags.
	Building** myBuildings=team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				bool found=false;
				for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
				{
					if(i->flag == b)
					{
						found=true;
						break;
					}
				}
				//If the flag is not in the list, look through our null records list and see if it matches any
				if (!found)
				{
					for (vector<explorationRecord*>::iterator i = explr.begin(); i!=explr.end(); i++)
					{
						if (b->posX==static_cast<int>((*i)->flag_x) && b->posY==static_cast<int>((*i)->flag_y))
						{
							(*i)->flag=b;
							if(AINicowar_DEBUG)
								std::cout<<"AINicowar: findCreatedFlags: Found a newly created or moved flag matching our records at positon "<<b->posX<<","<<b->posY<<" , inserting this flag into our records."<<endl;
							orders.push(new OrderModifyFlag(b->gid, (*i)->radius));
							orders.push(new OrderModifyBuilding(b->gid, (*i)->assigned));
							break;
						}
					}
				}
			}
		}
	}
}




void AINicowar::moderateSwarmsForExplorers(void)
{
	//I've raised the priority on explorers temporarily for testing.
	//	changeUnits("aircontrol", EXPLORER, static_cast<int>(desired_explorers/2) , desired_explorers, 0);
	changeUnits("aircontrol", EXPLORER, 0, static_cast<int>(desired_explorers/2), desired_explorers);
}




void AINicowar::explorerAttack(void)
{
	bool lvl3_school_exists=false;
	Building** myBuildings=team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING && b->type->level==2)
			{
				lvl3_school_exists=true;
			}
		}
	}
	if (!lvl3_school_exists)
	{
		return;
	}

	if (!developing_attack_explorers)
	{
		developing_attack_explorers=true;
		desired_explorers+=static_cast<int>(EXPLORERS_PER_ATTACK*EXPLORER_ATTACKS_AT_ONCE);
		return;
	}

	//check if we have enough explorers to start launching attacks.
	int ground_attack_explorers=0;
	Unit** myUnits = team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit* u = myUnits[i];
		if (u)
		{
			if (u->canLearn[MAGIC_ATTACK_GROUND] && u->performance[MAGIC_ATTACK_GROUND])
			{
				ground_attack_explorers+=1;
			}
		}
	}

	if (ground_attack_explorers<static_cast<int>(EXPLORERS_PER_ATTACK*EXPLORER_ATTACKS_AT_ONCE))
		return;
	if(AINicowar_DEBUG)
		if(!explorer_attacking)
			std::cout<<"AINicowar: explorerAttack: Enabling explorer attacks."<<endl;
	explorer_attacking=true;
}




void AINicowar::targetEnemy()
{
	if(enemy==NULL || !enemy->isAlive)
	{
		for(int i=0; i<32; ++i)
		{
			Team* t = game->teams[i];
			if(t)
			{
				if(t->me & team->enemies && t->isAlive)
				{
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: targetEnemy: A new enemy has been chosen."<<endl;
					enemy=game->teams[i];
					return;
				}
			}
		}
	}

}




void AINicowar::attack()
{
	vector<vector<Building*> > buildings(IntBuildingType::NB_BUILDING);
	for(int i=0; i<1024; ++i)
	{
		Building* b = enemy->myBuildings[i];
		if(b)
		{
			buildings[std::find(ATTACK_PRIORITY, ATTACK_PRIORITY+IntBuildingType::NB_BUILDING-3, b->type->shortTypeNum)-ATTACK_PRIORITY].push_back(b);
		}
	}

	unsigned int max_barracks_level=0;
	for(int i=0; i<1024; ++i)
	{
		Building* b = team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(b->type->level+1));
			}
		}
	}

	unsigned int available_units = getFreeUnits(ATTACK_STRENGTH, max_barracks_level+1);

	for(vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
	{
		if(i->assigned_level == max_barracks_level && available_units > i->assigned_units)
			available_units-=i->assigned_units;
		else if(available_units < i->assigned_units)
			available_units=0;
	}

	unsigned int attack_count=attacks.size();

	for(vector<vector<Building*> >::iterator i = buildings.begin(); i != buildings.end(); ++i)
	{
		for(vector<Building*>::iterator j = i->begin(); j!=i->end(); ++j)
		{
			Building* b = *j;
			if(attack_count!=MAX_ATTACKS_AT_ONCE && available_units>=ATTACK_WARRIOR_MINIMUM)
			{
				bool found=false;
				for(vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
				{
					if(b->gid==i->target)
					{
						found=true;
						break;
					}
				}
				if(found)
					continue;
				attackRecord ar;
				ar.target=b->gid;
				ar.flag=NULL;
				ar.flagx=b->posX+(b->type->width/2);
				ar.flagy=b->posY+(b->type->height/2);
				ar.zonex=b->posX-ATTACK_ZONE_BUILDING_PADDING;
				ar.zoney=b->posY-ATTACK_ZONE_BUILDING_PADDING;
				ar.width=b->type->width+ATTACK_ZONE_BUILDING_PADDING*2;
				ar.height=b->type->height+ATTACK_ZONE_BUILDING_PADDING*2;
				ar.unitx=b->posX-ATTACK_ZONE_EXAMINATION_PADDING;
				ar.unity=b->posY-ATTACK_ZONE_EXAMINATION_PADDING;
				ar.unit_width=b->type->width+ATTACK_ZONE_EXAMINATION_PADDING*2;
				ar.unit_height=b->type->height+ATTACK_ZONE_EXAMINATION_PADDING*2;
				ar.assigned_units=pollArea(ar.unitx, ar.unity, ar.unit_width, ar.unit_height, MAXIMUM, ENEMY_UNITS)*2+ATTACK_WARRIOR_MINIMUM;
				ar.assigned_level=max_barracks_level;
				attacks.push_back(ar);
				Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: attack: Creating a war flag at "<<ar.flagx<<", "<<ar.flagy<<" and assigning "<<ar.assigned_units<<" units to fight and kill the building at "<<b->posX<<","<<b->posY<<"."<<endl;
				orders.push(new OrderCreate(team->teamNumber, ar.flagx, ar.flagy, typeNum));
				++attack_count;
				available_units-=ar.assigned_units;
			}
		}
	}
}




void AINicowar::updateAttackFlags()
{
	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		if(!buildingStillExists(j->target))
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateAttackFlags: Stopping attack on a building, removing the "<<j->flag->posX<<","<<j->flag->posY<<" flag."<<endl;
			orders.push(new OrderDelete(j->flag->gid));
			j=attacks.erase(j);
			continue;
		}
		else
		{
			++j;
		}
	}

	for(int i=0; i<1024; ++i)
	{
		Building* b = team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end(); j++)
				{
					if(j->flag==NULL && b->posX == static_cast<int>(j->flagx) && b->posY == static_cast<int>(j->flagy))
					{
						j->flag=b;
						unsigned int radius=std::max(j->width, j->height)/2;
						if(AINicowar_DEBUG)
							std::cout<<"AINicowar: updateAttackFlags: Found a flag that attack() had created. Giving it a "<<radius<<" radius. Assigning "<<j->assigned_units<<" units to it, and setting it to use level "<<j->assigned_level<<" warriors."<<endl;

						orders.push(new OrderModifyFlag(b->gid, radius));
						orders.push(new OrderModifyBuilding(b->gid, j->assigned_units));
						orders.push(new OrderModifyWarFlag(b->gid, j->assigned_level));
						break;
					}
				}
			}
		}
	}

	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		unsigned int score = pollArea(j->unitx, j->unity, j->unit_width, j->unit_height, MAXIMUM, ENEMY_UNITS);
		if(score*2+ATTACK_WARRIOR_MINIMUM != j->assigned_units)
		{
			int new_assigned=score*2+ATTACK_WARRIOR_MINIMUM;
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateAttackFlags: Changing the "<<j->flag->posX<<","<<j->flag->posY<<" flag from "<<j->assigned_units<<" to "<<new_assigned<<"."<<endl;
			j->assigned_units=new_assigned;
			orders.push(new OrderModifyBuilding(j->flag->gid, new_assigned));
		}
		++j;
	}

}




void AINicowar::changeUnits(string module_name, unsigned int unit_type, unsigned int desired_units, unsigned int required_units, unsigned int emergency_units)
{
	unitRecord ur;
	if (module_demands.count(module_name))
		ur=module_demands[module_name];
	else
	{
		for (int i=0; i<NB_UNIT_TYPE; i++)
		{
			ur.desired_units[i]=0;
			ur.required_units[i]=0;
			ur.emergency_units[i]=0;
		}
	}
	ur.desired_units[unit_type]=desired_units;
	ur.required_units[unit_type]=required_units;
	ur.emergency_units[unit_type]=emergency_units;
	module_demands[module_name]=ur;
}




void AINicowar::moderateSwarms()
{
	//The number of units we want for each priority level
	unsigned int num_wanted[NB_UNIT_TYPE][3];
	unsigned int total_available[NB_UNIT_TYPE];
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		total_available[i]=team->stats.getLatestStat()->numberUnitPerType[i];
		for(int n=0; n<3; ++n)
		{
			num_wanted[i][n]=0;
		}
	}

	//Counts out the requested units from each of the modules
	for (std::map<string, unitRecord>::iterator i = module_demands.begin(); i!=module_demands.end(); i++)
	{
		for (unsigned int unit_t=0; static_cast<int>(unit_t)<NB_UNIT_TYPE; unit_t++)
		{
			num_wanted[unit_t][0]+=i->second.desired_units[unit_t];
			num_wanted[unit_t][1]+=i->second.required_units[unit_t];
			num_wanted[unit_t][2]+=i->second.emergency_units[unit_t];
		}
	}

	//Substract the already-existing amount of units from the numbers requested, and then move these totals multiplied by their respective score
	//into the ratios. This number can't be unsigned or it won't pass into OrderModifySwarm properly
	int ratios[NB_UNIT_TYPE];
	unsigned int total_wanted_score=0;
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		for(unsigned int n=0; n<3; ++n)
		{
			unsigned int reverse_loc=2-n;
			if(total_available[i] > num_wanted[i][reverse_loc])
			{
				total_available[i]-=num_wanted[i][reverse_loc];
				num_wanted[i][reverse_loc]=0;
			}
			else
			{
				num_wanted[i][reverse_loc]-=total_available[i];
			}
		}
		ratios[i]=0;
		ratios[i]+=num_wanted[i][0]*DESIRED_UNIT_SCORE;
		ratios[i]+=num_wanted[i][1]*REQUIRED_UNIT_SCORE;
		ratios[i]+=num_wanted[i][2]*EMERGENCY_UNIT_SCORE;
		total_wanted_score+=ratios[i];
	}

	int max=*max_element(ratios, ratios+NB_UNIT_TYPE);
	float devisor=1;
	if(max>16)
		devisor=static_cast<float>(max)/16.0;

	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; ++i)
	{
		ratios[i]=static_cast<int>(std::floor(ratios[i]/devisor+0.5));
	}

	unsigned int assigned_per_swarm=std::min(MAXIMUM_UNITS_FOR_SWARM, static_cast<unsigned int>(total_wanted_score/CREATION_UNIT_REQUIREMENT+1));
	changeUnits("spawn-manager", WORKER, 0, team->swarms.size()*assigned_per_swarm, 0);

	for (std::list<Building*>::iterator i = team->swarms.begin(); i != team->swarms.end(); ++i)
	{
		Building* swarm=*i;
		bool changed=false;
		for (int x = 0; x<NB_UNIT_TYPE; x++)
			if (swarm->ratio[x]!=ratios[x])
				changed=true;

		if(swarm->maxUnitWorking != static_cast<int>(assigned_per_swarm))
			orders.push(new OrderModifyBuilding(swarm->gid, assigned_per_swarm));

		if(!changed)
			continue;

		if(AINicowar_DEBUG)
			std::cout<<"AINicowar: moderateSpawns: Turning changing production ratios on a swarm from {Worker:"<<swarm->ratio[0]<<", Explorer:"<<swarm->ratio[1]<<", Warrior:"<<swarm->ratio[2]<<"} to {Worker:"<<ratios[0]<<", Explorer:"<<ratios[1]<<", Warrior:"<<ratios[2]<<"}. Assigning "<<assigned_per_swarm<<" workers."<<endl;
		orders.push(new OrderModifySwarm(swarm->gid, ratios));
	}
}




AINicowar::innRecord::innRecord() : pos(0), records(INN_RECORD_MAX) {}

void AINicowar::recordInns()
{
	for(int i=0; i<1024; ++i)
	{
		Building* b=team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING)
			{
				innRecord i = inns[b->gid];
				i.records[i.pos].food_amount=b->ressources[CORN];
				i.records[i.pos].units_eating=b->unitsInside.size();
				i.pos+=1;
				if (i.pos==INN_RECORD_MAX)
				{
					i.pos=0;
				}
				inns[b->gid]=i;
			}
		}
	}
}




struct innScore
{
	Building* inn;
	int food_score;
	int unit_score;
	innScore() {food_score=0; unit_score=0;}
};

struct finalInnScore
{
	Building* inn;
	int food_score;
	int food_index;
	int unit_score;
	int unit_index;
	finalInnScore() {food_score=0; food_index=0; unit_score=0; unit_index=0;}
};

//One inn is greater than another if it has less food. If they are equal, however, it will compare the unit amounts.
bool compare_food(const innScore& first, const innScore& other)
{
	if (first.food_score == other.food_score)
		return first.unit_score<other.unit_score;

	return first.food_score>other.food_score;
}




//One inn is greater than another if it has less units. If they are equal, however, it will compare the food amounts.
bool compare_unit(const innScore& first, const innScore& other)
{
	if (first.unit_score == other.unit_score)
		return first.food_score>other.food_score;

	return first.unit_score<other.unit_score;
}




//One final inn score is greater than another if its food_score+unit_score is greater than the others.
//If in doubt (a tie), check which building has the higher unit score (without food score added in).
bool compare_final(const finalInnScore& first, const finalInnScore& other)
{
	if (first.food_index+first.unit_index == other.food_index+other.unit_index)
	{
		if(first.unit_index<other.unit_index)
			return true;
		return false;
	}
	return first.food_index+first.unit_index < other.food_index+other.unit_index;
}




void AINicowar::modifyInns()
{

	std::vector<innScore> scores;

	for(std::map<int, innRecord>::iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		innScore score;
		score.inn=getBuildingFromGid(i->first);
		if (score.inn==NULL)
		{
			inns.erase(i);
			continue;
		}
		for (vector<singleInnRecord>::iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			score.food_score+=record->food_amount;
			score.unit_score+=record->units_eating;
		}
		scores.push_back(score);
	}
	if(scores.size()==0)
		return;

	std::vector<innScore> scores_total_food(scores);
	std::vector<innScore> scores_total_units(scores);

	sort(scores_total_food.begin(), scores_total_food.end(), &compare_food);
	sort(scores_total_units.begin(), scores_total_units.end(), &compare_unit);

	std::vector<finalInnScore> score_finals;

	int total_workers_needed=0;

	for (std::vector<innScore>::iterator i = scores.begin(); i!=scores.end(); ++i)
	{
		finalInnScore final_score;
		final_score.inn = i->inn;
		total_workers_needed+=i->inn->maxUnitWorking;
		for (std::vector<innScore>::iterator food_i = scores_total_food.begin(); food_i!=scores_total_food.end(); ++food_i)
		{
			if(i->inn == food_i->inn)
			{
				final_score.food_index=food_i-scores_total_food.begin();
				final_score.food_score=food_i->food_score;
				break;
			}
		}
		for (std::vector<innScore>::iterator unit_i = scores_total_units.begin(); unit_i!=scores_total_units.end(); ++unit_i)
		{
			if(i->inn == unit_i->inn)
			{
				final_score.unit_index=unit_i-scores_total_units.begin();
				final_score.unit_score=unit_i->unit_score;
				break;
			}
		}
		score_finals.push_back(final_score);
	}

	sort(score_finals.begin(), score_finals.end(), &compare_final);

	Building* to_increase;
	Building* to_decrease;

	for (std::vector<finalInnScore>::reverse_iterator i = score_finals.rbegin(); i!=score_finals.rend(); i++)
	{
		to_increase=i->inn;
		if(i-score_finals.rbegin()<static_cast<int>(score_finals.size()/2) && to_increase->maxUnitWorking<static_cast<int>(INN_MAX[to_increase->type->level]))
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: modifyInns: Increasing the number of units assigned to an inn."<<endl;
			total_workers_needed+=1;
			orders.push(new OrderModifyBuilding(to_increase->gid, to_increase->maxUnitWorking+1));
			break;
		}
	}

	for (std::vector<finalInnScore>::iterator i = score_finals.begin(); i!=score_finals.end(); i++)
	{
		to_decrease=i->inn;
		if(i-score_finals.begin()<static_cast<int>(score_finals.size()/2) && to_decrease->maxUnitWorking>static_cast<int>(INN_MINIMUM[to_decrease->type->level]))
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: modifyInns: Decreasing the number of units assigned to an inn."<<endl;
			total_workers_needed-=1;
			orders.push(new OrderModifyBuilding(to_decrease->gid, to_decrease->maxUnitWorking-1));
			break;
		}
	}
	changeUnits("inn-manager", WORKER, inns.size(), total_workers_needed, 0);
}




void AINicowar::findDefense()
{
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = team->myBuildings[i];
		if(b)
		{
			if(building_health.find(b->gid) != building_health.end())
			{
				if(static_cast<int>(building_health[b->gid]) > b->hp)
				{
					//					zone z=getZone(b->posX, b->posY, DEFENSE_ZONE_WIDTH, DEFENSE_ZONE_HEIGHT, DEFENSE_ZONE_HORIZONTAL_OVERLAP, DEFENSE_ZONE_VERTICAL_OVERLAP);
					bool found=false;
					for(vector<defenseRecord>::iterator j = defending_zones.begin(); j != defending_zones.end(); ++j)
					{
						//						if(j->zonex == z.x && j->zoney == z.y)
						if(j->zonex == b->posX-DEFENSE_ZONE_BUILDING_PADDING && j->zoney == b->posY-DEFENSE_ZONE_BUILDING_PADDING)
						{
							found=true;
						}
					}
					if(found)
					{
						continue;
					}
					defenseRecord dr;
					dr.flag=NULL;
					//					dr.flagx=z.x+(z.width/2);
					//					dr.flagy=z.y+(z.height/2);
					dr.flagx=b->posX+(b->type->width/2);
					dr.flagy=b->posY+(b->type->height/2);
					//					dr.zonex=z.x;
					//					dr.zoney=z.y;
					dr.zonex=b->posX-DEFENSE_ZONE_BUILDING_PADDING;
					dr.zoney=b->posY-DEFENSE_ZONE_BUILDING_PADDING;
					//					dr.width=z.width;
					//					dr.height=z.height;
					dr.width=b->type->width+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.height=b->type->height+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.assigned=pollArea(dr.zonex, dr.zoney, dr.width, dr.height, MAXIMUM, ENEMY_UNITS);
					defending_zones.push_back(dr);
					Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: findDefense: Creating a defense flag at "<<dr.flagx<<", "<<dr.flagy<<", to combat "<<dr.assigned<<" units that are attacking "<<b->posX<<","<<b->posY<<"."<<endl;
					orders.push(new OrderCreate(team->teamNumber, dr.flagx, dr.flagy, typeNum));
				}
			}
		}
	}

	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = team->myBuildings[i];
		if(b)
		{
			building_health[b->gid]=b->hp;
		}
	}
}




void AINicowar::updateFlags()
{
	for (vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end();)
	{
		int score = pollArea(i->zonex, i->zoney, i->width, i->height, MAXIMUM, ENEMY_UNITS);
		if(score==0)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateFlags: Found a zone at "<<i->zonex<<","<<i->zoney<<" that no longer has any enemy units in it. Removing this flag."<<endl;
			if(i->flag!=NULL)
				orders.push(new OrderDelete(i->flag->gid));
			i=defending_zones.erase(i);
		}
		else
		{
			if(i->flag!=NULL)
				if(score!=i->flag->maxUnitWorking)
			{
				orders.push(new OrderModifyBuilding(i->flag->gid, score));
			}
			++i;
		}
	}
}




void AINicowar::findCreatedDefenseFlags()
{
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum == IntBuildingType::WAR_FLAG)
			{
				for (vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end(); ++i)
				{
					if(i->flag == NULL && b->posX == static_cast<int>(i->flagx) && b->posY == static_cast<int>(i->flagy))
					{
						if(AINicowar_DEBUG)
							std::cout<<"AINicowar: findCreatedDefenseFlags: Found created flag at "<<i->flagx<<","<<i->flagy<<", adding it to the defense records."<<endl;
						i->flag=b;
						orders.push(new OrderModifyFlag(b->gid, std::max(i->width, i->height)/2));
						orders.push(new OrderModifyBuilding(b->gid, i->assigned));
						break;
					}
				}
			}
		}
	}
}




void AINicowar::controlTowers()
{

	int count=0;
	for(int i=0; i<1024; i++)
	{
		Building* b = team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum == IntBuildingType::DEFENSE_BUILDING &&
				b->buildingState==Building::ALIVE &&
				b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				count+=1;
				if(b->maxUnitWorking != static_cast<int>(NUM_PER_TOWER))
				{
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: controlTowers: Changing number of units assigned to a tower, from "<<b->maxUnitWorking<<" to "<<NUM_PER_TOWER<<"."<<endl;
					orders.push(new OrderModifyBuilding(b->gid, NUM_PER_TOWER));
				}
			}
		}
	}
	changeUnits("tower-controller", WORKER, 0, count*NUM_PER_TOWER, 0);
}




AINicowar::upgradeData AINicowar::findMaxSize(unsigned int building_type, unsigned int cur_level)
{
	string type = IntBuildingType::reverseConversionMap[building_type];
	BuildingType* new_type = globalContainer->buildingsTypes.getByType(type, 2, false);
	BuildingType* cur_type = globalContainer->buildingsTypes.getByType(type, cur_level, false);
	upgradeData ud;
	if(new_type)
	{
		ud.width=new_type->width;
		ud.height=new_type->height;
		ud.horizontal_offset = (new_type->width - cur_type->width)/2;
		ud.vertical_offset = (new_type->height - cur_type->height)/2;
	}
	else
	{
		ud.width=cur_type->width;
		ud.height=cur_type->height;
		ud.horizontal_offset=0;
		ud.vertical_offset=0;
	}
	return ud;
}




AINicowar::point AINicowar::findBestPlace(unsigned int building_type)
{
	//Get the best zone based on a number of factors
	vector<zone> zones = getBestZones(  static_cast<pollModifier>(CONSTRUCTION_FACTORS[building_type][0][0]),
		static_cast<pollType>(CONSTRUCTION_FACTORS[building_type][0][1]),
		static_cast<pollModifier>(CONSTRUCTION_FACTORS[building_type][1][0]),
		static_cast<pollType>(CONSTRUCTION_FACTORS[building_type][1][1]),
		static_cast<pollModifier>(CONSTRUCTION_FACTORS[building_type][2][0]),
		static_cast<pollType>(CONSTRUCTION_FACTORS[building_type][2][1]),
		BUILD_AREA_WIDTH, BUILD_AREA_HEIGHT, BUILD_AREA_HORIZONTAL_OVERLAP,
		BUILD_AREA_VERTICAL_OVERLAP, BUILD_AREA_EXTENTION_WIDTH,
		BUILD_AREA_EXTENTION_HEIGHT, MINIMUM_NEARBY_BUILDINGS_TO_CONSTRUCT);

	//Iterate through the zones
	for(std::vector<zone>::iterator i = zones.begin(); i!=zones.end(); ++i)
	{
		//Pregenerate some common numbers
		unsigned int full_width=BUILD_AREA_WIDTH+2*BUILD_AREA_EXTENTION_WIDTH;
		unsigned int full_height=BUILD_AREA_HEIGHT+2*BUILD_AREA_EXTENTION_HEIGHT;
		unsigned int zone_start_column=BUILD_AREA_EXTENTION_WIDTH;
		unsigned int zone_start_row=BUILD_AREA_EXTENTION_HEIGHT;
		unsigned int zone_end_column=BUILD_AREA_EXTENTION_WIDTH+BUILD_AREA_WIDTH;
		unsigned int zone_end_row=BUILD_AREA_EXTENTION_HEIGHT+BUILD_AREA_HEIGHT;
		unsigned short int imap[full_width][full_height];

		//Prepare the imap
		for(unsigned int x=0; x<full_width; ++x)
		{
			for(unsigned int y=0; y<full_height; ++y)
			{
				imap[x][y]=0;
			}
		}

		//Go through each square checking off squares that are used
		for(unsigned int x=0; x<full_width; ++x)
		{
			unsigned int fx=i->x + x - BUILD_AREA_EXTENTION_WIDTH;
			for(unsigned int y=0; y<full_height; ++y)
			{
				unsigned int fy=i->y + y - BUILD_AREA_EXTENTION_HEIGHT;
				//Check off hidden or occupied squares
				if(!map->isHardSpaceForBuilding(fx, fy) || !map->isMapDiscovered(fx, fy, team->me))
				{
					imap[x][y]=1;
				}

				//If we find a building here (or a building that is in the list of buildings to be constructed)
				bool found_building=false;
				upgradeData size;
				if(map->getBuilding(fx, fy)!=NOGBID)
				{
					Building* b=getBuildingFromGid(map->getBuilding(fx, fy));
					size=findMaxSize(b->type->shortTypeNum, b->type->level);
					found_building=true;
				}

				for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); i++)
				{
					if(i->x == fx && i->y == fy)
					{
						found_building=true;
						size=findMaxSize(i->building_type, 0);
					}
				}
				//Then mark all the squares this building occupies in its largest upgrade with an extra padding
				if(found_building)
				{

					unsigned int startx=x-size.horizontal_offset-BUILDING_PADDING;
					if(startx<0)
						startx=0;
					unsigned int starty=y-size.vertical_offset-BUILDING_PADDING;
					if(starty<0)
						starty=0;
					unsigned int endx=startx+size.width+BUILDING_PADDING*2;
					if(endx>=full_width)
						endx=full_width;
					unsigned int endy=starty+size.height+BUILDING_PADDING*2;
					if(endy>=full_height)
						endy=full_height;
					for(unsigned int x2=startx; x2<endx; ++x2)
					{
						for(unsigned int y2=starty; y2<endy; ++y2)
						{
							imap[x2][y2]=1;
						}
					}
				}
			}
		}

		upgradeData size=findMaxSize(building_type, 0);

		//Change this to output the int maps for debugging, carefull, they are large

		bool output_zones=false;
		if(output_zones)
		{
			std::cout<<"Zone "<<i->x<<","<<i->y<<endl;
			for(unsigned int y=0; y<full_height; ++y)
			{

				if(y==zone_start_row || y==zone_end_row)
				{
					for(unsigned int x=0; x<full_width+2; ++x)
					{
						if (x>=zone_start_column+1 && x<zone_end_column+1)
							std::cout<<"--";
						else
							std::cout<<"  ";
					}
					std::cout<<endl;
				}

				for(unsigned int x=0; x<full_width; ++x)
				{
					if (x==zone_start_column || x==zone_end_column)
					{
						if(y >= zone_start_row && y<zone_end_row)
							std::cout<<"| ";
						else
							std::cout<<"  ";
					}
					std::cout<<imap[x][y]<<" ";
				}
				std::cout<<endl;
			}
		}

		//Now go through the zones int map searching for a good, possible place to put the building.
		unsigned int max=0;
		point max_point;
		max_point.x=NOPOS;
		max_point.y=NOPOS;
		for(unsigned int x=zone_start_column; x<zone_end_column; ++x)
		{
			for(unsigned int y=zone_start_row; y<zone_end_row; ++y)
			{
				if(imap[x][y]==0)
				{
					bool all_empty=true;
					for(unsigned int x2=x; x2<(x+size.width) && all_empty; ++x2)
					{
						for(unsigned int y2=y; y2<(y+size.height) && all_empty; ++y2)
						{
							if(imap[x2][y2]==1)
							{
								all_empty=false;
							}
						}
					}
					if(all_empty)
					{
						point p;
						p.x=i->x + x - BUILD_AREA_EXTENTION_WIDTH;
						p.y=i->y + y - BUILD_AREA_EXTENTION_HEIGHT;
						if(!CRAMP_BUILDINGS)
							return p;

						unsigned int border_count=0;
						for(unsigned int x2=x-1; x2<(x+size.width+2); ++x2)
						{
							for(unsigned int y2=y-1; y2<(y+size.height+2); ++y2)
							{
								if(imap[x2][y2]==1)
									border_count++;
							}
						}
						if(border_count>max || max==0)
						{
							max_point=p;
							max=border_count;
						}
					}
				}
			}
		}
		if(max_point.x!=NOPOS && CRAMP_BUILDINGS)
			return max_point;
	}
	point p;
	p.x=NOPOS;
	p.y=NOPOS;
	return p;
}




void AINicowar::constructBuildings()
{
	unsigned total_free_workers=0;
	for(int i=0; i<NB_UNIT_LEVELS; ++i)
	{
		total_free_workers+=getFreeUnits(BUILD, i);
	}

	unsigned int counts[IntBuildingType::NB_BUILDING];
	unsigned int under_construction_counts[IntBuildingType::NB_BUILDING];
	unsigned int total_construction=new_buildings.size();

	if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
	{
		std::cout<<"Fail 1, too much construction."<<endl;
		return;
	}

	for(unsigned i = 0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		counts[i]=team->stats.getLatestStat()->numberBuildingPerType[i];
		under_construction_counts[i]=0;
	}

	for(std::vector<newConstructionRecord>::iterator i=new_buildings.begin(); i!=new_buildings.end(); ++i)
	{
		counts[i->building_type]++;
		under_construction_counts[i->building_type]++;
		if(total_free_workers>i->assigned)
			total_free_workers-=i->assigned;
		else
			total_free_workers=0;
		if(i->building!=NOGBID && getBuildingFromGid(i->building)!=NULL)
			total_free_workers+=getBuildingFromGid(i->building)->unitsWorking.size();
	}

	for(unsigned int i = 0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		while(true)
		{
			if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
			{
				std::cout<<"Fail 1, too much construction, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				return;
			}
			if(total_free_workers<MINIMUM_TO_CONSTRUCT_NEW)
			{
				std::cout<<"Fail 2, too few units, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				return;
			}
			if(under_construction_counts[i]>=MAX_NEW_CONSTRUCTION_PER_BUILDING[i])
			{
				std::cout<<"Fail 3, too many buildings of this type under constructon, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				break;
			}
			if(counts[i]>=num_buildings_wanted[i])
			{
				std::cout<<"Fail 4, building cap reached, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				break;
			}
			point p = findBestPlace(i);
			if(p.x == NOPOS || p.y==NOPOS)
			{
				std::cout<<"Fail 5, no suitable positon found, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				break;
			}

			newConstructionRecord ncr;
			ncr.building=NOGBID;
			ncr.x=p.x;
			ncr.y=p.y;
			///Change this later
			ncr.assigned=std::min(total_free_workers, MAXIMUM_TO_CONSTRUCT_NEW);
			ncr.building_type=i;
			new_buildings.push_back(ncr);

			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: constructBuildings: Starting construction on a "<<IntBuildingType::reverseConversionMap[i]<<", at position "<<p.x<<","<<p.y<<"."<<endl;
			Sint32 type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[i], 0, true);
			orders.push(new OrderCreate(team->teamNumber, p.x, p.y, type));
			total_construction+=1;
			total_free_workers-=ncr.assigned;
			under_construction_counts[i]++;
			counts[i]++;
		}
	}
}




void AINicowar::updateBuildings()
{
	//Remove records of buildings that are no longer under construction
	for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end();)
	{
		if(i->building!=NOGBID)
		{
			Building* b = getBuildingFromGid(i->building);
			if(b==NULL || b->constructionResultState == Building::NO_CONSTRUCTION)
			{
				orders.push(new OrderModifyBuilding(i->building, 1));
				i = new_buildings.erase(i);

			}
			else
				++i;
		}
		else
			++i;
	}

	//Update buildings that have just been created.
	//There is one problem, if a building is destroyed by enemy troops before this gets executed, the new record will hang and never be removed, causing
	//disruptions throughout the code.
	for(int i=0; i<1024; ++i)
	{
		Building *b = team->myBuildings[i];
		if(b)
		{
			for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); ++i)
			{
				if(i->building==NOGBID && b->type->shortTypeNum == static_cast<int>(i->building_type) && b->posX == static_cast<int>(i->x) && b->posY == static_cast<int>(i->y))
				{
					i->building=b->gid;
					orders.push(new OrderModifyBuilding(i->building, i->assigned));
				}
			}
		}
	}
}




void AINicowar::calculateBuildings()
{
	unsigned int total_units=team->stats.getLatestStat()->totalUnit;
	for (int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		if(UNITS_FOR_BUILDING[i]!=0)
			num_buildings_wanted[i]=std::max(total_units/UNITS_FOR_BUILDING[i], static_cast<unsigned int>(1));
		else
			num_buildings_wanted[i]=0;
	}
}
