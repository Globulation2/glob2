// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "Unit.h"

void MapEdit::placeOrHideRow(FractionValueText* label, ValueScrollBox* scrollBox, bool shown, int& ypos)
{
	if(!shown)
	{
		label->disable();
		scrollBox->disable();
	}
	else
	{
		label->area.y=ypos;
		scrollBox->area.y=ypos+16;
		ypos+=32;
	}
}

bool MapEdit::performBuildingAction(const std::string& action, int relMouseX, int relMouseY)
{
	if(action=="select map building")
	{
		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
		int gid=NOGBID;
		for(int t=0; t<Team::MAX_COUNT; ++t)
		{
			if(game.teams[t] && gid==NOGBID)
			{
				for (std::list<Building *>::iterator virtualIt=game.teams[t]->virtualBuildings.begin();
						virtualIt!=game.teams[t]->virtualBuildings.end(); ++virtualIt)
				{
					{
						Building *b=*virtualIt;
						if ((b->posX==x) && (b->posY==y))
						{
							gid=b->gid;
							break;
						}
					}
				}
			}
		}
		if(gid==NOGBID && game.map.getBuilding(x, y)!=NOGUID)
		{
			gid=game.map.getBuilding(x, y);
		}
		if(gid!=NOGBID)
		{
			performAction("unselect");
			Building* b=game.teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
			selectionMode=EditingBuilding;
			panelMode=BuildingEditor;
			selectedBuildingGID=gid;
			enableOnlyGroup("building editor");
			buildingInfoTitle->setBuilding(b);
			buildingPicture->setBuilding(b);
			bool hpLabel=false;
			buildingHPLabel->setValues(&b->hp, &b->type->hpMax);
			buildingHPScrollBox->setValues(&b->hp, &b->type->hpMax);
			bool foodLabel=false;
			buildingFoodQuantityLabel->setValues(&b->ressources[CORN], &b->type->maxRessource[CORN]);
			buildingFoodQuantityScrollBox->setValues(&b->ressources[CORN], &b->type->maxRessource[CORN]);
			bool assignedLabel=false;
			buildingAssignedLabel->setValues(&b->maxUnitWorking);
			buildingAssignedScrollBox->setValues(&b->maxUnitWorking);
			bool workerRatioLabel=false;
			buildingWorkerRatioLabel->setValues(&b->ratio[WORKER]);
			buildingWorkerRatioScrollBox->setValues(&b->ratio[WORKER]);
			bool explorerRatioLabel=false;
			buildingExplorerRatioLabel->setValues(&b->ratio[EXPLORER]);
			buildingExplorerRatioScrollBox->setValues(&b->ratio[EXPLORER]);
			bool warriorRatioLabel=false;
			buildingWarriorRatioLabel->setValues(&b->ratio[WARRIOR]);
			buildingWarriorRatioScrollBox->setValues(&b->ratio[WARRIOR]);
			bool cherryLabel=false;
			buildingCherryLabel->setValues(&b->ressources[CHERRY], &b->type->maxRessource[CHERRY]);
			buildingCherryScrollBox->setValues(&b->ressources[CHERRY], &b->type->maxRessource[CHERRY]);
			bool orangeLabel=false;
			buildingOrangeLabel->setValues(&b->ressources[ORANGE], &b->type->maxRessource[ORANGE]);
			buildingOrangeScrollBox->setValues(&b->ressources[ORANGE], &b->type->maxRessource[ORANGE]);
			bool pruneLabel=false;
			buildingPruneLabel->setValues(&b->ressources[PRUNE], &b->type->maxRessource[PRUNE]);
			buildingPruneScrollBox->setValues(&b->ressources[PRUNE], &b->type->maxRessource[PRUNE]);
			bool stoneLabel=false;
			buildingStoneLabel->setValues(&b->ressources[STONE], &b->type->maxRessource[STONE]);
			buildingStoneScrollBox->setValues(&b->ressources[STONE], &b->type->maxRessource[STONE]);
			bool bulletsLabel=false;
			buildingBulletsLabel->setValues(&b->bullets, &b->type->maxBullets);
			buildingBulletsScrollBox->setValues(&b->bullets, &b->type->maxBullets);
			bool minimumLevel=false;
			buildingMinimumLevelLabel->setValues(&b->minLevelToFlag);
			buildingMinimumLevelScrollBox->setValues(&b->minLevelToFlag);
			bool radius=false;
			buildingRadiusLabel->setValues(&b->unitStayRange, &b->type->maxUnitStayRange);
			buildingRadiusScrollBox->setValues(&b->unitStayRange, &b->type->maxUnitStayRange);
			if(b->type->isBuildingSite)
			{
				hpLabel=true;
				assignedLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::SWARM_BUILDING)
			{
				hpLabel=true;
				foodLabel=true;
				assignedLabel=true;
				workerRatioLabel=true;
				explorerRatioLabel=true;
				warriorRatioLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::FOOD_BUILDING)
			{
				hpLabel=true;
				foodLabel=true;
				assignedLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::HEAL_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
			{
				hpLabel=true;
				assignedLabel=true;
				stoneLabel=true;
				bulletsLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				assignedLabel=true;
				radius=true;
			}
			else if(b->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				assignedLabel=true;
				minimumLevel=true;
				radius=true;
			}
			else if(b->shortTypeNum==IntBuildingType::CLEARING_FLAG)
			{
				assignedLabel=true;
				minimumLevel=true;
				radius=true;
			}
			else if(b->shortTypeNum==IntBuildingType::STONE_WALL)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::MARKET_BUILDING)
			{
				hpLabel=true;
				assignedLabel=true;
				cherryLabel=true;
				orangeLabel=true;
				pruneLabel=true;
			}

			int ypos=252;
			placeOrHideRow(buildingHPLabel, buildingHPScrollBox, hpLabel, ypos);
			placeOrHideRow(buildingFoodQuantityLabel, buildingFoodQuantityScrollBox, foodLabel, ypos);
			placeOrHideRow(buildingAssignedLabel, buildingAssignedScrollBox, assignedLabel, ypos);
			placeOrHideRow(buildingWorkerRatioLabel, buildingWorkerRatioScrollBox, workerRatioLabel, ypos);
			placeOrHideRow(buildingExplorerRatioLabel, buildingExplorerRatioScrollBox, explorerRatioLabel, ypos);
			placeOrHideRow(buildingWarriorRatioLabel, buildingWarriorRatioScrollBox, warriorRatioLabel, ypos);
			placeOrHideRow(buildingCherryLabel, buildingCherryScrollBox, cherryLabel, ypos);
			placeOrHideRow(buildingOrangeLabel, buildingOrangeScrollBox, orangeLabel, ypos);
			placeOrHideRow(buildingPruneLabel, buildingPruneScrollBox, pruneLabel, ypos);
			placeOrHideRow(buildingStoneLabel, buildingStoneScrollBox, stoneLabel, ypos);
			placeOrHideRow(buildingBulletsLabel, buildingBulletsScrollBox, bulletsLabel, ypos);
			placeOrHideRow(buildingMinimumLevelLabel, buildingMinimumLevelScrollBox, minimumLevel, ypos);
			placeOrHideRow(buildingRadiusLabel, buildingRadiusScrollBox, radius, ypos);
		}
	}
	else if(action=="update building")
	{
		hasMapBeenModified = true;
	}
	else
		return false;
	return true;
}
