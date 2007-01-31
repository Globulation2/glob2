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

#include "AINicowar.h"
#include "GlobalContainer.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;

NewNicowar::NewNicowar()
{
	timer=0;
}


bool NewNicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("NewNicowar");
	timer=stream->readUint32("timer");
	stream->readLeaveSection();
	return true;
}


void NewNicowar::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("NewNicowar");
	stream->writeUint32(timer, "timer");
	stream->writeLeaveSection();
}


void NewNicowar::tick(Echo& echo)
{
	timer++;
	int totalUnit=echo.player->team->stats.getLatestStat()->totalUnit;
	int totalWorker=echo.player->team->stats.getLatestStat()->numberUnitPerType[WORKER];
	int totalWarrior=echo.player->team->stats.getLatestStat()->numberUnitPerType[WARRIOR];
	int totalEXPLORER=echo.player->team->stats.getLatestStat()->numberUnitPerType[EXPLORER];

	if(timer%200==0)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		int totalScore=0;
		for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
		{
			BuildingType* type=globalContainer->buildingsTypes.getByType("inn", echo.get_building_register().get_building_type(*i)->level, false);
			totalScore+=type->maxUnitInside*100 / type->timeToFeedUnit;
		}
		if(totalUnit/8 >= totalScore/50)
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

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);

//				std::cout<<"inn ordered, id="<<id<<std::endl;
		
			ManagementOrder* mo_completion=new AssignWorkers(1, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);

			ManagementOrder* mo_tracker=new AddRessourceTracker(12, id, CORN);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_tracker);
		}
	}
}


void NewNicowar::handle_message(Echo& echo, const std::string& message)
{

}

