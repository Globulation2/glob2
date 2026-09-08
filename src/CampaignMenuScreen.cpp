// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "CampaignMenuScreen.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "Engine.h"
#include "GameSessionScreen.h"
#include "GameLoadScreen.h"
#include "MessageScreen.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"
#include "GUIMessageBox.h"
#include <BinaryStream.h>
#include <FileManager.h>

CampaignMenuScreen::CampaignMenuScreen(const std::string& name, GAGGUI::ScreenStack& screens) : screens(screens)
{
    enablePhoneForm();
	if (!campaign.load(name))
		campaign.setName(name);
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", campaign.getName());
	addWidget(title);
	startMission = new TextButton(10, 430, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[start mission]"), START);
	addWidget(startMission);
	exitButton = new TextButton(330, 430, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), EXIT);
	addWidget(exitButton);
	playerName = new TextInput(330, 225, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", campaign.getPlayerName());
	addWidget(playerName);
	availableMissions = new CheckList(10, 50, 300, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	repopulateAvailableMissions();
	addWidget(availableMissions);
	
	
	mapPreview = new MapPreview(330, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
	
	description = new TextArea(10, 260, 620, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", true);
	addWidget(description);
    if (GAGCore::ApplicationHost::canImportFiles()) {
        importButton = new TextButton(10, 480, 145, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
            "standard", Toolkit::getStringTable()->getString("[import progress]"), 2);
        exportButton = new TextButton(165, 480, 145, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
            "standard", Toolkit::getStringTable()->getString("[export progress]"), 3);
        addWidget(importButton); addWidget(exportButton);
    }
    retryButton = new TextButton(330, 185, 145, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
        "standard", Toolkit::getStringTable()->getString("[retry save]"), 4);
    retryButton->visible = false;
    addWidget(retryButton);
}

void CampaignMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
    if (action == SCREEN_DESTROYED) {
        // Also persist progress when application quit unwinds the stack and
        // suppresses normal continuation callbacks.
        if (saveFailed) restorePrevious();
        else if (dirty && !persistence && !GAGCore::ApplicationHost::storageRestoreFailed()) campaign.save(true);
        return;
    }
    if (persistence) {
        if (action == TEXT_MODIFIED && source == playerName) playerName->setText(campaign.getPlayerName());
        return;
    }
    if (fileSelection) {
        if (action == TEXT_MODIFIED && source == playerName) playerName->setText(campaign.getPlayerName());
        if ((action == BUTTON_RELEASED || action == BUTTON_SHORTCUT) && source == exitButton) {
            fileSelection.reset();
            title->setText(campaign.getName());
            GAGCore::ApplicationHost::importChanged("cancelled");
        }
        return;
    }
    if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT) {
        if (importButton && source == importButton) {
            fileSelection = GAGCore::ApplicationHost::selectFile("campaign");
            title->setText(Toolkit::getStringTable()->getString("[select import file]"));
            GAGCore::ApplicationHost::importChanged("selecting");
            return;
        }
        if (exportButton && source == exportButton) { exportProgress(); return; }
        if (source == retryButton) { saveProgress(leaveAfterSave); return; }
    }
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==EXIT)
		{
            if (saveFailed) { if (restorePrevious()) endExecute(par1); }
            else if (!dirty) endExecute(par1);
            else saveProgress(true);
		}
		else if(par1==START)
		{
			CampaignMapEntry* selected = getSelectedMission();
			if (selected)
			{
                // A session may complete a mission even when application quit
                // suppresses its normal return callback.
                dirty = true;
                const auto filename = selected->getMapFileName(), mission = selected->getMapName();
                screens.push(std::make_unique<GameLoadScreen>([this, filename, mission](Engine& engine) {
                    return engine.initCampaignTask(filename, &campaign, mission);
                }), [this](Screen& loading, int result) {
                    if (result == 1) {
                        screens.push(std::make_unique<GameSessionScreen>(screens, static_cast<GameLoadScreen&>(loading).takeEngine()),
                            [this](Screen&, int) {
                                repopulateAvailableMissions();
                                dirty = true;
                                saveProgress();
                            });
                    } else if (result == 2) {
                        auto& strings = *Toolkit::getStringTable();
                        screens.push(std::make_unique<MessageScreen>(strings.getString("[ERROR_CANT_LOAD_MAP]"),
                            std::vector<std::string>{strings.getString("[ok]")}));
                    }
                });
			}
		}
	}
	else if(action==TEXT_MODIFIED)
	{
		if(source==playerName)
		{
			campaign.setPlayerName(playerName->getText());
            dirty = true;
		}
	}
	else if (action == LIST_ELEMENT_SELECTED)
	{
		CampaignMapEntry* selected = getSelectedMission();
		if (selected)
		{
			mapPreview->setMapThumbnail(selected->getMapFileName().c_str());
			description->setText(Toolkit::getStringTable()->getString(selected->getDescription()));
		}
	}
}



CampaignMapEntry* CampaignMenuScreen::getSelectedMission()
{
	// List::get() asserts (and is undefined behavior in release builds) when the
	// list has no selection. The list starts unselected and repopulateAvailableMissions()
	// clears the selection after every mission run, so guard before dereferencing it.
	if (availableMissions->getSelectionIndex() < 0)
		return nullptr;
	return campaign.findUnlockedMap(availableMissions->get());
}



