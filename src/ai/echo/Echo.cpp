// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <map>
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <tuple>

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using std::shared_ptr;



void AIEcho::signature_write(GAGCore::OutputStream *stream)
{
	stream->write("EchoSig", AI_ECHO_SIGNATURE_LENGTH, "signature");
}



void AIEcho::signature_check(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	char signature[AI_ECHO_SIGNATURE_LENGTH];
	stream->read(signature, AI_ECHO_SIGNATURE_LENGTH, "signature");
	if (memcmp(signature,"EchoSig", AI_ECHO_SIGNATURE_LENGTH)!=0)
	{

		std::cerr<<"Signature match failed. Expected \"EchoSig\", recieved \""<<signature<<"\""<<std::endl;
		assert(false);
	}
}




Echo::Echo(EchoAI* echoai, Player* player) : player(player), echoai(echoai), gm(), br(player, *this), fm(*this), timer(0)
{
	previous_building_id=-1;
	from_load_timer=0;
	is_fruit=false;
}


unsigned int Echo::add_building_order(Construction::BuildingOrder* bo)
{
	building_orders.push_back(std::shared_ptr<Construction::BuildingOrder>(bo));
	bo->queue_gradients(get_gradient_manager());
	unsigned int id=br.register_building();
	bo->id=id;
	return id;
}


void Echo::add_management_order(Management::ManagementOrder* mo)
{
	management_orders.push_back(std::shared_ptr<Management::ManagementOrder>(mo));
}


void Echo::update_management_orders()
{
	for(std::vector<std::shared_ptr<Management::ManagementOrder> >::iterator i=management_orders.begin(); i!=management_orders.end();)
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
	ressource_trackers[building_id]=std::make_tuple(std::shared_ptr<RessourceTracker>(rt), true);
}



std::shared_ptr<Management::RessourceTracker> Echo::get_ressource_tracker(int building_id)
{
	if(ressource_trackers.find(building_id)==ressource_trackers.end())
		return std::shared_ptr<Management::RessourceTracker>();
	return std::get<0>(ressource_trackers[building_id]);
}



void Echo::pause_ressource_tracker(int building_id)
{
	std::get<1>(ressource_trackers[building_id])=false;
}



void Echo::unpause_ressource_tracker(int building_id)
{
	std::get<1>(ressource_trackers[building_id])=true;
}



void Echo::update_ressource_trackers()
{
	for(std::map<int, std::tuple<std::shared_ptr<Management::RessourceTracker>, bool> >::iterator i = ressource_trackers.begin(); i!=ressource_trackers.end();)
	{
		if(!br.is_building_found(i->first) && !br.is_building_pending(i->first))
		{
			std::map<int, std::tuple<std::shared_ptr<Management::RessourceTracker>, bool> >::iterator current=i;
			++i;
			ressource_trackers.erase(current);
			continue;
		}
		else if(br.is_building_found(i->first))
		{
			if(std::get<1>(i->second))
				std::get<0>(i->second)->tick();
		}
		++i;
	}
}



void Echo::update_building_orders()
{
	for(std::vector<std::shared_ptr<Construction::BuildingOrder> >::iterator i=building_orders.begin(); i!=building_orders.end();)
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


std::shared_ptr<Order> Echo::getOrder(void)
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
		std::shared_ptr<Order> order=orders.front();
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
	return std::shared_ptr<Order>(new NullOrder());
}
