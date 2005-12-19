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
#include "Ressource.h"
#include <algorithm>
#include <iterator>
#include "Brush.h"

using namespace std;
using namespace Nicowar;

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
	center_x=0;
	center_y=0;
	module_timer=0;

	assert(player);

	attack_module=NULL;
	defense_module=NULL;
	new_construction_module=NULL;
	upgrade_repair_module=NULL;
	unit_module=NULL;

	new BasicDistributedSwarmManager(*this);
	new PrioritizedBuildingAttack(*this);
	new SimpleBuildingDefense(*this);
	new DistributedNewConstructionManager(*this);
	new RandomUpgradeRepairModule(*this);
	new ExplorationManager(*this);
	new InnManager(*this);
	new TowerController(*this);
	new BuildingClearer(*this);

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
	for(vector<Module*>::iterator i=modules.begin(); i!=modules.end(); ++i)
	{
		if(*i)
			delete *i;
	}
}




bool AINicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);

	stream->readEnterSection("AINicowar");
	timer=stream->readUint32("timer");
	iteration=stream->readUint32("iteration");
	center_x=stream->readUint32("center_x");
	center_y=stream->readUint32("center_y");
	module_timer=stream->readUint32("module_timer");
	active_module=modules.begin()+stream->readUint32("active_module");

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

	char signature[4];

	stream->readEnterSection("modules");
	unsigned int size = stream->readUint32();
	for(n=0; n<size; ++n)
	{
		stream->read(signature, 4, "signatureStart");
		if (memcmp(signature,"MoSt", 4)!=0)
		{
			std::cout<<"Signature missmatch at begin of module #"<<n<<", "<<modules[n]->getName()<<". Expected \"MoEn\", recieved \""<<signature<<"\"."<<endl;
			stream->readLeaveSection();
			return false;
		}

		modules[n]->load(stream, player, versionMinor);

		stream->read(signature, 4, "signatureEnd");
		if (memcmp(signature,"MoEn", 4)!=0)
		{
			std::cout<<"Signature missmatch at end of module #"<<n<<", "<<modules[n]->getName()<<". Expected \"MoEn\", recieved \""<<signature<<"\"."<<endl;
			stream->readLeaveSection();
			return false;
		}

	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}




void AINicowar::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINicowar");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(iteration, "iteration");
	stream->writeUint32(center_x, "center_x");
	stream->writeUint32(center_y, "center_y");
	stream->writeUint32(module_timer, "module_timer");
	stream->writeUint32(active_module-modules.begin(), "active_module");

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

	stream->writeEnterSection("modules");
	stream->writeUint32(modules.size());
	for(vector<Module*>::iterator i = modules.begin(); i!=modules.end(); ++i)
	{
		stream->write("MoSt", 4, "signatureStart");
		(*i)->save(stream);
		stream->write("MoEn", 4, "signatureEnd");
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




Order *AINicowar::getOrder(void)
{

	//See if there is an existing order that the AI wanted to have done
	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}

	timer++;

	//Waits for atleast one iteration before it does anything, because of a few odd, 'goes to fast' bugs.
	if (timer<STARTUP_TIME)
		return new NullOrder();

	//	std::cout<<"timer="<<timer<<";"<<endl;

	if(active_module==modules.end() || iteration==0)
	{
		if(iteration==0)
		{
			setCenter();
			if(SEE_EVERYTHING)
			{
				for(int x=0; x<map->getW(); ++x)
				{
					for(int y=0; y<map->getH(); ++y)
					{
						map->setMapDiscovered(x, y, team->me);
					}
				}
			}
		}
		iteration+=1;
		active_module=modules.begin();
		std::cout<<"AINicowar: getOrder: ******Entering iteration "<<iteration<<" at tick #"<<timer<<". ******"<<endl;
	}

	//The +1 here is because the program would end one tick late, so the tick #'s that
	//it entered each iteration at would be 171, or 441 (for an interval of ten).
	//This -1 corrects that.
	if((timer+1)%TIMER_INTERVAL==0)
	{
		//		std::cout<<"Performing function: timer="<<timer<<"; module_timer="<<module_timer<<";"<<endl;
		bool cont = (*active_module)->perform(module_timer);
		if(!cont)
			++module_timer;

		if(module_timer==(*active_module)->numberOfTicks())
		{
			module_timer=0;
			++active_module;
		}
	};

	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}

	return new NullOrder();
}




void AINicowar::setDefenseModule(DefenseModule* module)
{
	remove(modules.begin(), modules.end(), defense_module);
	if(defense_module!=NULL)
		delete defense_module;
	defense_module=module;
	modules.push_back(module);
}




void AINicowar::setAttackModule(AttackModule* module)
{
	remove(modules.begin(), modules.end(), attack_module);
	if(attack_module!=NULL)
		delete attack_module;
	attack_module=module;
	modules.push_back(module);
}




void AINicowar::setNewConstructionModule(NewConstructionModule* module)
{
	remove(modules.begin(), modules.end(), new_construction_module);
	if(new_construction_module!=NULL)
		delete new_construction_module;
	new_construction_module=module;
	modules.push_back(module);
}




void AINicowar::setUpgradeRepairModule(UpgradeRepairModule* module)
{
	remove(modules.begin(), modules.end(), upgrade_repair_module);

	if(upgrade_repair_module!=NULL)
		delete upgrade_repair_module;
	upgrade_repair_module=module;
	modules.push_back(module);
}




void AINicowar::setUnitModule(UnitModule* module)
{
	remove(modules.begin(), modules.end(), unit_module);
	if(unit_module!=NULL)
		delete unit_module;
	unit_module=module;
	modules.push_back(module);
}




void AINicowar::addOtherModule(OtherModule* module)
{
	other_modules[module->getName()]=module;
	modules.push_back(module);
}




DefenseModule* AINicowar::getDefenseModule()
{
	return defense_module;
}




AttackModule* AINicowar::getAttackModule()
{
	return attack_module;
}




NewConstructionModule* AINicowar::getNewConstructionModule()
{
	return new_construction_module;
}




UpgradeRepairModule* AINicowar::getUpgradeRepairModule()
{
	return upgrade_repair_module;
}




UnitModule* AINicowar::getUnitModule()
{
	return unit_module;
}




OtherModule* AINicowar::getOtherModule(string name)
{
	return other_modules[name];
}




void AINicowar::setCenter()
{
	unsigned int x_total=0;
	unsigned int y_total=0;
	unsigned int square_total=0;
	for(int x=0; x < map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(map->isMapDiscovered(x, y, team->me))
			{
				x_total+=x;
				y_total+=y;
				square_total++;
			}
		}
	}
	center_x=x_total/square_total;
	center_y=y_total/square_total;
}




bool Nicowar::buildingStillExists(Game* game, Building* building)
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




bool Nicowar::buildingStillExists(Game* game, unsigned int gid)
{
	return getBuildingFromGid(game, gid)!=NULL;
}




unsigned int Nicowar::getFreeUnits(Team* team, int ability, int level)
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
			if ( (u->activity==Unit::ACT_RANDOM && u->movement==Unit::MOV_RANDOM_GROUND) && u->level[ability]==static_cast<int>(level) && u->medical==Unit::MED_FREE)
			{
				free_workers+=1;
			}
		}
	}
	return free_workers;
}




GridPollingSystem::GridPollingSystem(AINicowar& ai)
{
	map=ai.map;
	team=ai.team;
	game=ai.game;
	center_x=ai.getCenterX();
	center_y=ai.getCenterY();
}




