// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "MapEdit.h"
#include "UnitEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"

bool MapEdit::performUnitAction(const std::string& action, int relMouseX, int relMouseY)
{
	if(action=="select worker")
	{
		performAction("unselect");
		placingUnit=Worker;
		selectionMode=PlaceUnit;
	}
	else if(action=="select warrior")
	{
		performAction("unselect");
		placingUnit=Warrior;
		selectionMode=PlaceUnit;
	}
	else if(action=="select explorer")
	{
		performAction("unselect");
		placingUnit=Explorer;
		selectionMode=PlaceUnit;
	}
	else if(action=="select unit level 1")
	{
		placingUnitLevel=0;
	}
	else if(action=="select unit level 2")
	{
		placingUnitLevel=1;
	}
	else if(action=="select unit level 3")
	{
		placingUnitLevel=2;
	}
	else if(action=="select unit level 4")
	{
		placingUnitLevel=3;
	}
	else if(action=="place unit")
	{
		int type=0;
		if(placingUnit==Worker)
			type=WORKER;
		else if(placingUnit==Warrior)
			type=WARRIOR;
		else if(placingUnit==Explorer)
			type=EXPLORER;
		int level=placingUnitLevel;

		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);

		Unit *unit=game.addUnit(x, y, team, type, level, rand()%256, 0, 0);
		if (unit)
		{
			if (game.teams[team]->startPosSet<1)
			{
				game.teams[team]->startPosX=viewportX;
				game.teams[team]->startPosY=viewportY;
				game.teams[team]->startPosSet=1;
			}
			game.regenerateDiscoveryMap();
			hasMapBeenModified = true;
		}
	}
	else if(action=="select map unit")
	{
		int x;
		int y;
		int gid=NOGUID;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
		if(game.map.getAirUnit(x, y)!=NOGUID)
		{
			gid=game.map.getAirUnit(x, y);
		}
		else if(game.map.getGroundUnit(x, y)!=NOGUID)
		{
			gid=game.map.getGroundUnit(x, y);
		}
		if(gid!=NOGUID)
		{
			performAction("unselect");
			selectedUnitGID=gid;
			view.selectedUnit=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
			selectionMode=EditingUnit;
			panelMode=UnitEditor;
			unitInfoTitle->setUnit(view.selectedUnit);
			unitPicture->setUnit(view.selectedUnit);
			unitHPLabel->setValues(&view.selectedUnit->hp, &view.selectedUnit->performance[HP]);
			unitHPScrollBox ->setValues(&view.selectedUnit->hp, &view.selectedUnit->performance[HP]);
			unitWalkLevelLabel->setValues(&view.selectedUnit->level[WALK]);
			unitWalkLevelScrollBox->setValues(&view.selectedUnit->level[WALK]);
			unitSwimLevelLabel->setValues(&view.selectedUnit->level[SWIM]);
			unitSwimLevelScrollBox->setValues(&view.selectedUnit->level[SWIM]);
			unitHarvestLevelLabel->setValues(&view.selectedUnit->level[HARVEST]);
			unitHarvestLevelScrollBox->setValues(&view.selectedUnit->level[HARVEST]);
			unitBuildLevelLabel->setValues(&view.selectedUnit->level[BUILD]);
			unitBuildLevelScrollBox->setValues(&view.selectedUnit->level[BUILD]);
			unitAttackSpeedLevelLabel->setValues(&view.selectedUnit->level[ATTACK_SPEED]);
			unitAttackSpeedLevelScrollBox->setValues(&view.selectedUnit->level[ATTACK_SPEED]);
			unitAttackStrengthLevelLabel->setValues(&view.selectedUnit->level[ATTACK_STRENGTH]);
			unitAttackStrengthLevelScrollBox->setValues(&view.selectedUnit->level[ATTACK_STRENGTH]);
			unitMagicGroundAttackLevelLabel->setValues(&view.selectedUnit->level[MAGIC_ATTACK_GROUND]);
			unitMagicGroundAttackLevelScrollBox->setValues(&view.selectedUnit->level[MAGIC_ATTACK_GROUND]);
			enableOnlyGroup("unit editor");
			if(!view.selectedUnit->canLearn[WALK])
			{
				unitWalkLevelLabel->disable();
				unitWalkLevelScrollBox->disable();
			}
			if(!view.selectedUnit->canLearn[SWIM])
			{
				unitSwimLevelLabel->disable();
				unitSwimLevelScrollBox->disable();
			}
			if(!view.selectedUnit->canLearn[HARVEST])
			{
				unitHarvestLevelLabel->disable();
				unitHarvestLevelScrollBox->disable();
			}
			if(!view.selectedUnit->canLearn[BUILD])
			{
				unitBuildLevelLabel->disable();
				unitBuildLevelScrollBox->disable();
			}
			if(!view.selectedUnit->canLearn[ATTACK_SPEED])
			{
				unitAttackSpeedLevelLabel->disable();
				unitAttackSpeedLevelScrollBox->disable();
			}
			if(!view.selectedUnit->canLearn[ATTACK_STRENGTH])
			{
				unitAttackStrengthLevelLabel->disable();
				unitAttackStrengthLevelScrollBox->disable();
			}
			if(!view.selectedUnit->canLearn[MAGIC_ATTACK_GROUND])
			{
				unitMagicGroundAttackLevelLabel->disable();
				unitMagicGroundAttackLevelScrollBox->disable();
			}
		}
	}
	else if(action=="update unit walk level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[WALK]);
		u->performance[WALK] = ut->performance[WALK];
		hasMapBeenModified = true;
	}
	else if(action=="update unit swim level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[SWIM]);
		u->performance[SWIM] = ut->performance[SWIM];
		hasMapBeenModified = true;
	}
	else if(action=="update unit harvest level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[HARVEST]);
		u->performance[HARVEST] = ut->performance[HARVEST];
		hasMapBeenModified = true;
	}
	else if(action=="update unit build level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[BUILD]);
		u->performance[BUILD] = ut->performance[BUILD];
		hasMapBeenModified = true;
	}
	else if(action=="update unit attack speed level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[ATTACK_SPEED]);
		u->performance[ATTACK_SPEED] = ut->performance[ATTACK_SPEED];
		hasMapBeenModified = true;
	}
	else if(action=="update unit attack strength level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[ATTACK_STRENGTH]);
		u->performance[ATTACK_STRENGTH] = ut->performance[ATTACK_STRENGTH];
		hasMapBeenModified = true;
	}
	else if(action=="update unit magic ground attack level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[MAGIC_ATTACK_GROUND]);
		u->performance[MAGIC_ATTACK_GROUND] = ut->performance[MAGIC_ATTACK_GROUND];
		hasMapBeenModified = true;
	}
	else if(action=="update unit")
	{
		hasMapBeenModified = true;
	}
	else
		return false;
	return true;
}
