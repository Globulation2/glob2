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


bool ManagementOrder::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	stream->readEnterSection("conditions");
	Uint32 size = stream->readUint32("size");
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
	conditions.push_back(std::shared_ptr<Condition>(condition));
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