unsigned int GridPollingSystem::pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollModifier mod, pollType poll_type)
{
	if (poll_type == NONE)
		return 0;

	//	if(poll_type == FRIENDLY_BUILDINGS)
	//		std::cout<<"findEnemyFlags: Starting poll."<<endl;

	if (poll_type == CENTER_DISTANCE)
	{
		unsigned int score=0;
		/*
				if(mod==MAXIMUM)
					score=intdistance((x+width/2), center_x) + intdistance((y+height/2), center_y);
				if(mod==MINIMUM)
					score=map->getW()/2-intdistance((x+width/2), center_x) + map->getH()/2-intdistance((y+height/2), center_y);
		*/
		if(mod==MAXIMUM)
			score=intdistance((x+width), center_x) + intdistance((y+height), center_y);
		if(mod==MINIMUM)
			score=(map->getW()-intdistance((x+width), center_x)) + (map->getH()-intdistance((y+height), center_y));
		return score;
	}

	unsigned int bound_h=x+width;
	if(static_cast<int>(bound_h)>map->getW())
		bound_h-=map->getW();

	unsigned int bound_y=y+height;
	if(static_cast<int>(bound_y)>map->getH())
		bound_y-=map->getH();

	unsigned int orig_y=y;
	unsigned int score=0;
	for(; x!=bound_h; ++x)
	{
		if(static_cast<int>(x) >= map->getW())
			x=0;

		y=orig_y;
		for(; y!=bound_y; ++y)
		{
			if(static_cast<int>(y) >= map->getH())
				y=0;

			Unit* u=NULL;
			Building* b=NULL;
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
					b = getBuildingFromGid(game, map->getBuilding(x, y));
					if (b)
					{
						if(b->owner->me&team->enemies && b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
						{
							score++;
						}
					}
					break;
				case FRIENDLY_BUILDINGS:
					b = getBuildingFromGid(game, map->getBuilding(x, y));
					if (b)
					{
						if(b->owner->me == team->me && b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
						{
							score++;
						}
					}
					break;
				case ENEMY_UNITS:
					u = getUnitFromGid(game, map->getGroundUnit(x, y));
					if (u)
					{
						if(u->owner->me & team->enemies && u->posX==static_cast<int>(x) && u->posY == static_cast<int>(y))
						{
							score++;
						}
					}
					break;
				case ENEMY_WARRIORS:
					u = getUnitFromGid(game, map->getGroundUnit(x, y));
					if (u)
					{
						if(u->owner->me & team->enemies && u->posX==static_cast<int>(x) && u->posY == static_cast<int>(y) && u->typeNum==WARRIOR)
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
				case CENTER_DISTANCE:
					assert(false);
					break;
				case NONE:
					assert(false);
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




GridPollingSystem::zone GridPollingSystem::getZone(unsigned int x, unsigned int y, unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap)
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




int GridPollingSystem::getPositionScore(const std::vector<pollRecord>& polls, const std::vector<pollRecord>::const_iterator& iter)
{
	int score=0;
	std::vector<pollRecord>::const_iterator last = polls.begin();
	for(std::vector<pollRecord>::const_iterator i = polls.begin(); i!=polls.end(); ++i)
	{
		if(i==iter)
		{
			break;
		}
		if(i->score != last->score)
		{
			score++;
			last=i;
		}
	}
	return score;
}




GridPollingSystem::getBestZonesSplit* GridPollingSystem::getBestZones(poll p, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height)
{
	getBestZonesSplit* split=new getBestZonesSplit;
	split->p=p;
	split->width=width;
	split->height=height;
	split->horizontal_overlap=horizontal_overlap;
	split->vertical_overlap=vertical_overlap;
	split->extention_width=extention_width;
	split->extention_height=extention_height;
	std::vector<pollRecord>& a_list=*new std::vector<pollRecord>;
	std::vector<pollRecord>& b_list=*new std::vector<pollRecord>;
	std::vector<pollRecord>& c_list=*new std::vector<pollRecord>;
	split->a_list=&a_list;
	split->b_list=&b_list;
	split->c_list=&c_list;

	for (unsigned int x=0; x<=static_cast<unsigned int>(map->getW()-horizontal_overlap); x+=width-horizontal_overlap)
	{
		for (unsigned int y=0; y<=static_cast<unsigned int>(map->getH()-vertical_overlap); y+=height-vertical_overlap)
		{
			int full_x=x-extention_width;
			if(full_x<0)
				full_x=map->getW()+full_x;
			int full_y=y-extention_height;
			if(full_y<0)
				full_y=map->getH()+full_y;

			int full_w=width+2*extention_width;
			int full_h=height+2*extention_height;

			if(pollArea(full_x, full_y, full_w, full_h, p.mod_minimum, p.minimum_type)<p.minimum_score)
				continue;

			if(pollArea(full_x, full_y, full_w, full_h, p.mod_maximum, p.maximum_type)>p.maximum_score)
				continue;

			pollRecord pr_a;
			pr_a.x=x;
			pr_a.y=y;
			pr_a.width=width;
			pr_a.height=height;
			pollRecord pr_b=pr_a;
			pollRecord pr_c=pr_b;
			pr_a.score=pollArea(full_x, full_y, full_w, full_h, p.mod_1, p.type_1);
			pr_b.score=pollArea(full_x, full_y, full_w, full_h, p.mod_2, p.type_2);
			pr_c.score=pollArea(full_x, full_y, full_w, full_h, p.mod_3, p.type_3);
			a_list.push_back(pr_a);
			b_list.push_back(pr_b);
			c_list.push_back(pr_c);
		}
	}

	std::sort(a_list.begin(), a_list.end());
	std::sort(b_list.begin(), b_list.end());
	std::sort(c_list.begin(), c_list.end());
	return split;
}




std::vector<GridPollingSystem::zone> GridPollingSystem::getBestZones(GridPollingSystem::getBestZonesSplit* split_calc)
{
	std::vector<pollRecord>& a_list=*split_calc->a_list;
	std::vector<pollRecord>& b_list=*split_calc->b_list;
	std::vector<pollRecord>& c_list=*split_calc->c_list;

	std::vector<threeTierRecord> final;
	unsigned int pos_a=0;
	for (std::vector<pollRecord>::iterator i1=a_list.begin(); i1!=a_list.end(); ++i1)
	{
		pos_a++;

		threeTierRecord ttr;
		ttr.x=i1->x;
		ttr.y=i1->y;
		ttr.width=i1->width;
		ttr.height=i1->height;

		for(std::vector<pollRecord>::iterator s1=i1; s1!=a_list.end() && s1->score == i1->score; ++s1)
			pos_a++;

		ttr.score_a=pos_a;

		unsigned int pos_b=0;
		for (std::vector<pollRecord>::iterator i2=b_list.begin(); i2!=b_list.end(); ++i2)
		{
			pos_b++;
			if(i2->x==ttr.x && i2->y==ttr.y)
			{
				for(std::vector<pollRecord>::iterator s2=i2; s2!=b_list.end() && s2->score == i2->score; ++s2)
					pos_b++;
				ttr.score_b=pos_b;
				break;
			}
		}

		unsigned int pos_c=0;
		for (std::vector<pollRecord>::iterator i3=c_list.begin(); i3!=c_list.end(); ++i3)
		{
			pos_c++;
			if(i3->x==ttr.x && i3->y==ttr.y)
			{
				for(std::vector<pollRecord>::iterator s3=i3; s3!=c_list.end() && s3->score == i3->score; ++s3)
					pos_c++;
				ttr.score_c=pos_c;
				break;
			}
		}
		final.push_back(ttr);
	}

	std::sort(final.begin(), final.end());

	std::vector<zone> return_list;
	//For some reason, the final list is being sorted backwards. It seems to be because of my lack of thoroughly thinking out
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

	delete split_calc->a_list;
	delete split_calc->b_list;
	delete split_calc->c_list;
	delete split_calc;

	return return_list;
}




SimpleBuildingDefense::SimpleBuildingDefense(AINicowar& ai) : ai(ai)
{
	ai.setDefenseModule(this);
}




bool SimpleBuildingDefense::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return findDefense();
		case 1:
			return updateFlags();
		case 2:
			return findCreatedDefenseFlags();
	}
	return false;
}




string SimpleBuildingDefense::getName() const
{
	return "SimpleBuildingDefense";
}




bool SimpleBuildingDefense::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{

	stream->readEnterSection("SimpleBuildingDefense");
	stream->readEnterSection("defending_zones");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		defenseRecord dr;
		unsigned int gbid=stream->readUint32();
		if(gbid==NOGBID)
			dr.flag=NULL;
		else
			dr.flag=getBuildingFromGid(ai.game, gbid);
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
	stream->readLeaveSection();
	return true;
}




void SimpleBuildingDefense::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("SimpleBuildingDefense");
	stream->writeEnterSection("defending_zones");
	stream->writeUint32(defending_zones.size());
	for(std::vector<defenseRecord>::const_iterator i=defending_zones.begin(); i!=defending_zones.end(); ++i)
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
	stream->writeLeaveSection();
}




bool SimpleBuildingDefense::findDefense()
{
	GridPollingSystem gps(ai);
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(building_health.find(b->gid) != building_health.end())
			{
				if(static_cast<int>(building_health[b->gid]) > b->hp)
				{
					bool found=false;
					for(vector<defenseRecord>::iterator j = defending_zones.begin(); j != defending_zones.end(); ++j)
					{
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
					dr.flagx=b->posX+(b->type->width/2);
					dr.flagy=b->posY+(b->type->height/2);
					dr.zonex=b->posX-DEFENSE_ZONE_BUILDING_PADDING;
					dr.zoney=b->posY-DEFENSE_ZONE_BUILDING_PADDING;
					dr.width=b->type->width+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.height=b->type->height+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.assigned=gps.pollArea(dr.zonex, dr.zoney, dr.width, dr.height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_WARRIORS);
					defending_zones.push_back(dr);
					Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: findDefense: Creating a defense flag at "<<dr.flagx<<", "<<dr.flagy<<", to combat "<<dr.assigned<<" units that are attacking "<<b->posX<<","<<b->posY<<"."<<endl;
					ai.orders.push(new OrderCreate(ai.team->teamNumber, dr.flagx, dr.flagy, typeNum));
				}
			}
		}
	}

	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			building_health[b->gid]=std::min(b->type->hpMax, b->hp);
		}
	}
	return false;
}




