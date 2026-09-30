// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault
#pragma once
#include "Campaign.h"
#include "ui/FrontendUI.h"
#include <ApplicationHost.h>
#include <ScreenStack.h>
#include <memory>
#include <string>
#include <vector>

class CampaignEditor : public Glob2UI::Screen
{
  public:
	CampaignEditor(const std::string &name, GAGGUI::ScreenStack &screens);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	enum
	{
		ADDMAP,
		EDITMAP,
		REMOVEMAP,
		OK,
		CANCEL,
	};

  protected:
	void onEscape() override;

  private:
	friend struct MobileGallerySetup;
	Campaign campaign;
	GAGGUI::ScreenStack &screens;
	std::vector<std::string> mapNames;
	int selectedMap = -1;
	std::string saveStatus;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	void saveCampaign();
	void saveFailed();
	void addMap();
	void editMap();
	void removeMap();
	///Rebuild the map list from the campaign
	void syncMapList();
};

class CampaignMapEntryEditor : public Glob2UI::Screen
{
  public:
	CampaignMapEntryEditor(Campaign &campaign, CampaignMapEntry &mapEntry);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	/// The description as edited so far; the entry changes only on OK.
	const std::string &draftDescription() const { return description; }
	enum
	{
		OK,
		CANCEL,
		ISUNLOCKED,
	};

  protected:
	void onEscape() override { endExecute(CANCEL); }

  private:
	friend struct MobileGallerySetup;
	CampaignMapEntry &entry;
	Campaign &campaign;
	std::string name, description;
	bool unlocked;
	std::vector<std::string> otherMaps;
	std::vector<bool> unlockedBy;
	int selectedOther = -1;
	void accept();
};
