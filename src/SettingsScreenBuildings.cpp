// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// All construction and event handling for the "Building Defaults" tab of the
// settings screen. Split out of SettingsScreen.cpp to keep each file under
// 500 lines and to isolate the four near-identical sub-group loops where the
// differences (which type filter, which level, which row-wrap threshold) are
// the easy place for transcription bugs to slip in.

#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <GUIText.h>
#include <GUIButton.h>
#include <GUINumber.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <string>
#include "FormatableString.h"

namespace
{
	// Per-group column-wrap thresholds: number of rows before the layout starts a
	// new column. Tuned so each group fits within its available vertical space.
	constexpr int kRowsCompleted = 4;
	constexpr int kRowsNewConstruction = 6;
	constexpr int kRowsUpgrades = 7;
	constexpr int kRowsFlags = 8;
}


void SettingsScreen::addNumbersFor(int low, int high, Number* widget)
{
	for(int i=low; i<=high; ++i)
	{
		widget->add(i);
	}
}


void SettingsScreen::buildBuildingDefaultsTab()
{
	// Initialise the per-building unit-ratio widget grid to null. Each cell is
	// populated lazily by addDefaultUnitAssignmentWidget() in the four sub-group
	// builds below, but the cells that never apply (flag types in non-flag groups,
	// missing building levels) must stay null so activateDefaultAssignedGroupNumber()
	// and flushDefaultsToSettings() can null-check them.
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		for(int l=0; l<6; ++l)
		{
			unitRatios[t][l]=NULL;
			unitRatioTexts[t][l]=NULL;
		}
	}

	buildCompletedBuildingsGroup();
	buildNewConstructionGroup();
	buildUpgradesGroup();
	buildFlagsGroup();

	buildings = new TextButton( 10, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Building Defaults]"), BUILDINGSETTINGS);
	constructionsites = new TextButton( 140, 60, 220, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Construction Site Defaults]"), CONSTRUCTIONSITES);
	upgrades = new TextButton( 370, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Upgrade Defaults]"), UPGRADES);
	flags = new TextButton( 500, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Flag Defaults]"), FLAGSETTINGS);

	unitSettingsExplanation = new Text( 10, 80, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[unit settings explanation]"));

	addWidgetToGroup(unitSettingsExplanation, unitGroup);
	addWidgetToGroup(buildings, unitGroup);
	addWidgetToGroup(constructionsites, unitGroup);
	addWidgetToGroup(upgrades, unitGroup);
	addWidgetToGroup(flags, unitGroup);
	addWidgetToGroup(flagSettingsExplanation, unitGroup);
}


void SettingsScreen::buildCompletedBuildingsGroup()
{
	// Group 1 — already-built buildings that accept units (foodable or fillable).
	// Iterates all non-flag building types and the three completed levels (1, 3, 5).
	int group_row=0;
	int group_current_column_x=20;
	int group_widest_element=0;

	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		if(t==IntBuildingType::EXPLORATION_FLAG || t==IntBuildingType::WAR_FLAG || t==IntBuildingType::CLEARING_FLAG)
			continue;
		std::string name=IntBuildingType::typeFromShortNumber(t);
		for(int l=0; l<3; ++l)
		{
			BuildingType* type = globalContainer->buildingsTypes.getByType(name, l, false);
			if(type != NULL && (type->foodable || type->fillable))
			{
				int size = addDefaultUnitAssignmentWidget(t, l*2+1, group_current_column_x, 100 + 40*group_row, kBuildingGroupCompleted);
				group_widest_element = std::max(group_widest_element, size);

				group_row += 1;
				if(group_row == kRowsCompleted)
				{
					group_row = 0;
					group_current_column_x += group_widest_element + 10;
					group_widest_element = 0;
				}
			}
		}
	}
}


void SettingsScreen::buildNewConstructionGroup()
{
	// Group 2 — new construction sites (level 0, before any building has been raised).
	// Iterates all building types whose level-0 (under-construction) variant exists.
	int group_row=0;
	int group_current_column_x=20;
	int group_widest_element=0;

	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		std::string name=IntBuildingType::typeFromShortNumber(t);
		//Even numbers represent under-construction, whereas odd numbers represent completed buildings
		if(globalContainer->buildingsTypes.getByType(name, 0, true) != NULL)
		{
			int size = addDefaultUnitAssignmentWidget(t, 0, group_current_column_x, 100 + 40*group_row, kBuildingGroupNewConstruction);
			group_widest_element = std::max(group_widest_element, size);

			group_row += 1;
			if(group_row == kRowsNewConstruction)
			{
				group_row = 0;
				group_current_column_x += group_widest_element + 10;
				group_widest_element = 0;
			}
		}
	}
}