bool SimpleBuildingDefense::updateFlags()
{
	GridPollingSystem gps(ai);
	for (vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end();)
	{
		unsigned int score = gps.pollArea(i->zonex, i->zoney, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_WARRIORS);
		if(score==0 && i->flag!=NULL)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateFlags: Found a zone at "<<i->zonex<<","<<i->zoney<<" that no longer has any enemy units in it. Removing this flag."<<endl;
			ai.orders.push(new OrderDelete(i->flag->gid));
			i=defending_zones.erase(i);
		}
		else
		{
			if(i->flag!=NULL)
				if(static_cast<int>(score)!=i->flag->maxUnitWorking)
			{
				ai.orders.push(new OrderModifyBuilding(i->flag->gid, score));
			}
			++i;
		}
	}
	return false;
}




bool SimpleBuildingDefense::findCreatedDefenseFlags()
{
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
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
						ai.orders.push(new OrderModifyFlag(b->gid, std::max(i->width, i->height)/2));
						ai.orders.push(new OrderModifyBuilding(b->gid, i->assigned));
						break;
					}
				}
			}
		}
	}
	return false;
}




GeneralsDefense::GeneralsDefense(AINicowar& ai) : ai(ai)
{
	ai.setDefenseModule(this);
}




bool GeneralsDefense::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return findEnemyFlags();
		case 1:
			return updateDefenseFlags();
	}
	return false;
}




string GeneralsDefense::getName() const
{
	return "GeneralsDefense";
}




bool GeneralsDefense::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("GeneralsDefense");
	stream->readEnterSection("defending_flags");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		defenseRecord dr;
		dr.flag=stream->readUint32();
		dr.enemy_flag=stream->readUint32();
		defending_flags.push_back(dr);
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void GeneralsDefense::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GeneralsDefense");
	stream->writeEnterSection("defending_flags");
	stream->writeUint32(defending_flags.size());
	for(std::vector<defenseRecord>::const_iterator i = defending_flags.begin(); i!=defending_flags.end(); ++i)
	{
		stream->writeUint32(i->flag);
		stream->writeUint32(i->enemy_flag);
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool GeneralsDefense::findEnemyFlags()
{
	GridPollingSystem gps(ai);
	for (unsigned int t=0; t<32; t++)
	{
		Team* team = ai.game->teams[t];
		if(team)
		{
			if(team->me & ai.team->enemies)
			{
				for(unsigned int n=0; n<1024; ++n)
				{
					Building* b = team->myBuildings[n];
					if(b)
					{
						if(b->type->shortTypeNum==IntBuildingType::WAR_FLAG)
						{

							unsigned int score = gps.pollArea((b->posX)-(b->unitStayRange)-DEFENSE_ZONE_SIZE_INCREASE,
								(b->posY)-(b->unitStayRange)-DEFENSE_ZONE_SIZE_INCREASE,
								b->unitStayRange*2+DEFENSE_ZONE_SIZE_INCREASE*2,
								b->unitStayRange*2+DEFENSE_ZONE_SIZE_INCREASE*2, GridPollingSystem::MAXIMUM,
								GridPollingSystem::FRIENDLY_BUILDINGS);

							if(score>0)
							{
								bool found=false;
								for(vector<defenseRecord>::iterator i = defending_flags.begin(); i!= defending_flags.end(); ++i)
								{
									if(i->enemy_flag == b->gid)
										found=true;
								}
								if(found)
									continue;

								defenseRecord dr;
								dr.flag=NOGBID;
								dr.enemy_flag=b->gid;
								defending_flags.push_back(dr);
								if(AINicowar_DEBUG)
									std::cout<<"AINicowar: findEnemyFlags: Creating new flag at "<<b->posX<<","<<b->posY<<" to combat an enemy attack!"<<endl;
								Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
								ai.orders.push(new OrderCreate(ai.team->teamNumber, b->posX, b->posY, typeNum));
							}
						}
					}
				}
			}
		}
	}
	return false;
}




bool GeneralsDefense::updateDefenseFlags()
{
	for(vector<defenseRecord>::iterator i = defending_flags.begin(); i!= defending_flags.end();)
	{
		if(buildingStillExists(ai.game, i->enemy_flag)==false)
		{
			i=defending_flags.erase(i);
			continue;
		}

		if(i->flag==NOGBID)
		{
			Building* eb = getBuildingFromGid(ai.game, i->enemy_flag);
			for(unsigned int n=0; n<1024; ++n)
			{
				Building* b = ai.team->myBuildings[n];
				if(b)
				{
					if(b->type->shortTypeNum == IntBuildingType::WAR_FLAG && b->posX==eb->posX && b->posY==eb->posY)
					{
						i->flag=b->gid;
						ai.orders.push(new OrderModifyFlag(i->flag, eb->unitStayRange));
						ai.orders.push(new OrderModifyBuilding(i->flag, eb->maxUnitWorking));
						ai.orders.push(new OrderModifyMinLevelToFlag(i->flag, eb->minLevelToFlag));
						break;
					}
				}
			}
		}
		++i;
	}
	return false;
}




PrioritizedBuildingAttack::PrioritizedBuildingAttack(AINicowar& ai) : ai(ai)
{
	ai.setAttackModule(this);
	enemy=NULL;
}




bool PrioritizedBuildingAttack::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return targetEnemy();
		case 1:
			return attack();
		case 2:
			return updateAttackFlags();
	}
	return false;
}




string PrioritizedBuildingAttack::getName() const
{
	return "PrioritizedBuildingAttack";
}




bool PrioritizedBuildingAttack::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("PrioritizedBuildingAttack");
	stream->readEnterSection("attacks");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		attackRecord ar;
		ar.target=stream->readUint32();
		unsigned int gbid=stream->readUint32();
		if(gbid==NOGBID)
			ar.flag=NULL;
		else
			ar.flag=getBuildingFromGid(ai.game, gbid);
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
	stream->readLeaveSection();
	return true;
}




void PrioritizedBuildingAttack::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("PrioritizedBuildingAttack");
	stream->writeEnterSection("attacks");
	stream->writeUint32(attacks.size());
	for(std::vector<attackRecord>::const_iterator i = attacks.begin(); i!=attacks.end(); ++i)
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
	stream->writeLeaveSection();
}




bool PrioritizedBuildingAttack::targetEnemy()
{
	if(enemy==NULL || !enemy->isAlive)
	{
		for(int i=0; i<32; ++i)
		{
			Team* t = ai.game->teams[i];
			if(t)
			{
				if(t->me & ai.team->enemies && t->isAlive)
				{
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: targetEnemy: A new enemy has been chosen."<<endl;
					enemy=t;
					return false;
				}
			}
		}
	}
	return false;
}




