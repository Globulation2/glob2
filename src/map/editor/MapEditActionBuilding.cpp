// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "BuildingType.h"
#include "MapEdit.h"
#include "Unit.h"
#include "GlobalContainer.h"
#include <algorithm>

namespace
{
// Selection-time only: materials can outlive their last natural deposit.
MaterialMask editorMaterialPresence(const Game& game, MaterialMask requested)
{
    if (!requested) return 0;
    MaterialMask present=0;
    for (unsigned m=0;m<MaterialCount;++m)
        if ((requested&(1u<<m)) && game.map.hasMaterialSourceSlot(m)) present|=MaterialMask(1u<<m);
    for (const Team* team:game.teams)
    {
        if (!team || (present&requested)==requested) continue;
        for (unsigned m=0;m<MaterialCount;++m)
            if (team->teamMaterials[m] || team->reservedTeamMaterials[m]) present|=MaterialMask(1u<<m);
        for (int id=0;id<Unit::MAX_COUNT;++id)
            if (const Unit* unit=team->myUnits[id];unit && validMaterial(unit->carriedMaterial))
                present|=MaterialMask(1u<<unit->carriedMaterial);
        for (int id=0;id<Building::MAX_COUNT;++id)
            if (const Building* building=team->myBuildings[id])
                for (unsigned m=0;m<MaterialCount;++m)
                    if (building->materials[m]) present|=MaterialMask(1u<<m);
    }
    return present;
}
}

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
            view.selectedBuilding=b;
			enableOnlyGroup("building editor");
			buildingEditRows.clear(); buildingEditFirstRow=0;
			const auto& spec=b->type->semantics;
			buildingHPLabel->setValues(&b->hp,&b->type->hpMax, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->hp) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->hpMax) : 0; });
			buildingHPScrollBox->setValues(&b->hp,&b->type->hpMax, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->hp) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->hpMax) : 0; });
			buildingAssignedLabel->setValues(&b->maxUnitWorking,&b->type->semantics.assignmentLimit, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->maxUnitWorking) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->semantics.assignmentLimit) : 0; });
			buildingAssignedScrollBox->setValues(&b->maxUnitWorking,&b->type->semantics.assignmentLimit, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->maxUnitWorking) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->semantics.assignmentLimit) : 0; });
			buildingWorkerRatioLabel->setValues(&b->ratio[WORKER], [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->ratio[WORKER]) : 0; }); buildingWorkerRatioScrollBox->setValues(&b->ratio[WORKER], [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->ratio[WORKER]) : 0; });
			buildingExplorerRatioLabel->setValues(&b->ratio[EXPLORER], [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->ratio[EXPLORER]) : 0; }); buildingExplorerRatioScrollBox->setValues(&b->ratio[EXPLORER], [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->ratio[EXPLORER]) : 0; });
			buildingWarriorRatioLabel->setValues(&b->ratio[WARRIOR], [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->ratio[WARRIOR]) : 0; }); buildingWarriorRatioScrollBox->setValues(&b->ratio[WARRIOR], [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->ratio[WARRIOR]) : 0; });
			buildingBulletsLabel->setValues(&b->bullets,&b->type->maxBullets, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->bullets) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->maxBullets) : 0; }); buildingBulletsScrollBox->setValues(&b->bullets,&b->type->maxBullets, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->bullets) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->maxBullets) : 0; });
			buildingMinimumLevelLabel->setValues(&b->minLevelToFlag, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->minLevelToFlag) : 0; }); buildingMinimumLevelScrollBox->setValues(&b->minLevelToFlag, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->minLevelToFlag) : 0; });
			buildingWorkerLevelLabel->setValues(&b->minWorkerLevelToFlag, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->minWorkerLevelToFlag) : 0; }); buildingWorkerLevelScrollBox->setValues(&b->minWorkerLevelToFlag, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->minWorkerLevelToFlag) : 0; });
			buildingBombingRequirement=b->explorersRequireBombing;
            const auto bombing=[](const PresentationFrame& frame) {
                const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref);
                return value ? Sint32(value->explorersRequireBombing) : 0;
            };
            buildingBombingLabel->setValues(&buildingBombingRequirement,bombing);
            buildingBombingScrollBox->setValues(&buildingBombingRequirement,bombing);
			buildingRadiusLabel->setValues(&b->unitStayRange,&b->type->maxUnitStayRange, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->unitStayRange) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->maxUnitStayRange) : 0; }); buildingRadiusScrollBox->setValues(&b->unitStayRange,&b->type->maxUnitStayRange, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(value->unitStayRange) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->maxUnitStayRange) : 0; });
			addBuildingEditRow(buildingHPLabel,buildingHPScrollBox,b->type->hpMax>0);
			addBuildingEditRow(buildingAssignedLabel,buildingAssignedScrollBox,spec.assignmentLimit>0);
			addBuildingEditRow(buildingWorkerRatioLabel,buildingWorkerRatioScrollBox,spec.production.recipes[WORKER].enabled);
			addBuildingEditRow(buildingExplorerRatioLabel,buildingExplorerRatioScrollBox,spec.production.recipes[EXPLORER].enabled);
			addBuildingEditRow(buildingWarriorRatioLabel,buildingWarriorRatioScrollBox,spec.production.recipes[WARRIOR].enabled);
            MaterialMask requested=0;
            for (unsigned material=materialIndex(MaterialId::Gold);material<MaterialCount;++material)
                if (b->type->maxMaterial[material]>0) requested|=MaterialMask(1u<<material);
            const auto present=editorMaterialPresence(game,requested);
			for (int resource=0; resource<MaterialCount; ++resource)
			{
				buildingResourceLabels[resource]->setValues(&b->materials[resource],&b->type->maxMaterial[resource], [resource](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.materials(*value)[resource]) : 0; }, [resource](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->maxMaterial[resource]) : 0; });
				buildingResourceControls[resource]->setValues(&b->materials[resource],&b->type->maxMaterial[resource], [resource](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.materials(*value)[resource]) : 0; }, [resource](const PresentationFrame& frame) { const auto* value=frame.entities.building(frame.entities.selectedBuilding.ref); return value ? Sint32(frame.entities.type(*value)->maxMaterial[resource]) : 0; });
				addBuildingEditRow(buildingResourceLabels[resource],buildingResourceControls[resource],b->type->maxMaterial[resource]>0 &&
					(resource < int(MaterialId::Gold) || (present&(1u<<resource))));
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
		game.snapshots().invalidateBoundary();
		hasMapBeenModified = true;
	}
	else
		return false;
	return true;
}