void SettingsScreen::buildUpgradesGroup()
{
	// Group 3 — upgrade construction sites (under-construction levels 2 and 4).
	// Outer loop walks levels {1, 2} and the slot index l*2 yields {2, 4} — the
	// under-construction-upgrade variants.
	int group_row=0;
	int group_current_column_x=20;
	int group_widest_element=0;

	for(int l=1; l<3; ++l)
	{
		for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
		{
			std::string name=IntBuildingType::typeFromShortNumber(t);
			//Even numbers represent under-construction, whereas odd numbers represent completed buildings
			if(globalContainer->buildingsTypes.getByType(name, l, true) != NULL)
			{
				int size = addDefaultUnitAssignmentWidget(t, l*2, group_current_column_x, 100 + 40*group_row, kBuildingGroupUpgrades);
				group_widest_element = std::max(group_widest_element, size);

				group_row += 1;
				if(group_row == kRowsUpgrades)
				{
					group_row = 0;
					group_current_column_x += group_widest_element + 10;
					group_widest_element = 0;
				}
			}
		}
	}
}


void SettingsScreen::buildFlagsGroup()
{
	// Group 4 — flags. Builds two stacked widget rows: per-flag default unit count
	// (top), and per-flag default radius (bottom). The explanation text sits between
	// them at the row-offset measured from the original layout (3 rows down from
	// the first row's y origin).
	int group_row=0;
	int group_current_column_x=20;
	int group_widest_element=0;

	for(int t=IntBuildingType::EXPLORATION_FLAG; t<=IntBuildingType::CLEARING_FLAG; ++t)
	{
		int size = addDefaultUnitAssignmentWidget(t, 1, group_current_column_x, 100 + 40*group_row, kBuildingGroupFlags, true);
		group_widest_element = std::max(group_widest_element, size);

		group_row += 1;
		if(group_row == kRowsFlags)
		{
			group_row = 0;
			group_current_column_x += group_widest_element + 10;
			group_widest_element = 0;
		}
	}
	flagSettingsExplanation = new Text( 10, 100+40*3+10, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[flag settings explanation]"));

	for(int t=IntBuildingType::EXPLORATION_FLAG; t<=IntBuildingType::CLEARING_FLAG; ++t)
	{
		int size = addDefaultFlagRadiusWidget(t, group_current_column_x, 130 + 40*group_row, kBuildingGroupFlags);
		group_widest_element = std::max(group_widest_element, size);

		group_row += 1;
		if(group_row == kRowsFlags)
		{
			group_row = 0;
			group_current_column_x += group_widest_element + 10;
			group_widest_element = 0;
		}
	}
}


// Creates a single (label, number-spinner) widget pair for one (building type, level)
// slot. The `level` parameter encodes the slot inside the per-type 6-wide array using
// the parity rule: even = under-construction site for level/2+1, odd = completed
// building at level (level+1)/2. The `flag` flag forces the label to be the bare
// building name (used for flags, which have no construction-level concept). Widgets
// start hidden; activateDefaultAssignedGroupNumber(group) reveals them when its tab
// is active. Returns the wider of the two widgets so the caller can advance its
// column-x cursor.
int SettingsScreen::addDefaultUnitAssignmentWidget(int type, int level, int x, int y, int group, bool flag)
{
	unitRatios[type][level] = new Number(x, y+20, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	addNumbersFor(0, 20, unitRatios[type][level]);
	unitRatios[type][level]->setNth(globalContainer->settings.defaultUnitsAssigned[type][level]);
	unitRatios[type][level]->visible=false;
	addWidgetToGroup(unitRatios[type][level], unitGroup);

	std::string text=getDefaultUnitAssignmentText(type, level, flag);
	unitRatioTexts[type][level]=new Text(x, y, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", text);

	addWidgetToGroup(unitRatioTexts[type][level], unitGroup);
	unitRatioTexts[type][level]->visible=false;
	unitRatioGroupNumbers[type][level] = group;

	return std::max(unitRatioTexts[type][level]->getWidth(), unitRatios[type][level]->getWidth());
}



// Creates a (label, radius-spinner) pair for one flag type. Indexed by
// (type - EXPLORATION_FLAG) into the flagRadii/flagRadiusTexts arrays. The spinner
// stores radius-1 internally (range 1..20 maps to internal nth 0..19) so the
// settings round-trip preserves user choice with a default of 0 meaning "no
// override". Returns the wider widget for column advancement.
int SettingsScreen::addDefaultFlagRadiusWidget(int type, int x, int y, int group)
{
	int n = type - IntBuildingType::EXPLORATION_FLAG;
	flagRadii[n] = new Number(x, y+20, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	addNumbersFor(1, 20, flagRadii[n]);
	flagRadii[n]->setNth(std::max(0, globalContainer->settings.defaultFlagRadius[n]-1));
	flagRadii[n]->visible=false;
	addWidgetToGroup(flagRadii[n], unitGroup);

	std::string text=getDefaultUnitAssignmentText(type, 1, true);
	flagRadiusTexts[n]=new Text(x, y, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", text);

	addWidgetToGroup(flagRadiusTexts[n], unitGroup);
	flagRadiusTexts[n]->visible=false;
	flagRadiusGroupNumbers[n] = group;

	return std::max(flagRadiusTexts[n]->getWidth(), flagRadii[n]->getWidth());
}



std::string SettingsScreen::getDefaultUnitAssignmentText(int type, int level, bool flag)
{
	std::string name="[" + IntBuildingType::typeFromShortNumber(type) + "]";
	std::string tname = Toolkit::getStringTable()->getString(name.c_str());

	std::string value;
	if(flag)
	{
		value = tname;
	}
	else if(level%2 == 0)
	{
		// Even level = under-construction site; label as "build <name> level N".
		value = FormatableString(Toolkit::getStringTable()->getString("[build %0 level %1]")).arg(tname).arg(level/2 + 1);
	}
	else if(level == 1 && globalContainer->buildingsTypes.getByType(IntBuildingType::typeFromShortNumber(type), level+1, false) == NULL)
	{
		// Single-level building (no level-2 variant exists): omit the level suffix.
		value = tname;
	}
	else
	{
		// Odd level >= 1: completed building; label as "<name> level N".
		value = FormatableString(Toolkit::getStringTable()->getString("[%0 level %1]")).arg(tname).arg(level/2+1);
	}
	return value;
}



void SettingsScreen::setLanguageTextsForDefaultAssignmentWidgets()
{
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		bool flag = false;
		if(t == IntBuildingType::EXPLORATION_FLAG || t == IntBuildingType::WAR_FLAG || t == IntBuildingType::CLEARING_FLAG)
			flag = true;
		for(int l=0; l<6; ++l)
		{
			if(unitRatioTexts[t][l])
			{
				unitRatioTexts[t][l]->setText(getDefaultUnitAssignmentText(t, l, flag));
			}
		}
	}
	for(int t=0; t<3; ++t)
	{
		if(flagRadiusTexts[t])
		{
			flagRadiusTexts[t]->setText(getDefaultUnitAssignmentText(t+IntBuildingType::EXPLORATION_FLAG, 1, true));
		}
	}
}



void SettingsScreen::activateDefaultAssignedGroupNumber(int group)
{
	for(int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		for(int j=0; j<6; ++j)
		{
			if(unitRatioGroupNumbers[i][j] == group)
			{
				if(unitRatios[i][j])
					unitRatios[i][j]->visible=true;
				if(unitRatioTexts[i][j])
					unitRatioTexts[i][j]->visible=true;
			}
			else
			{
				if(unitRatios[i][j])
					unitRatios[i][j]->visible=false;
				if(unitRatioTexts[i][j])
					unitRatioTexts[i][j]->visible=false;
			}
		}
	}
	for(int i=0; i<3; ++i)
	{

		if(flagRadiusGroupNumbers[i] == group)
		{
			if(flagRadii[i])
				flagRadii[i]->visible=true;
			if(flagRadiusTexts[i])
				flagRadiusTexts[i]->visible=true;
		}
		else
		{
			if(flagRadii[i])
				flagRadii[i]->visible=false;
			if(flagRadiusTexts[i])
				flagRadiusTexts[i]->visible=false;
		}
	}
	if(group == kBuildingGroupFlags)
		flagSettingsExplanation->visible=true;
	else
		flagSettingsExplanation->visible=false;
}


void SettingsScreen::flushDefaultsToSettings()
{
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		for(int l=0; l<6; ++l)
		{
			if(unitRatios[t][l])
			{
				if(unitRatios[t][l]->getNth() == 0)
					unitRatios[t][l]->setNth(1);
				globalContainer->settings.defaultUnitsAssigned[t][l]=unitRatios[t][l]->getNth();
			}
		}
	}
	for(int t=0; t<3; ++t)
	{
		if(flagRadii[t])
		{
			globalContainer->settings.defaultFlagRadius[t] = flagRadii[t]->getNth()+1;
		}
	}
}