bool PrioritizedBuildingAttack::attack()
{
	GridPollingSystem gps(ai);
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
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING && b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(b->type->level+1));
			}
		}
	}

	//If we don't have enough barracks, don't bother doing anything, otherwise, make sure where producing warriors.
	if(max_barracks_level<MINIMUM_BARRACKS_LEVEL+1)
		return false;
	else
		ai.getUnitModule()->changeUnits("attack", WARRIOR, BASE_ATTACK_WARRIORS, 0, 0);

	unsigned int available_units = getFreeUnits(ai.team, ATTACK_STRENGTH, max_barracks_level+1);

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

				if(available_units<ATTACK_WARRIOR_MINIMUM)
					return false;

				attackRecord ar;
				ar.target=b->gid;
				ar.target_x=b->posX;
				ar.target_y=b->posY;
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
				ar.assigned_units=std::min(std::min(static_cast<unsigned int>(20), available_units), std::max(gps.pollArea(ar.unitx, ar.unity, ar.unit_width, ar.unit_height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_UNITS)*2, ATTACK_WARRIOR_MINIMUM));
				ar.assigned_level=max_barracks_level;
				attacks.push_back(ar);
				Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: attack: Creating a war flag at "<<ar.flagx<<", "<<ar.flagy<<" and assigning "<<ar.assigned_units<<" units to fight and kill the building at "<<b->posX<<","<<b->posY<<"."<<endl;
				ai.orders.push(new OrderCreate(ai.team->teamNumber, ar.flagx, ar.flagy, typeNum));
				++attack_count;
				available_units-=ar.assigned_units;
			}
		}
	}
	return false;
}




bool PrioritizedBuildingAttack::updateAttackFlags()
{
	GridPollingSystem gps(ai);
	for(int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
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

						ai.orders.push(new OrderModifyFlag(b->gid, radius));
						ai.orders.push(new OrderModifyBuilding(b->gid, j->assigned_units));
						ai.orders.push(new OrderModifyMinLevelToFlag(b->gid, j->assigned_level));
						break;
					}
				}
			}
		}
	}

	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		Building* b = getBuildingFromGid(ai.game, j->target);
		if(b==NULL || b->posX!=static_cast<int>(j->target_x) || b->posY!=static_cast<int>(j->target_y))
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateAttackFlags: Stopping attack on a building, removing the "<<j->flag->posX<<","<<j->flag->posY<<" flag."<<endl;
			ai.orders.push(new OrderDelete(j->flag->gid));
			j=attacks.erase(j);
			continue;
		}
		else
		{
			++j;
		}
	}

	unsigned int max_barracks_level=0;
	for(int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING && b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(b->type->level+1));
			}
		}
	}
	unsigned int available_units = getFreeUnits(ai.team, ATTACK_STRENGTH, max_barracks_level+1);

	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		if(j->flag == NULL)
		{
			++j;
			continue;
		}

		std::cout<<j->flag->gid<<endl;
		available_units+=j->flag->unitsWorking.size();
		unsigned int score = gps.pollArea(j->unitx, j->unity, j->unit_width, j->unit_height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_UNITS);
		unsigned int new_assigned=std::min(std::min(static_cast<unsigned int>(20), available_units), std::max(score*2, ATTACK_WARRIOR_MINIMUM));

		if(new_assigned<ATTACK_WARRIOR_MINIMUM)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateAttackFlags: Stopping attack, not enough free warriors, removing the "<<j->flag->posX<<","<<j->flag->posY<<" flag."<<endl;
			ai.orders.push(new OrderDelete(j->flag->gid));
			j=attacks.erase(j);
			std::cout<<j->flag->gid<<endl;
			continue;

		}

		if(new_assigned != j->assigned_units)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updateAttackFlags: Changing the "<<j->flag->posX<<","<<j->flag->posY<<" flag from "<<j->assigned_units<<" to "<<new_assigned<<"."<<endl;
			j->assigned_units=new_assigned;
			ai.orders.push(new OrderModifyBuilding(j->flag->gid, new_assigned));
			available_units-=new_assigned;
		}

		if(max_barracks_level != j->assigned_level)
		{
			j->assigned_level=max_barracks_level;
			ai.orders.push(new OrderModifyMinLevelToFlag(j->flag->gid, max_barracks_level));
		}
		++j;
	}

	return false;
}




DistributedNewConstructionManager::DistributedNewConstructionManager(AINicowar& ai) : ai(ai)
{
	ai.setNewConstructionModule(this);
}




bool DistributedNewConstructionManager::perform(unsigned int time_slice_n)
{
	//Keep the number of units we want updated
	ai.getUnitModule()->changeUnits("DistributedNewConstructionManager", WORKER, MAXIMUM_TO_CONSTRUCT_NEW*MAX_NEW_CONSTRUCTION_AT_ONCE, MINIMUM_TO_CONSTRUCT_NEW, 0);
	switch(time_slice_n)
	{
		case 0:
			return constructBuildings();
		case 1:
			return updateBuildings();
		case 2:
			return calculateBuildings();
	}
	return false;
}




string DistributedNewConstructionManager::getName() const
{
	return "DistributedNewConstructionManager";
}




bool DistributedNewConstructionManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DistributedNewConstructionManager");
	stream->readEnterSection("new_buildings");
	unsigned int n = stream->readUint32();
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
	stream->readLeaveSection();
	return true;
}




void DistributedNewConstructionManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("DistributedNewConstructionManager");
	stream->writeEnterSection("new_buildings");
	stream->writeUint32(new_buildings.size());
	for(std::vector<newConstructionRecord>::const_iterator i = new_buildings.begin(); i!=new_buildings.end(); ++i)
	{
		stream->writeUint32(i->building);
		stream->writeUint32(i->x);
		stream->writeUint32(i->y);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->building_type);
	}

	stream->writeEnterSection("num_buildings_wanted");
	stream->writeUint32(num_buildings_wanted.size());
	for(std::map<unsigned int, unsigned int>::const_iterator i = num_buildings_wanted.begin(); i!=num_buildings_wanted.end(); ++i)
	{
		stream->writeUint32(i->first);
		stream->writeUint32(i->second);
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




DistributedNewConstructionManager::upgradeData DistributedNewConstructionManager::findMaxSize(unsigned int building_type, unsigned int cur_level)
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




DistributedNewConstructionManager::point DistributedNewConstructionManager::findBestPlace(unsigned int building_type)
{
	GridPollingSystem gps(ai);
	//Get the best zone based on a number of factors
	GridPollingSystem::poll zp;
	zp.mod_1=static_cast<GridPollingSystem::pollModifier>(CONSTRUCTION_FACTORS[building_type][0][0]);
	zp.type_1=static_cast<GridPollingSystem::pollType>(CONSTRUCTION_FACTORS[building_type][0][1]);
	zp.mod_2=static_cast<GridPollingSystem::pollModifier>(CONSTRUCTION_FACTORS[building_type][1][0]);
	zp.type_2=static_cast<GridPollingSystem::pollType>(CONSTRUCTION_FACTORS[building_type][1][1]);
	zp.mod_3=static_cast<GridPollingSystem::pollModifier>(CONSTRUCTION_FACTORS[building_type][2][0]);
	zp.type_3=static_cast<GridPollingSystem::pollType>(CONSTRUCTION_FACTORS[building_type][2][1]);
	zp.mod_minimum=GridPollingSystem::MAXIMUM;
	zp.minimum_type=GridPollingSystem::FRIENDLY_BUILDINGS;
	zp.minimum_score=MINIMUM_NEARBY_BUILDINGS_TO_CONSTRUCT;

	GridPollingSystem::getBestZonesSplit* split = gps.getBestZones(zp, BUILD_AREA_WIDTH, BUILD_AREA_HEIGHT, BUILD_AREA_HORIZONTAL_OVERLAP,
		BUILD_AREA_VERTICAL_OVERLAP, BUILD_AREA_EXTENTION_WIDTH, BUILD_AREA_EXTENTION_HEIGHT);

	vector<GridPollingSystem::zone> zones = gps.getBestZones(split);

	//Iterate through the zones
	for(std::vector<GridPollingSystem::zone>::iterator i = zones.begin(); i!=zones.end(); ++i)
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
				if(!ai.map->isHardSpaceForBuilding(fx, fy) || !ai.map->isMapDiscovered(fx, fy, ai.team->me))
				{
					imap[x][y]=1;
				}

				//If we find a building here (or a building that is in the list of buildings to be constructed)
				bool found_building=false;
				upgradeData size;
				if(ai.map->getBuilding(fx, fy)!=NOGBID)
				{
					Building* b=getBuildingFromGid(ai.game, ai.map->getBuilding(fx, fy));
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




bool DistributedNewConstructionManager::constructBuildings()
{
	unsigned total_free_workers=0;
	for(int i=0; i<NB_UNIT_LEVELS; ++i)
	{
		total_free_workers+=getFreeUnits(ai.team, BUILD, i);
	}

	//Counts out the number of buildings allready existing, including ones under construction
	unsigned int counts[IntBuildingType::NB_BUILDING];
	//The totals of the number of builinds under construction only
	unsigned int under_construction_counts[IntBuildingType::NB_BUILDING];
	//The total amount of construction
	unsigned int total_construction=new_buildings.size();
	//Holds the order in which it should decide to build buildings.
	std::vector<typePercent> construction_priorities;

	if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
	{
		//		std::cout<<"Fail 1, too much construction."<<endl;
		return false;
	}

	for(unsigned i = 0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		counts[i]=ai.team->stats.getLatestStat()->numberBuildingPerType[i];
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
		if(i->building!=NOGBID && getBuildingFromGid(ai.game, i->building)!=NULL)
			total_free_workers+=getBuildingFromGid(ai.game, i->building)->unitsWorking.size();
	}

	for (unsigned int i = 0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		typePercent tp;
		tp.building_type=i;
		if(num_buildings_wanted[i]==0)
			tp.percent=100;
		else
			tp.percent=static_cast<unsigned int>(static_cast<float>(counts[i])/static_cast<float>(num_buildings_wanted[i])*100);
		construction_priorities.push_back(tp);
	}

	std::sort(construction_priorities.begin(), construction_priorities.end());

	//These numbers are because I don't believe a building size will ever exceed them.
	unsigned int min_failed_width=512;
	unsigned int min_failed_height=512;

	for(std::vector<typePercent>::iterator i = construction_priorities.begin(); i!=construction_priorities.end(); ++i)
	{
		while(true)
		{
			if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
			{
				//				std::cout<<"Fail 1, too much construction, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				return false;
			}
			if(total_free_workers<MINIMUM_TO_CONSTRUCT_NEW && !CHEAT_INSTANT_BUILDING)
			{
				//				std::cout<<"Fail 2, too few units, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				return false;
			}
			if(under_construction_counts[i->building_type]>=MAX_NEW_CONSTRUCTION_PER_BUILDING[i->building_type])
			{
				//				std::cout<<"Fail 3, too many buildings of this type under constructon, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				break;
			}
			if(counts[i->building_type]>=num_buildings_wanted[i->building_type])
			{
				//				std::cout<<"Fail 4, building cap reached, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				break;
			}
			point p = findBestPlace(i->building_type);
			upgradeData size=findMaxSize(i->building_type, 0);
			//If there wasn't a place for a smaller building than this one, than there certainly won't be a place for this one
			if(size.width>=min_failed_width && size.height>=min_failed_height)
				break;

			if(p.x == NOPOS || p.y==NOPOS)
			{
				min_failed_width=size.width;
				min_failed_height=size.height;
				//				std::cout<<"Fail 5, no suitable positon found, for "<<IntBuildingType::reverseConversionMap[i]<<"."<<endl;
				break;
			}

			newConstructionRecord ncr;
			ncr.building=NOGBID;
			ncr.x=p.x;
			ncr.y=p.y;
			//Don't assign more units than the total amount of resources needed to construct the building.
			unsigned int needed_resource_total=0;
			BuildingType* t=globalContainer->buildingsTypes.getByType(IntBuildingType::reverseConversionMap[i->building_type], 0, true);
			for(unsigned int n=0; n<MAX_NB_RESSOURCES; ++n)
			{
				needed_resource_total+=t->maxRessource[n];
			}
			ncr.assigned=std::min(total_free_workers, std::min(MAXIMUM_TO_CONSTRUCT_NEW, needed_resource_total));
			ncr.building_type=i->building_type;
			new_buildings.push_back(ncr);

			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: constructBuildings: Starting construction on a "<<IntBuildingType::reverseConversionMap[i->building_type]<<", at position "<<p.x<<","<<p.y<<"."<<endl;
			Sint32 type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[i->building_type], 0, !CHEAT_INSTANT_BUILDING);
			ai.orders.push(new OrderCreate(ai.team->teamNumber, p.x, p.y, type));
			total_construction+=1;
			total_free_workers-=ncr.assigned;
			under_construction_counts[i->building_type]++;
			counts[i->building_type]++;
		}
	}
	return false;
}




bool DistributedNewConstructionManager::updateBuildings()
{
	//Remove records of buildings that are no longer under construction
	for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end();)
	{
		if(i->building!=NOGBID)
		{
			Building* b = getBuildingFromGid(ai.game, i->building);
			if(b==NULL || b->constructionResultState == Building::NO_CONSTRUCTION)
			{
				ai.orders.push(new OrderModifyBuilding(i->building, 1));
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
		Building *b = ai.team->myBuildings[i];
		if(b)
		{
			for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); ++i)
			{
				if(i->building==NOGBID && b->type->shortTypeNum == static_cast<int>(i->building_type) && b->posX == static_cast<int>(i->x) && b->posY == static_cast<int>(i->y))
				{
					i->building=b->gid;
					ai.orders.push(new OrderModifyBuilding(i->building, i->assigned));
				}
			}
		}
	}
	return false;
}




bool DistributedNewConstructionManager::calculateBuildings()
{
	unsigned int total_units=ai.team->stats.getLatestStat()->totalUnit;
	for (int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		if(UNITS_FOR_BUILDING[i]!=0)
			num_buildings_wanted[i]=total_units/UNITS_FOR_BUILDING[i]+1;
		else
			num_buildings_wanted[i]=0;
	}
	return false;
}




bool DistributedNewConstructionManager::typePercent::operator<(const typePercent& tp) const
{
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]>STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return true;
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]<STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return false;

	return percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[building_type]<tp.percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[tp.building_type];
}




bool DistributedNewConstructionManager::typePercent::operator>(const typePercent& tp) const
{
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]<STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return true;
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]>STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return false;
	return percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[building_type]>tp.percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[tp.building_type];
}




RandomUpgradeRepairModule::RandomUpgradeRepairModule(AINicowar& ai) : ai(ai)
{
	ai.setUpgradeRepairModule(this);
	ai.getUnitModule()->changeUnits("RandomUpgradeRepairModule", WORKER, MAXIMUM_TO_UPGRADE*MAX_CONSTRUCTION_AT_ONCE, MINIMUM_TO_UPGRADE, 0);
}




bool RandomUpgradeRepairModule::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return removeOldConstruction();
		case 1:
			return updatePendingConstruction();
		case 2:
			return startNewConstruction();
		case 3:
			return reassignConstruction();
	}
	return false;
}




string RandomUpgradeRepairModule::getName() const
{
	return "RandomUpgradeRepairModule";
}




bool RandomUpgradeRepairModule::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RandomUpgradeRepairModule");
	stream->readEnterSection("active_construction");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		constructionRecord cr;
		cr.building=getBuildingFromGid(ai.game, stream->readUint32());
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
		cr.building=getBuildingFromGid(ai.game, stream->readUint32());
		cr.assigned=stream->readUint32();
		cr.original=stream->readUint32();
		cr.is_repair=stream->readUint8();
		pending_construction.push_back(cr);
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void RandomUpgradeRepairModule::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("RandomUpgradeRepairModule");
	stream->writeEnterSection("active_construction");
	stream->writeUint32(active_construction.size());
	for(std::list<constructionRecord>::const_iterator i = active_construction.begin(); i!=active_construction.end(); ++i)
	{
		stream->writeUint32(i->building->gid);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->original);
		stream->writeUint8(i->is_repair);
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("pending_construction");
	stream->writeUint32(pending_construction.size());
	for(std::list<constructionRecord>::const_iterator i = pending_construction.begin(); i!=pending_construction.end(); ++i)
	{
		stream->writeUint32(i->building->gid);
		stream->writeUint32(i->assigned);
		stream->writeUint32(i->original);
		stream->writeUint8(i->is_repair);
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool RandomUpgradeRepairModule::removeOldConstruction(void)
{
	for (std::list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(ai.game, b))
		{
			i=active_construction.erase(i);
			continue;
		}

		unsigned int original = i->original;
		if (b->constructionResultState!=Building::UPGRADE && b->constructionResultState!=Building::REPAIR )
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: removeOldConstruction: Removing an old "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" from the active construction list, changing assigned number of units back to "<<original<<" from "<<i->assigned<<"."<<endl;
			i=active_construction.erase(i);
			ai.orders.push(new OrderModifyBuilding(b->gid, original));
			continue;
		}
		i++;
	}
	return false;
}




