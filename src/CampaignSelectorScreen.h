// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "Campaign.h"
#include "Glob2Screen.h"
#include "GUIText.h"
#include "GUIButton.h"
#include "GUIFileList.h"
#include "GUITextArea.h"

#include <string>

class CampaignSelectorScreen : public Glob2Screen
{
public:
	CampaignSelectorScreen(bool isSelectingSave=false);
	void onAction(Widget *source, Action action, int par1, int par2);
	std::string getCampaignName() const;

	enum
	{
		//! Value returned upon screen execution completion when a valid campaign is selected
		OK = 1,
		//! Value returned upon screen execution completion when the campaign selection is canceled
		CANCEL = 2,
	};
private:
	//! Title of the screen, depends on the directory given in parameter
	Text *title;
	//! The ok button
	Button *ok;
	//! The cancel button
	Button *cancel;
	/// The list of campaigns
	FileList *fileList;
	/// The description
	TextArea* description;
	/// Descriptions already read from disk, so highlighting the same file
	/// twice doesn't re-parse the whole campaign file each time
	CampaignDescriptionCache descriptionCache;
};



