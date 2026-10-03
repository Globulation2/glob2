// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault
#pragma once
#include "Campaign.h"
#include "ui/FrontendUI.h"
#include <string>
#include <vector>

class CampaignSelectorScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "campaign_selector"; }
	explicit CampaignSelectorScreen(bool isSelectingSave = false);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	std::string getCampaignName() const;

	enum
	{
		//! A valid campaign is selected
		OK = 1,
		//! The selection was cancelled
		CANCEL = 2,
	};

  protected:
	void onEscape() override { endExecute(CANCEL); }

  private:
	std::string directory;
	std::vector<std::string> names;
	int selected = -1;
	std::string description;
	/// Descriptions already read from disk, so highlighting the same file
	/// twice doesn't re-parse the whole campaign file each time
	CampaignDescriptionCache descriptionCache;
	void select(int index);
};