bool RandomUpgradeRepairModule::updatePendingConstruction(void)
{
	for (std::list<constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(ai.game, b))
		{
			i=active_construction.erase(i);
			continue;
		}
		unsigned int assigned = i->assigned;
		if (b->buildingState != Building::WAITING_FOR_CONSTRUCTION && b->buildingState != Building::WAITING_FOR_CONSTRUCTION_ROOM)
		{
			constructionRecord u=*i;
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updatePendingConstruction: The "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" was found that it is no longer pending construction, I am assigning number of requested units, "<<assigned<<", to it."<<endl;
			active_construction.push_back(u);
			i=pending_construction.erase(i);
			ai.orders.push(new OrderModifyBuilding(b->gid, assigned));
			continue;
		}
		i++;
	}
	return false;
}




int RandomUpgradeRepairModule::getAvailableUnitsForConstruction(int level)
{
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(ai.team, BUILD, j+1);
	}

	int total_free = 0;
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		if ((j+1)>=level)
			total_free+=free_workers[j];
	}
	return total_free;
}




bool RandomUpgradeRepairModule::reassignConstruction(void)
{
	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(ai.team, BUILD, j+1);
	}

	//Add in the numbers of units that are already working
	for (list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		if(buildingStillExists(ai.game, b))
		{
			for (std::list<Unit*>::iterator i = b->unitsWorking.begin(); i!=b->unitsWorking.end(); i++)
			{
				Unit* u=*i;
				free_workers[u->level[BUILD]]+=1;
			}
		}
	}

	//Finally, iterate through the shuffled list of records changing the number of units allocated to upgrade the buildings.
	for (list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		if(!buildingStillExists(ai.game, b))
			continue;
		if(b->constructionResultState==Building::NO_CONSTRUCTION)
			continue;

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
			ai.orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		else if (is_repair && available_repair==0)
		{
			std::cout<<b->constructionResultState<<endl;
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: There are not enough available units. Canceling repair on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			ai.orders.push(new OrderCancelConstruction(b->gid));
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
			ai.orders.push(new OrderModifyBuilding(b->gid, num_to_assign));
			i->assigned=num_to_assign;
		}
		reduce(free_workers, b->type->level, num_to_assign);
	}
	return false;
}




bool RandomUpgradeRepairModule::startNewConstruction(void)
{
	//If we already have more than our max, don't do any more
	if (active_construction.size()+pending_construction.size()>=MAX_CONSTRUCTION_AT_ONCE)
		return false;

	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(ai.team, BUILD, j+1);
	}

	//Look through each building we previoussly assigned to be upgraded, and subtract their requested number of units from the free ones (ones that they haven't recieved yet but wanted.). Also take not of the type of building, counting up the numbers for each type.
	int construction_counts[IntBuildingType::NB_BUILDING];
	for (int j=0; j!=IntBuildingType::NB_BUILDING; j++)
		construction_counts[j]=0;

	for (list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		if(buildingStillExists(ai.game, b))
		{
			unsigned int assigned = i->assigned;
			reduce(free_workers, b->type->level, assigned-b->unitsWorking.size());
			construction_counts[b->type->shortTypeNum]+=1;
		}
	}

	for (list<constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end(); i++)
	{
		Building *b=i->building;
		unsigned int assigned = i->assigned;
		reduce(free_workers, b->type->level, assigned-b->unitsWorking.size());
		construction_counts[b->type->shortTypeNum]+=1;
	}

	//Make a copy of the array of buildings, then shuffle it so that every building has an equal chance of being upgraded with the spare units.
	vector<Building*> buildings;
	buildings.reserve(1024);
	for (int i=0; i<1024; i+=1)
	{
		Building *b=ai.team->myBuildings[i];
		if (b)
			buildings.push_back(b);
	}
	random_shuffle(buildings.begin(), buildings.end(), syncRandAdapter);

	//Look through the buildings and find one that needs to be upgraded or repaired, and if their are atleast 4 of the correct unit type available to upgrade it, then do it.
	for (vector<Building*>::iterator i = buildings.begin(); i!=buildings.end(); i++)
	{
		Building *b=*i;
		if (b)
		{
			//Check if its worthy of having anything done to it.
			if (b->constructionResultState==Building::NO_CONSTRUCTION          &&
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
				if (b->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING             &&
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
					constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=true;
					pending_construction.push_back(u);
					ai.orders.push(new OrderConstruction(b->gid));
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
					constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=false;
					pending_construction.push_back(u);
					ai.orders.push(new OrderConstruction(b->gid));
					reduce(free_workers, b->type->level+1, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}
			}
		}
	}
	return false;
}




BasicDistributedSwarmManager::BasicDistributedSwarmManager(AINicowar& ai) : ai(ai)
{
	ai.setUnitModule(this);
}




bool BasicDistributedSwarmManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return moderateSwarms();
	}
	return false;
}




string BasicDistributedSwarmManager::getName() const
{
	return "BasicDistributedSwarmManager";
}




bool BasicDistributedSwarmManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BasicDistributedSwarmManager");
	stream->readEnterSection("module_demands");
	unsigned int n = stream->readUint32();
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
	stream->readLeaveSection();
	return true;
}




void BasicDistributedSwarmManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BasicDistributedSwarmManager");
	stream->writeEnterSection("module_demands");
	stream->writeUint32(module_demands.size());
	for(std::map<string, unitRecord>::const_iterator i = module_demands.begin(); i!=module_demands.end(); ++i)
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
	stream->writeLeaveSection();
}




void BasicDistributedSwarmManager::changeUnits(string module_name, unsigned int unit_type, unsigned int desired_units, unsigned int required_units, unsigned int emergency_units)
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




bool BasicDistributedSwarmManager::moderateSwarms()
{
	//The number of units we want for each priority level
	unsigned int num_wanted[NB_UNIT_TYPE][3];
	unsigned int total_available[NB_UNIT_TYPE];
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		total_available[i]=ai.team->stats.getLatestStat()->numberUnitPerType[i];
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
	ai.getUnitModule()->changeUnits("spawn-manager", WORKER, 0, ai.team->swarms.size()*assigned_per_swarm, 0);

	bool need_to_output=true;
	for (std::list<Building*>::iterator i = ai.team->swarms.begin(); i != ai.team->swarms.end(); ++i)
	{
		Building* swarm=*i;
		bool changed=false;
		for (int x = 0; x<NB_UNIT_TYPE; x++)
			if (swarm->ratio[x]!=ratios[x])
				changed=true;

		if(swarm->maxUnitWorking != static_cast<int>(assigned_per_swarm))
			ai.orders.push(new OrderModifyBuilding(swarm->gid, assigned_per_swarm));

		if(!changed)
			continue;

		if(AINicowar_DEBUG && need_to_output)
			std::cout<<"AINicowar: moderateSpawns: Turning changing production ratios on a swarm from {Worker:"<<swarm->ratio[0]<<", Explorer:"<<swarm->ratio[1]<<", Warrior:"<<swarm->ratio[2]<<"} to {Worker:"<<ratios[0]<<", Explorer:"<<ratios[1]<<", Warrior:"<<ratios[2]<<"}. Assigning "<<assigned_per_swarm<<" workers."<<endl;
		need_to_output=false;
		ai.orders.push(new OrderModifySwarm(swarm->gid, ratios));
	}
	return false;
}




ExplorationManager::ExplorationManager(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
	explorers_wanted=0;
	developing_attack_explorers=false;
	explorer_attacking=false;
}




