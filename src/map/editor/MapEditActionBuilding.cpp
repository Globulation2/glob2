// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "BuildingType.h"
#include "MapEdit.h"
#include "Unit.h"
#include "GlobalContainer.h"
#include <algorithm>

void MapEdit::addBuildingEditRow(FractionValueText* label, ValueScrollBox* control, bool shown)
{
	label->disable(); control->disable();
	if (shown) buildingEditRows.emplace_back(label,control);
}

void MapEdit::layoutBuildingEditRows()
{
	if (panelMode!=BuildingEditor) return;
	const int visible=std::max(1,(globalContainer->gfx->getH()-252)/32);
	buildingEditFirstRow=std::clamp(buildingEditFirstRow,0,std::max(0,int(buildingEditRows.size())-visible));
	for (size_t i=0; i<buildingEditRows.size(); ++i)
	{
		auto [label,control]=buildingEditRows[i];
		const int row=int(i)-buildingEditFirstRow;
		label->area.y=252+row*32; control->area.y=label->area.y+16;
		label->enabled=control->enabled=usesPhone() || (row>=0 && row<visible);
	}
}

bool MapEdit::performBuildingAction(const std::string& action, float relMouseX, float relMouseY)
{
	if(action=="select map building")
	{
		int x;
		int y;
		game.map.displayToMapCaseAligned(mapMouseX(mouseX), mapMouseY(mouseY), &x, &y, viewportX, viewportY);
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
						if (((x-b->posX)&game.map.getMaskW()) < b->type->width &&
							((y-b->posY)&game.map.getMaskH()) < b->type->height)
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
			buildingEditRows.clear(); buildingEditFirstRow=0;
			const auto& spec=b->type->semantics;
			buildingHPLabel->setValues(&b->hp,&b->type->hpMax);
			buildingHPScrollBox->setValues(&b->hp,&b->type->hpMax);
			buildingAssignedLabel->setValues(&b->maxUnitWorking,&b->type->semantics.assignmentLimit);
			buildingAssignedScrollBox->setValues(&b->maxUnitWorking,&b->type->semantics.assignmentLimit);
			buildingWorkerRatioLabel->setValues(&b->ratio[WORKER]); buildingWorkerRatioScrollBox->setValues(&b->ratio[WORKER]);
			buildingExplorerRatioLabel->setValues(&b->ratio[EXPLORER]); buildingExplorerRatioScrollBox->setValues(&b->ratio[EXPLORER]);
			buildingWarriorRatioLabel->setValues(&b->ratio[WARRIOR]); buildingWarriorRatioScrollBox->setValues(&b->ratio[WARRIOR]);
			buildingBulletsLabel->setValues(&b->bullets,&b->type->maxBullets); buildingBulletsScrollBox->setValues(&b->bullets,&b->type->maxBullets);
			buildingMinimumLevelLabel->setValues(&b->minLevelToFlag); buildingMinimumLevelScrollBox->setValues(&b->minLevelToFlag);
			buildingWorkerLevelLabel->setValues(&b->minWorkerLevelToFlag); buildingWorkerLevelScrollBox->setValues(&b->minWorkerLevelToFlag);
			buildingBombingRequirement=b->explorersRequireBombing;
			buildingRadiusLabel->setValues(&b->unitStayRange,&b->type->maxUnitStayRange); buildingRadiusScrollBox->setValues(&b->unitStayRange,&b->type->maxUnitStayRange);
			addBuildingEditRow(buildingHPLabel,buildingHPScrollBox,b->type->hpMax>0);
			addBuildingEditRow(buildingAssignedLabel,buildingAssignedScrollBox,spec.assignmentLimit>0);
			addBuildingEditRow(buildingWorkerRatioLabel,buildingWorkerRatioScrollBox,spec.production.recipes[WORKER].enabled);
			addBuildingEditRow(buildingExplorerRatioLabel,buildingExplorerRatioScrollBox,spec.production.recipes[EXPLORER].enabled);
			addBuildingEditRow(buildingWarriorRatioLabel,buildingWarriorRatioScrollBox,spec.production.recipes[WARRIOR].enabled);
			for (int resource=0; resource<MAX_RESOURCES; ++resource)
			{
				buildingResourceLabels[resource]->setValues(&b->materials[resource],&b->type->maxMaterial[resource]);
				buildingResourceControls[resource]->setValues(&b->materials[resource],&b->type->maxMaterial[resource]);
				addBuildingEditRow(buildingResourceLabels[resource],buildingResourceControls[resource],b->type->maxMaterial[resource]>0 &&
					(resource < int(MaterialId::Gold) || game.map.hasMaterialSource(resource)));
			}
			addBuildingEditRow(buildingBulletsLabel,buildingBulletsScrollBox,b->type->maxBullets>0);
			addBuildingEditRow(buildingMinimumLevelLabel,buildingMinimumLevelScrollBox,b->type->zonable[WARRIOR]);
			addBuildingEditRow(buildingWorkerLevelLabel,buildingWorkerLevelScrollBox,b->type->zonable[WORKER]);
			addBuildingEditRow(buildingBombingLabel,buildingBombingScrollBox,b->type->zonable[EXPLORER]);
			addBuildingEditRow(buildingRadiusLabel,buildingRadiusScrollBox,b->type->maxUnitStayRange>0);
			layoutBuildingEditRows();
		}
	}
	else if(action=="update building")
	{
		if (selectionMode==EditingBuilding)
			if (auto* b=game.teams[Building::GIDtoTeam(selectedBuildingGID)]->myBuildings[Building::GIDtoID(selectedBuildingGID)])
				b->explorersRequireBombing=buildingBombingRequirement!=0;
		hasMapBeenModified = true;
	}
	else
		return false;
	return true;
}
