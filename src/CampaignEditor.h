// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "Glob2Screen.h"
#include "Campaign.h"
#include "GUIText.h"
#include "GUIButton.h"
#include "GUIList.h"
#include "GUITextInput.h"
#include "GUITextArea.h"
#include "GUICheckList.h"

class CampaignEditor : public Glob2Screen
{
public:
	CampaignEditor(const std::string& name);
	void onAction(Widget *source, Action action, int par1, int par2);
	enum
	{
		ADDMAP,
		EDITMAP,
		REMOVEMAP,
		OK,
		CANCEL,
	};
private:
	Campaign campaign;
	/// Title of the screen, depends on the directory given in parameter
	Text *title;
	/// The ok button
	Button *ok;
	/// The cancel button
	Button *cancel;
	/// List of maps in the campaign
	List* mapList;
	/// Opens the map selection screen, and then the editor to add a new map
	Button* addMap;
	/// Opens the map editor screen and edits the selected map
	Button* editMap;
	/// Remvoes the map from the list of maps
	Button* removeMap;
	/// Text editor changes the name of the campaign
	TextInput* nameEditor;
	/// Text editor for description
	TextArea* description;

	///Adds all of the maps in the campaign to the mapList
	void syncMapList();
};


class CampaignMapEntryEditor : public Glob2Screen
{
public:
	CampaignMapEntryEditor(Campaign& campaign, CampaignMapEntry& mapEntry);
	void onAction(Widget *source, Action action, int par1, int par2);
	enum
	{
		OK,
		CANCEL,
		ISUNLOCKED,
	};
private:
	CampaignMapEntry& entry;
	Campaign& campaign;
	/// Title of the screen, depends on the directory given in parameter
	Text *title;
	/// The ok button
	Button *ok;
	/// The cancel button
	Button *cancel;
	/// List of maps that unlock the map thats being edited
	CheckList* mapsUnlockedBy;
	/// The label for mapsUnlockedBy
	Text *mapsUnlockedByLabel;
	/// Text editor changes the name of the map in the campaign
	TextInput* nameEditor;
	/// The label for nameEditor
	Text *nameEditorLabel;
	/// The text editor for the description
	TextArea *descriptionEditor;
	/// The label for the descriptionEditor
	Text *descriptionEditorLabel;
	/// The button that says whether this entry is unlocked by default
	OnOffButton* isUnlocked;
	/// The label for isUnlocked
	Text *isUnlockedLabel;
};