bool ExplorationManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return exploreWorld();
		case 1:
			return updateExplorationFlags();
		case 2:
			return moderateSwarmsForExplorers();
		case 3:
			return explorerAttack();
	}
	return false;
}




string ExplorationManager::getName() const
{
	return "ExplorationManager";
}




bool ExplorationManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ExplorationManager");
	stream->readEnterSection("active_exploration");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		explorationRecord er;
		unsigned int gid=stream->readUint32();
		if(!gid)
			er.flag=NULL;
		else
			er.flag=getBuildingFromGid(ai.game, gid);
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
	explorers_wanted=stream->readUint32("explorers_wanted");
	developing_attack_explorers=stream->readUint8("developing_attack_explorers");
	explorer_attacking=stream->readUint8("explorer_attacking");
	stream->readLeaveSection();
	return true;
}




void ExplorationManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("ExplorationManager");
	stream->writeEnterSection("active_exploration");
	stream->writeUint32(active_exploration.size());
	for(std::list<explorationRecord>::const_iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
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
	stream->writeUint32(explorers_wanted, "explorers_wanted");
	stream->writeUint8(developing_attack_explorers, "developing_attack_explorers");
	stream->writeUint8(explorer_attacking, "explorer_attacking");
	stream->writeLeaveSection();
}




bool ExplorationManager::exploreWorld(void)
{
	GridPollingSystem gps(ai);
	unsigned int exploration_count=0;
	unsigned int attack_count=0;
	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
	{

		if(i->isAssaultFlag)
			attack_count++;
		else
			exploration_count++;
	}

	int wanted_exploration=EXPLORER_MAX_REGIONS_AT_ONCE-exploration_count;
	int wanted_attack=EXPLORER_ATTACKS_AT_ONCE-attack_count;
	if(! explorer_attacking )
		wanted_attack=0;

	if(wanted_exploration==0 && wanted_attack==0)
		return false;

	GridPollingSystem::poll p;
	p.mod_1=GridPollingSystem::MAXIMUM;
	p.type_1=GridPollingSystem::HIDDEN_SQUARES;
	p.mod_2=GridPollingSystem::MINIMUM;
	p.type_2=GridPollingSystem::CENTER_DISTANCE;
	p.mod_minimum=GridPollingSystem::MAXIMUM;
	p.minimum_type=GridPollingSystem::VISIBLE_SQUARES;
	p.minimum_score=1;
	GridPollingSystem::getBestZonesSplit* split = gps.getBestZones(p, EXPLORER_REGION_WIDTH, EXPLORER_REGION_HEIGHT, EXPLORER_REGION_HORIZONTAL_OVERLAP,
		EXPLORER_REGION_VERTICAL_OVERLAP, EXPLORER_REGION_HORIZONTAL_EXTENTION,
		EXPLORER_REGION_VERTICAL_EXTENTION);
	vector<GridPollingSystem::zone> best = gps.getBestZones(split);
	unsigned int size=best.size();

	//	std::cout<<"best.size()="<<best.size()<<endl;

	p.mod_1=GridPollingSystem::MAXIMUM;
	p.type_1=GridPollingSystem::ENEMY_BUILDINGS;
	p.mod_2=GridPollingSystem::MAXIMUM;
	p.type_2=GridPollingSystem::NONE;
	p.mod_minimum=GridPollingSystem::MAXIMUM;
	p.minimum_type=GridPollingSystem::ENEMY_BUILDINGS;
	p.minimum_score=1;
	split = gps.getBestZones(p, EXPLORER_ATTACK_AREA_WIDTH, EXPLORER_ATTACK_AREA_HEIGHT, EXPLORER_ATTACK_AREA_HORIZONTAL_OVERLAP,
		EXPLORER_ATTACK_AREA_VERTICAL_OVERLAP, EXPLORER_ATTACK_AREA_HORIZONTAL_EXTENTION,
		EXPLORER_ATTACK_AREA_VERTICAL_EXTENTION);

	vector<GridPollingSystem::zone> best_attack = gps.getBestZones(split);

	std::copy(best_attack.begin(), best_attack.end(), back_insert_iterator<vector<GridPollingSystem::zone> >(best));

	vector<GridPollingSystem::zone>::iterator attack_start=best.begin()+size;

	///Erase any zones that already have flags in them
	for(vector<GridPollingSystem::zone>::iterator i = best.begin(); i!=best.end();)
	{
		if(!isFreeOfFlags(i->x+i->width/2, i->y+i->height/2))
		{
			//			std::cout<<"removing zone from list!"<<endl;
			i = best.erase(i);
		}
		else
		{
			++i;
		}
	}

	vector<GridPollingSystem::zone>::iterator top=best.begin();
	if(top<attack_start && gps.pollArea(top->x, top->y, top->width, top->height, GridPollingSystem::MAXIMUM, GridPollingSystem::HIDDEN_SQUARES)==0)
	{
		//		std::cout<<"Doing nothing! wanted_exploration="<<wanted_exploration<<";"<<endl;
		return false;
	}

	for (vector<GridPollingSystem::zone>::iterator i=best.begin(); i!=best.end(); ++i)
	{
		//		std::cout<<"wanted_exploration="<<wanted_exploration<<"; "<<endl;
		GridPollingSystem::zone z = *i;
		if((i>=attack_start && wanted_attack==0) || (wanted_attack==0 && wanted_exploration==0))
			break;

		if(wanted_exploration==0)
			continue;

		explorationRecord exploration_record;
		//This flag will be picked up by findCreatedFlags
		exploration_record.flag=NULL;
		exploration_record.flag_x=z.x+(z.width/2);
		exploration_record.flag_y=z.y+(z.height/2);
		exploration_record.zone_x=z.x;
		exploration_record.zone_y=z.y;
		exploration_record.width=z.width;
		exploration_record.height=z.height;
		if(i<attack_start)
		{
			exploration_record.assigned=EXPLORERS_PER_REGION;
			exploration_record.radius=EXPLORATION_FLAG_RADIUS;
			exploration_record.isAssaultFlag=false;
			wanted_exploration--;
		}
		else
		{
			exploration_record.assigned=EXPLORERS_PER_ATTACK;
			exploration_record.radius=EXPLORATION_FLAG_ATTACK_RADIUS;
			exploration_record.isAssaultFlag=true;
			wanted_attack--;
		}

		active_exploration.push_back(exploration_record);

		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
		if(AINicowar_DEBUG)
			std::cout<<"AINicowar: exploreWorld: Creating a new flag at position "<<exploration_record.flag_x<<","<<exploration_record.flag_y<<"."<<endl;
		ai.orders.push(new OrderCreate(ai.team->teamNumber, exploration_record.flag_x, exploration_record.flag_y,typeNum));
		explorers_wanted+=exploration_record.assigned;
	}
	return false;
}




bool ExplorationManager::updateExplorationFlags(void)
{
	GridPollingSystem gps(ai);
	//Iterate through the buildings (which includes flags), looking for a flag that is not on the lists, that is in the right
	//spot to be one of our null records flags.
	Building** myBuildings=ai.team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				for (list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
				{
					if (i->flag==NULL && b->posX==static_cast<int>(i->flag_x) && b->posY==static_cast<int>(i->flag_y) && b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
					{
						i->flag=b;
						if(AINicowar_DEBUG)
							std::cout<<"AINicowar: updateExplorationFlags: Found a newly created flag matching our records at positon "<<b->posX<<","<<b->posY<<" , inserting this flag into our records."<<endl;
						ai.orders.push(new OrderModifyFlag(b->gid, i->radius));
						ai.orders.push(new OrderModifyBuilding(b->gid, i->assigned));
						break;
					}
				}
			}
		}
	}

	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end();)
	{
		if((!i->isAssaultFlag || explorer_attacking) && i->flag!=NULL)
		{
			int score=0;
			if(i->isAssaultFlag)
				score=gps.pollArea(i->zone_x, i->zone_y, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_BUILDINGS);
			else
				score=gps.pollArea(i->zone_x, i->zone_y, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::HIDDEN_SQUARES);

			if(score==0)
			{
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateExplorationFlags: Removing a flag this is no longer needed at "<<i->flag_x<<", "<<i->flag_y<<"."<<endl;
				ai.orders.push(new OrderDelete(i->flag->gid));
				explorers_wanted-=i->assigned;
				i=active_exploration.erase(i);
			}
			else
				++i;
		}
		else
			++i;
	}

	return false;
}




