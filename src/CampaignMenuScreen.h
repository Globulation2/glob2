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

class MapPreview;

///The main campaign screen: pick an unlocked mission and play it.
class CampaignMenuScreen : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return "campaign_menu"; }
	CampaignMenuScreen(const std::string &name, GAGGUI::ScreenStack &screens);
	~CampaignMenuScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void setNewCampaign();
	void onTimer(Uint32) override;
	enum
	{
		EXIT,
		START,
	};

  protected:
	void onEscape() override { leave(); }

  private:
	Campaign campaign;
	bool dirty = false, saveFailed = false, leaveAfterSave = false;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	std::unique_ptr<GAGCore::ApplicationHost::FileSelection> fileSelection;
	void saveProgress(bool leave = false);
	void saveFailure();
	void exportProgress();
	void importProgress();
	void startMission();
	void leave();
	std::vector<unsigned char> previousFile;
	bool previousCaptured = false, previousExisted = false;
	std::string progressPath() const;
	bool readProgressFile(std::vector<unsigned char> &bytes) const;
	void capturePrevious();
	bool restorePrevious();
	GAGGUI::ScreenStack &screens;

	std::string status;
	std::string playerName;
	std::vector<std::string> missionNames;
	std::vector<bool> missionCompleted;
	int selectedMission = -1;
	std::string description;
	std::unique_ptr<MapPreview> mapPreview;
	bool persisted = false;

	//! Rebuild the displayed mission list from the current campaign state.
	void repopulateAvailableMissions();
	void selectMission(int index);
	//! The campaign entry for the selected row, or nullptr when nothing is selected.
	CampaignMapEntry *getSelectedMission();
	void showStatus(const std::string &text);
};