void CampaignMenuScreen::repopulateAvailableMissions()
{
	availableMissions->clear();
	for (unsigned i = 0; i < campaign.getMapCount(); ++i)
	{
		if (campaign.getMap(i).isUnlocked())
			availableMissions->addItem(campaign.getMap(i).getMapName(), campaign.getMap(i).isCompleted());
	}
}



void CampaignMenuScreen::setNewCampaign()
{
    dirty = true;
	campaign.setPlayerName(globalContainer->settings.getUsername());
	playerName->setText(globalContainer->settings.getUsername());
}


void CampaignMenuScreen::saveFailure()
{
    persistence.reset(); saveFailed = true;
    retryButton->visible = true;
    title->setText(Toolkit::getStringTable()->getString("[campaign save failed]"));
    exitButton->setText(Toolkit::getStringTable()->getString("[leave without saving]"));
    GAGCore::ApplicationHost::importChanged("failed");
}
void CampaignMenuScreen::saveProgress(bool leave)
{
    leaveAfterSave = leave;
    try {
        if (GAGCore::ApplicationHost::storageRestoreFailed()) { saveFailure(); return; }
        capturePrevious();
        if (!campaign.save(true)) { saveFailure(); return; }
        persistence = GAGCore::ApplicationHost::persistStorage();
        if (!persistence) { saveFailure(); return; }
        saveFailed = false; retryButton->visible = false;
        title->setText(Toolkit::getStringTable()->getString("[saving to storage]"));
        GAGCore::ApplicationHost::importChanged("persisting");
    } catch (const std::exception&) { saveFailure(); }
}
void CampaignMenuScreen::exportProgress()
{
    try {
        if (GAGCore::ApplicationHost::exportFile("campaign-progress.campaign", campaign.exportProgress())) return;
    } catch (const std::exception&) {}
    title->setText(Toolkit::getStringTable()->getString("[export failed]"));
}
void CampaignMenuScreen::onTimer(Uint32)
{
    if (fileSelection) {
        const auto state = fileSelection->state();
        if (state == GAGCore::ApplicationHost::FileSelectionState::Pending) return;
        if (state == GAGCore::ApplicationHost::FileSelectionState::Selected && campaign.importProgress(fileSelection->takeFile().bytes)) {
            fileSelection.reset();
            playerName->setText(campaign.getPlayerName());
            repopulateAvailableMissions();
            dirty = true;
            saveProgress();
        } else {
            title->setText(Toolkit::getStringTable()->getString(state == GAGCore::ApplicationHost::FileSelectionState::Cancelled ? "[import cancelled]" : "[campaign import failed]"));
            GAGCore::ApplicationHost::importChanged(state == GAGCore::ApplicationHost::FileSelectionState::Cancelled ? "cancelled" : "invalid");
            fileSelection.reset();
        }
    }
    if (persistence) {
        const auto state = persistence->state();
        if (state == GAGCore::ApplicationHost::PersistenceState::Pending) return;
        if (state == GAGCore::ApplicationHost::PersistenceState::Failed) { saveFailure(); return; }
        persistence.reset(); dirty = false; saveFailed = false;
        previousCaptured = false; previousFile.clear();
        title->setText(campaign.getName());
        exitButton->setText(Toolkit::getStringTable()->getString("[goto main menu]"));
        GAGCore::ApplicationHost::importChanged("succeeded");
        if (leaveAfterSave) endExecute(EXIT);
    }
}

std::string CampaignMenuScreen::progressPath() const
{
    return glob2NameToFilename("games", campaign.getName(), "txt");
}
bool CampaignMenuScreen::readProgressFile(std::vector<unsigned char>& bytes) const
{
    auto& files = *Toolkit::getFileManager();
    if (!files.exists(progressPath())) return false;
    GAGCore::BinaryInputStream input(files.openInputStreamBackend(progressPath()));
    if (!input.isValid()) throw std::ios_base::failure("Cannot read prior campaign progress");
    input.seekFromEnd(0);
    const auto size = input.getPosition();
    if (size > 64u*1024u*1024u) throw std::ios_base::failure("Campaign file exceeds backup limit");
    input.seekFromStart(0); bytes.resize(size);
    GAGCore::BinaryInputStream::CheckedReads checked(&input);
    input.read(bytes.data(), size, "campaign");
    return true;
}
void CampaignMenuScreen::capturePrevious()
{
    if (previousCaptured) return;
    previousFile.clear();
    previousExisted = readProgressFile(previousFile);
    previousCaptured = true;
}
bool CampaignMenuScreen::restorePrevious()
{
    if (!previousCaptured) return true;
    try {
        auto& files = *Toolkit::getFileManager();
        std::vector<unsigned char> current;
        const bool exists = readProgressFile(current);
        if (exists == previousExisted && current == previousFile) return true;
        if (!previousExisted) { files.remove(progressPath()); return !files.exists(progressPath()); }
        return files.writeAtomically(progressPath(), [this](GAGCore::OutputStream& output) {
            output.write(previousFile.data(), previousFile.size(), "campaign rollback");
        });
    } catch (const std::exception&) { return false; }
}