bool ExplorationManager::isFreeOfFlags(unsigned int x, unsigned int y)
{
	//Check if theres any flags of other types on the spot.
	Building** myBuildings=ai.team->myBuildings;
	for (int i=0; i<1024; i+=1)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			if (    b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG ||
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




bool ExplorationManager::moderateSwarmsForExplorers(void)
{
	//I've raised the priority on explorers temporarily for testing.
	//	changeUnits("aircontrol", EXPLORER, static_cast<int>(desired_explorers/2) , desired_explorers, 0);
	ai.getUnitModule()->changeUnits("aircontrol", EXPLORER, 0, static_cast<int>(explorers_wanted/2), explorers_wanted);
	return false;
}




bool ExplorationManager::explorerAttack(void)
{
	bool lvl3_school_exists=false;
	Building** myBuildings=ai.team->myBuildings;
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
		return false;
	}

	if (!developing_attack_explorers)
	{
		developing_attack_explorers=true;
		return false;
	}

	//check if we have enough explorers to start launching attacks.
	int ground_attack_explorers=0;
	Unit** myUnits = ai.team->myUnits;
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
		return false;
	if(AINicowar_DEBUG)
		if(!explorer_attacking)
			std::cout<<"AINicowar: explorerAttack: Enabling explorer attacks."<<endl;
	explorer_attacking=true;
	return false;
}




InnManager::InnManager(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool InnManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return recordInns();
		case 1:
			return modifyInns();
	}
	return false;
}




string InnManager::getName() const
{
	return "InnManager";
}




bool InnManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("InnManager");
	stream->readEnterSection("inns");
	unsigned int n = stream->readUint32();
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
	stream->readLeaveSection();
	return true;
}




void InnManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("InnManager");
	stream->writeEnterSection("inns");
	stream->writeUint32(static_cast<unsigned int>(inns.size()));
	for(std::map<int, innRecord>::const_iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		stream->writeUint32(i->first);
		stream->writeUint32(i->second.pos);
		stream->writeUint32(i->second.records.size());
		for(std::vector<singleInnRecord>::const_iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			stream->writeUint32(record->food_amount);
			stream->writeUint32(record->units_eating);
		}
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




InnManager::innRecord::innRecord() : pos(0), records(INN_RECORD_MAX) {}

bool InnManager::recordInns()
{
	for(int i=0; i<1024; ++i)
	{
		Building* b=ai.team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING && b->constructionResultState==Building::NO_CONSTRUCTION)
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
	return false;
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




bool InnManager::modifyInns()
{

	std::vector<innScore> scores;

	for(std::map<int, innRecord>::iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		innScore score;
		score.inn=getBuildingFromGid(ai.game, i->first);
		if (score.inn==NULL)
		{
			inns.erase(i);
			continue;
		}

		if (score.inn->constructionResultState!=Building::NO_CONSTRUCTION)
		{
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
		return false;

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
			ai.orders.push(new OrderModifyBuilding(to_increase->gid, to_increase->maxUnitWorking+1));
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
			ai.orders.push(new OrderModifyBuilding(to_decrease->gid, to_decrease->maxUnitWorking-1));
			break;
		}
	}
	ai.getUnitModule()->changeUnits("inn-manager", WORKER, inns.size(), total_workers_needed, 0);
	return false;
}




TowerController::TowerController(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool TowerController::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return controlTowers();
	}
	return false;
}




string TowerController::getName() const
{
	return "TowerController";
}




bool TowerController::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("TowerController");
	stream->readLeaveSection();
	return true;
}




void TowerController::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("TowerController");
	stream->writeLeaveSection();
}




bool TowerController::controlTowers()
{

	int count=0;
	for(int i=0; i<1024; i++)
	{
		Building* b = ai.team->myBuildings[i];
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
					ai.orders.push(new OrderModifyBuilding(b->gid, NUM_PER_TOWER));
				}
			}
		}
	}
	ai.getUnitModule()->changeUnits("tower-controller", WORKER, 0, count*NUM_PER_TOWER, 0);
	return false;
}




BuildingClearer::BuildingClearer(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool BuildingClearer::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return removeOldPadding();
		case 1:
			return updateClearingAreas();
	}
	return false;
}




string BuildingClearer::getName() const
{
	return "BuildingClearer";
}




bool BuildingClearer::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingClearer");
	unsigned int n = stream->readUint32();
	while(n--)
	{
		clearingRecord cr;
		cr.x=stream->readUint32();
		cr.y=stream->readUint32();
		cr.width=stream->readUint32();
		cr.height=stream->readUint32();
		cr.level=stream->readUint32();
		cleared_buildings[stream->readUint32()];
	}

	stream->readLeaveSection();
	return true;
}




void BuildingClearer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BuildingClearer");
	for(map<int, clearingRecord>::const_iterator i=cleared_buildings.begin(); i!=cleared_buildings.end(); ++i)
	{
		stream->writeUint32(i->first);
		stream->writeUint32(i->second.x);
		stream->writeUint32(i->second.y);
		stream->writeUint32(i->second.width);
		stream->writeUint32(i->second.height);
		stream->writeUint32(i->second.level);
	}
	stream->writeLeaveSection();
}




bool BuildingClearer::removeOldPadding()
{
	for(map<int, clearingRecord>::iterator i=cleared_buildings.begin(); i!=cleared_buildings.end();)
	{
		Building* b = getBuildingFromGid(ai.game, i->first);
		if(b==NULL || b->type->level != static_cast<int>(i->second.level))
		{

			unsigned int x_bound=i->second.x+i->second.width;
			unsigned int y_bound=i->second.y+i->second.height;
			if(static_cast<int>(x_bound) > ai.map->getW())
				x_bound-=ai.map->getW();
			if(static_cast<int>(y_bound) > ai.map->getH())
				y_bound-=ai.map->getH();

			BrushAccumulator acc;
			for(unsigned int x=i->second.x; x!=x_bound; ++x)
			{
				for(unsigned int y=i->second.y; y!=y_bound; ++y)
				{
					if(ai.map->isClearAreaLocal(x,y))
					{
						acc.applyBrush(ai.map, BrushApplication(x,y,0));
					}
				}
				ai.orders.push(new OrderAlterateClearArea(ai.team->teamNumber, BrushTool::MODE_DEL, &acc));
				cleared_buildings.erase(i);
				continue;
			}
		}
		++i;
	}
	return false;
}




bool BuildingClearer::updateClearingAreas()
{
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(	b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG &&
				b->type->shortTypeNum!=IntBuildingType::WAR_FLAG &&
				b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG &&
				 cleared_buildings.find(b->gid) == cleared_buildings.end())
			{
				clearingRecord cr;
				cr.x=b->posX-CLEARING_AREA_BUILDING_PADDING+1;
				cr.y=b->posY-CLEARING_AREA_BUILDING_PADDING+1;
				cr.width=b->type->width+CLEARING_AREA_BUILDING_PADDING*2-2;
				cr.height=b->type->height+CLEARING_AREA_BUILDING_PADDING*2-2;
				cr.level=b->type->level;

				unsigned int x_bound=cr.x+cr.width;
				unsigned int y_bound=cr.y+cr.height;
				if(static_cast<int>(x_bound) > ai.map->getW())
					x_bound-=ai.map->getW();
				if(static_cast<int>(y_bound) > ai.map->getH())
					y_bound-=ai.map->getH();

				BrushAccumulator acc;
				for(unsigned int x=cr.x; x!=x_bound; ++x)
				{
					for(unsigned int y=cr.y; y!=y_bound; ++y)
					{
						if(!ai.map->isClearAreaLocal(x,y))
						{
							acc.applyBrush(ai.map, BrushApplication(x,y,6));
						}
					}
				}
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateClearingAreas: Adding clearing area around the building at "<<b->posX<<","<<b->posY<<"."<<endl;
				ai.orders.push(new OrderAlterateClearArea(ai.team->teamNumber, BrushTool::MODE_ADD, &acc));
				cleared_buildings[b->gid]=cr;
			}
		}
	}
	return false;
}
