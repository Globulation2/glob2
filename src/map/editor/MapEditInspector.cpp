// SPDX-License-Identifier: GPL-3.0-or-later
// Shared unit/building inspector rows; see MapEditInspector.h.

#include "MapEditInspector.h"
#include "BuildingPresentation.h"
#include "BuildingType.h"
#include "EditorDockInternal.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "UnitDisplayNames.h"
#include "UnitType.h"
#include "render/UnitAnimation.h"
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

namespace fe = Glob2UI;

InspectorModel buildInspectorModel(MapEdit &editor)
{
	InspectorModel model;
    if (!editor.view.scene) return model;
    const auto& frame = *editor.view.scene;
    model.world = frame.world;
	if (editor.panelMode == MapEdit::BuildingEditor && editor.selectedBuildingGID != NOGBID)
	{
		const auto *building = frame.entities.building(editor.selectedBuildingGID);
		if (!building)
			return model;
		model.kind = InspectorModel::Kind::Building;
		model.identity = editor.selectedBuildingGID;
		model.building = building;
		model.title = buildingDisplayName(*frame.entities.type(*building));
		model.detail = GAGCore::FormattableString(fe::tr("[Team %0 / Level %1]"))
						   .arg(building->team + 1)
						   .arg(frame.entities.type(*building)->level + 1);
		// The rows the selection set up (MapEdit::addBuildingEditRow), in order.
		struct Named
		{
			ValueScrollBox *box;
			const char *key, *caption;
		};
		const Named named[] = {{editor.buildingHPScrollBox, "hp", "[hp]"},
							   {editor.buildingAssignedScrollBox, "assigned", "[assigned]"},
							   {editor.buildingWorkerRatioScrollBox, "worker-ratio", "[Worker Ratio]"},
							   {editor.buildingExplorerRatioScrollBox, "explorer-ratio", "[Explorer Ratio]"},
							   {editor.buildingWarriorRatioScrollBox, "warrior-ratio", "[Warrior Ratio]"},
							   {editor.buildingBulletsScrollBox, "bullets", "[Bullets]"},
							   {editor.buildingMinimumLevelScrollBox, "minimum-level", "[Minimum Level To Flag]"},
							   {editor.buildingWorkerLevelScrollBox, "worker-level", "[Worker]"},
							   {editor.buildingBombingScrollBox, "ground-attack", "[ground attack]"},
							   {editor.buildingRadiusScrollBox, "range", "[range]"}};
		for (const auto &[label, box] : editor.buildingEditRows)
		{
			(void)label;
			bool found = false;
			for (const auto &entry : named)
				if (entry.box == box)
				{
					model.rows.push_back({entry.key, fe::tr(entry.caption), box});
					found = true;
					break;
				}
			if (found)
				continue;
			for (int material = 0; material < MaterialCount; ++material)
				if (editor.buildingResourceControls[material] == box)
					model.rows.push_back({"material/" + std::to_string(material), getMaterialName(material), box});
		}
		return model;
	}
	if (editor.panelMode == MapEdit::UnitEditor && editor.selectedUnitGID != NOGUID)
	{
		const auto *unit = frame.entities.unit(editor.selectedUnitGID);
		if (!unit)
			return model;
		model.kind = InspectorModel::Kind::Unit;
		model.identity = 65536 + editor.selectedUnitGID;
		model.unit = unit;
		model.title = getUnitName(unit->typeNum);
		model.detail = GAGCore::FormattableString(fe::tr("[Team %0]")).arg(unit->team + 1);
		model.rows.push_back({"hp", fe::tr("[hp]"), editor.unitHPScrollBox});
		struct Skill
		{
			int ability;
			ValueScrollBox *box;
			const char *key, *caption;
		};
		const Skill skills[] = {{WALK, editor.unitWalkLevelScrollBox, "walk", "[Walk]"},
								{SWIM, editor.unitSwimLevelScrollBox, "swim", "[Swim]"},
								{BUILD, editor.unitBuildLevelScrollBox, "build", "[Build]"},
								{ATTACK_SPEED, editor.unitAttackSpeedLevelScrollBox, "attack-speed", "[At. speed]"},
								{ATTACK_STRENGTH, editor.unitAttackStrengthLevelScrollBox, "attack-strength", "[At. strength]"},
								{MAGIC_ATTACK_GROUND, editor.unitMagicGroundAttackLevelScrollBox, "magic-ground", "[Magic At. Ground]"}};
		for (const auto &skill : skills)
			if (unit->canLearn[skill.ability])
				model.rows.push_back({skill.key, fe::tr(skill.caption), skill.box});
	}
	return model;
}

fe::Element inspectorRows(const InspectorModel &model, const fe::Presentation &p, const std::string &prefix,
						  std::function<void()> changed)
{
	std::vector<fe::Element> rows;
	for (const auto &row : model.rows)
	{
		ValueScrollBox *box = row.value;
		const int maximum = std::max(0, box->maximumValue());
		fe::StepperOptions options;
		options.enabled = maximum > 0;
		options.valueText = std::to_string(box->currentValue()) + " / " + std::to_string(maximum);
		auto stepper = fe::stepper(prefix + "/" + row.key, std::clamp(box->currentValue(), 0, maximum), 0, maximum,
								   [box, changed](int value)
								   {
									   box->setValue(value);
									   if (changed)
										   changed();
								   }, options);
		rows.push_back(fe::field(row.caption, stepper, {.controlWidth = 150}));
	}
	return fe::column(std::move(rows), {p.pt(6)});
}

void paintInspectorPicture(fe::Canvas &canvas, fe::Rect bounds, const InspectorModel &model)
{
	if (model.building)
	{
		const auto *type = &(*model.world.catalogs->typeDefinitions)[model.building->typeNum];
		auto *sprite = type->miniSpriteImage >= 0 ? type->miniSpritePtr : type->gameSpritePtr;
		const int frame = type->miniSpriteImage >= 0 ? type->miniSpriteImage : type->gameSpriteImage;
		if (!sprite)
			return;
		sprite->setBaseColor(presentationColor(model.world.teams->values[model.building->team].color));
		EditorDockPaint::spriteFit(canvas, bounds, sprite, frame, 1.5);
	}
	else if (model.unit)
	{
		auto *unit = model.unit;
		const auto *type = &model.world.catalogs->unitTypes[unit->typeNum][0];
		const int image = unitAnimationFrame(type->startImage[unit->action], unit->direction, unit->delta);
		auto *sprite = globalContainer->units;
		sprite->setBaseColor(presentationColor(model.world.teams->values[unit->team].color));
		EditorDockPaint::spriteFit(canvas, bounds, sprite, image, 1.5);
	}
}
