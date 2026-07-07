// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "Campaign.h"
#include "Glob2Screen.h"
#include "GUIButton.h"
#include "GUICheckList.h"
#include "GUIText.h"
#include "GUITextInput.h"
#include "GUITextArea.h"

class MapPreview;

///This is the main campaign screen
class CampaignMenuScreen : public Glob2Screen
{
public:
	CampaignMenuScreen(const std::string& name);
	void onAction(Widget *source, Action action, int par1, int par2);
	void setNewCampaign();
	enum
	{
		EXIT,
		START,
	};
private:
	Campaign campaign;

	/// Title of the screen
	Text* title;
	/// The exit to menuscreen button
	Button* exit;
	/// The "start mission" buttion
	Button* startMission;

	/// The box where the players name is put
	TextInput* playerName;

	/// The list of missions that are currently unlocked
	CheckList* availableMissions;
	
	/// Map description
	TextArea* description;
	
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;

	//! Rebuild the displayed mission list from the current campaign state.
	void repopulateAvailableMissions();

	//! The campaign entry for the currently selected list row, or nullptr when
	//! nothing is selected (or the selected name no longer maps to an unlocked map).
	CampaignMapEntry* getSelectedMission();
};


