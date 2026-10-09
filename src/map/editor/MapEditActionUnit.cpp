// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "MapEdit.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"

void MapEdit::refreshSelectedUnitPerformance(int stat)
{
	Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
	UnitType *ut = u->race->getUnitType(u->typeNum, u->level[stat]);
	u->performance[stat] = ut->performance[stat];
	game.snapshots().invalidateBoundary();
		hasMapBeenModified = true;
}

bool MapEdit::performUnitAction(const std::string& action, float relMouseX, float relMouseY)
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
		game.map.displayToMapCaseAligned(mapMouseX(mouseX), mapMouseY(mouseY), &x, &y, viewportX, viewportY);

		Unit *unit=game.addUnit(x, y, team, type, level, 0, 0, 0);
		if (unit)
		{
			unit->delta = unit->entityRandom.nextU32() & 255;
			if (game.teams[team]->startPosSet<Team::START_POS_FROM_UNIT)
			{
				game.teams[team]->startPosX=viewportX;
				game.teams[team]->startPosY=viewportY;
				game.teams[team]->startPosSet=Team::START_POS_FROM_UNIT;
			}
			game.regenerateDiscoveryMap();
			game.snapshots().invalidateBoundary();
		hasMapBeenModified = true;
		}
	}
	else if(action=="select map unit")
	{
		int x;
		int y;
		int gid=NOGUID;
		game.map.displayToMapCaseAligned(mapMouseX(mouseX), mapMouseY(mouseY), &x, &y, viewportX, viewportY);
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
			unitHPLabel->setValues(&view.selectedUnit->hp, &view.selectedUnit->performance[HP], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->hp) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->performance[HP]) : 0; });
			unitHPScrollBox ->setValues(&view.selectedUnit->hp, &view.selectedUnit->performance[HP], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->hp) : 0; }, [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->performance[HP]) : 0; });
			unitWalkLevelLabel->setValues(&view.selectedUnit->level[WALK], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[WALK]) : 0; });
			unitWalkLevelScrollBox->setValues(&view.selectedUnit->level[WALK], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[WALK]) : 0; });
			unitSwimLevelLabel->setValues(&view.selectedUnit->level[SWIM], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[SWIM]) : 0; });
			unitSwimLevelScrollBox->setValues(&view.selectedUnit->level[SWIM], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[SWIM]) : 0; });
			unitBuildLevelLabel->setValues(&view.selectedUnit->level[BUILD], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[BUILD]) : 0; });
			unitBuildLevelScrollBox->setValues(&view.selectedUnit->level[BUILD], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[BUILD]) : 0; });
			unitAttackSpeedLevelLabel->setValues(&view.selectedUnit->level[ATTACK_SPEED], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[ATTACK_SPEED]) : 0; });
			unitAttackSpeedLevelScrollBox->setValues(&view.selectedUnit->level[ATTACK_SPEED], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[ATTACK_SPEED]) : 0; });
			unitAttackStrengthLevelLabel->setValues(&view.selectedUnit->level[ATTACK_STRENGTH], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[ATTACK_STRENGTH]) : 0; });
			unitAttackStrengthLevelScrollBox->setValues(&view.selectedUnit->level[ATTACK_STRENGTH], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[ATTACK_STRENGTH]) : 0; });
			unitMagicGroundAttackLevelLabel->setValues(&view.selectedUnit->level[MAGIC_ATTACK_GROUND], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[MAGIC_ATTACK_GROUND]) : 0; });
			unitMagicGroundAttackLevelScrollBox->setValues(&view.selectedUnit->level[MAGIC_ATTACK_GROUND], [](const PresentationFrame& frame) { const auto* value=frame.entities.unit(frame.entities.selectedUnit); return value ? Sint32(value->level[MAGIC_ATTACK_GROUND]) : 0; });
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
		refreshSelectedUnitPerformance(WALK);
	}
	else if(action=="update unit swim level")
	{
		refreshSelectedUnitPerformance(SWIM);
	}
	else if(action=="update unit build level")
	{
		// One worker level: the build box drives harvest as well.
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		u->setWorkerLevel(u->level[BUILD]);
		game.snapshots().invalidateBoundary();
		hasMapBeenModified = true;
	}
	else if(action=="update unit attack speed level")
	{
		refreshSelectedUnitPerformance(ATTACK_SPEED);
	}
	else if(action=="update unit attack strength level")
	{
		refreshSelectedUnitPerformance(ATTACK_STRENGTH);
	}
	else if(action=="update unit magic ground attack level")
	{
		refreshSelectedUnitPerformance(MAGIC_ATTACK_GROUND);
	}
	else if(action=="update unit")
	{
		game.snapshots().invalidateBoundary();
		hasMapBeenModified = true;
	}
	else
		return false;
	return true;
}
