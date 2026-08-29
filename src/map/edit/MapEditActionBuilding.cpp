// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "Unit.h"

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
			if(!hpLabel)
			{
				buildingHPLabel->disable();
				buildingHPScrollBox->disable();
			}
			else
			{
				buildingHPLabel->area.y=ypos;
				buildingHPScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!foodLabel)
			{
				buildingFoodQuantityLabel->disable();
				buildingFoodQuantityScrollBox->disable();
			}
			else
			{
				buildingFoodQuantityLabel->area.y=ypos;
				buildingFoodQuantityScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!assignedLabel)
			{
				buildingAssignedLabel->disable();
				buildingAssignedScrollBox->disable();
			}
			else
			{
				buildingAssignedLabel->area.y=ypos;
				buildingAssignedScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!workerRatioLabel)
			{
				buildingWorkerRatioLabel->disable();
				buildingWorkerRatioScrollBox->disable();
			}
			else
			{
				buildingWorkerRatioLabel->area.y=ypos;
				buildingWorkerRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!explorerRatioLabel)
			{
				buildingExplorerRatioLabel->disable();
				buildingExplorerRatioScrollBox->disable();
			}
			else
			{
				buildingExplorerRatioLabel->area.y=ypos;
				buildingExplorerRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!warriorRatioLabel)
			{
				buildingWarriorRatioLabel->disable();
				buildingWarriorRatioScrollBox->disable();
			}
			else
			{
				buildingWarriorRatioLabel->area.y=ypos;
				buildingWarriorRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!cherryLabel)
			{
				buildingCherryLabel->disable();
				buildingCherryScrollBox->disable();
			}
			else
			{
				buildingCherryLabel->area.y=ypos;
				buildingCherryScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!orangeLabel)
			{
				buildingOrangeLabel->disable();
				buildingOrangeScrollBox->disable();
			}
			else
			{
				buildingOrangeLabel->area.y=ypos;
				buildingOrangeScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!pruneLabel)
			{
				buildingPruneLabel->disable();
				buildingPruneScrollBox->disable();
			}
			else
			{
				buildingPruneLabel->area.y=ypos;
				buildingPruneScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!stoneLabel)
			{
				buildingStoneLabel->disable();
				buildingStoneScrollBox->disable();
			}
			else
			{
				buildingStoneLabel->area.y=ypos;
				buildingStoneScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!bulletsLabel)
			{
				buildingBulletsLabel->disable();
				buildingBulletsScrollBox->disable();
			}
			else
			{
				buildingBulletsLabel->area.y=ypos;
				buildingBulletsScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!minimumLevel)
			{
				buildingMinimumLevelLabel->disable();
				buildingMinimumLevelScrollBox->disable();
			}
			else
			{
				buildingMinimumLevelLabel->area.y=ypos;
				buildingMinimumLevelScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!radius)
			{
				buildingRadiusLabel->disable();
				buildingRadiusScrollBox->disable();
			}
			else
			{
				buildingRadiusLabel->area.y=ypos;
				buildingRadiusScrollBox->area.y=ypos+16;
				ypos+=32;
			}
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
