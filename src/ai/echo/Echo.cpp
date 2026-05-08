// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingType.h"
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



bool MapInfo::is_ressource(int x, int y)
{
	return echo.player->map->isRessource(x, y);
}



bool MapInfo::is_water(int x, int y)
{
	return echo.player->map->isWater(x, y);
}



bool MapInfo::is_sand(int x, int y)
{
	return echo.player->map->isSand(x, y);
}



bool MapInfo::is_grass(int x, int y)
{
	return echo.player->map->isGrass(x, y);
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
		std::shared_ptr<ManagementOrder> mo=std::shared_ptr<ManagementOrder>(ManagementOrder::load_order(stream, player, versionMinor));
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
		building_orders[buildingIndex]=std::shared_ptr<BuildingOrder>(new BuildingOrder);
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
		std::shared_ptr<RessourceTracker> rt(new RessourceTracker(*this, stream, player, versionMinor));
		bool activated=stream->readUint8("active");
		ressource_trackers[id]=std::make_tuple(rt, activated);
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
	for (std::list<std::shared_ptr<Order> >::iterator i = orders.begin(); i!=orders.end(); ++i)
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
		std::get<0>(i->second)->save(stream);
		stream->writeUint8(std::get<1>(i->second), "active");
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

