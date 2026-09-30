// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault
#include "CampaignMenuScreen.h"
#include "Engine.h"
#include "GUIMapPreview.h"
#include "GameLoadScreen.h"
#include "GameSessionScreen.h"
#include "GlobalContainer.h"
#include "MessageScreen.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>

using namespace Glob2UI;

CampaignMenuScreen::CampaignMenuScreen(const std::string &name, GAGGUI::ScreenStack &screens) : screens(screens)
{
	if (!campaign.load(name))
		campaign.setName(name);
	status = campaign.getName();
	playerName = campaign.getPlayerName();
	mapPreview = std::make_unique<MapPreview>();
	repopulateAvailableMissions();
}

CampaignMenuScreen::~CampaignMenuScreen()
{
	// Also persist progress when application quit unwinds the stack and
	// suppresses normal continuation callbacks.
	if (saveFailed)
		restorePrevious();
	else if (dirty && !persistence && !GAGCore::ApplicationHost::storageRestoreFailed())
		campaign.save(true);
}

void CampaignMenuScreen::showStatus(const std::string &text)
{
	status = text;
	invalidate();
}

Element CampaignMenuScreen::build(const Presentation &p)
{
	const bool busy = bool(persistence) || bool(fileSelection);
	ListOptions listOptions;
	listOptions.checked = missionCompleted;
	listOptions.toggle = [](int, bool) {};
	listOptions.activate = [this](int) { startMission(); };
	listOptions.visibleRows = 8;
	listOptions.emptyText = tr("[No items]");
	auto missions = listView("missions", missionNames, selectedMission, [this](int i) { selectMission(i); }, listOptions);
	auto side = column({Glob2UI::mapPreview("preview", *this->mapPreview, 160),
						field(tr("[player name]"), textField("player", playerName,
															[this](const std::string &v)
															{
																if (persistence || fileSelection)
																	return;
																playerName = v;
																campaign.setPlayerName(v);
																dirty = true;
															}, {false, 32, "", {}, false, !busy}))});
	auto text = scroll("description", paragraph(description));
	std::vector<Element> tools;
	if (GAGCore::ApplicationHost::canImportFiles())
	{
		tools.push_back(button("import", tr("[import progress]"), [this] { importProgress(); }, {false, false, !busy}));
		tools.push_back(button("export", tr("[export progress]"), [this] { exportProgress(); }, {false, false, !busy}));
	}
	if (saveFailed)
		tools.push_back(button("retry", tr("[retry save]"), [this] { saveProgress(leaveAfterSave); }, {true}));
	auto toolRow = tools.empty() ? empty() : wrap(std::move(tools), {-1, p.pt(150)});
	auto statusLine = status == campaign.getName() ? empty() : paragraph(status, {FontRole::Support, true});
	Element body = adaptive(
		[missions, side, text, toolRow, statusLine](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return scroll("campaign/scroll", column({statusLine, side, missions, text, toolRow}));
			return column({statusLine, expanded(row({expanded(missions), width(ctx.presentation.pt(260), side)}, {-1, CrossAlign::Stretch})),
						   height(ctx.presentation.pt(120), text), toolRow});
		});
	return page(campaign.getName(), body,
				actions({{"start", tr("[start mission]"), [this] { startMission(); }, true, SDLK_RETURN, selectedMission >= 0 && !busy},
						 {"exit", tr(saveFailed ? "[leave without saving]" : "[goto main menu]"), [this] { leave(); }, false, SDLK_ESCAPE}},
						p),
				p, 960);
}

void CampaignMenuScreen::leave()
{
	if (fileSelection)
	{
		fileSelection.reset();
		showStatus(campaign.getName());
		GAGCore::ApplicationHost::importChanged("cancelled");
		return;
	}
	if (persistence)
		return;
	if (saveFailed)
	{
		if (restorePrevious())
			endExecute(EXIT);
	}
	else if (!dirty)
		endExecute(EXIT);
	else
		saveProgress(true);
}

void CampaignMenuScreen::startMission()
{
	if (persistence || fileSelection)
		return;
	CampaignMapEntry *selected = getSelectedMission();
	if (!selected)
		return;
	// A session may complete a mission even when application quit
	// suppresses its normal return callback.
	dirty = true;
	const auto filename = selected->getMapFileName(), mission = selected->getMapName();
	screens.push(std::make_unique<GameLoadScreen>([this, filename, mission](Engine &engine)
												  { return engine.initCampaignTask(filename, &campaign, mission); }),
				 [this](GAGGUI::Screen &loading, int result)
				 {
					 if (result == 1)
						 screens.push(std::make_unique<GameSessionScreen>(screens, static_cast<GameLoadScreen &>(loading).takeEngine()),
									  [this](GAGGUI::Screen &, int)
									  {
										  repopulateAvailableMissions();
										  dirty = true;
										  saveProgress();
									  });
					 else if (result == 2)
						 screens.push(std::make_unique<MessageScreen>(tr("[ERROR_CANT_LOAD_MAP]"), std::vector<std::string>{tr("[ok]")}));
				 });
}

void CampaignMenuScreen::importProgress()
{
	if (persistence || fileSelection)
		return;
	fileSelection = GAGCore::ApplicationHost::selectFile("campaign");
	showStatus(tr("[select import file]"));
	GAGCore::ApplicationHost::importChanged("selecting");
}

void CampaignMenuScreen::selectMission(int index)
{
	selectedMission = index;
	CampaignMapEntry *selected = getSelectedMission();
	if (selected)
	{
		mapPreview->setMapThumbnail(selected->getMapFileName().c_str());
		description = tr(selected->getDescription());
	}
	else
	{
		mapPreview->setMapThumbnail(MapThumbnail());
		description.clear();
	}
	invalidate();
}

CampaignMapEntry *CampaignMenuScreen::getSelectedMission()
{
	if (selectedMission < 0 || selectedMission >= int(missionNames.size()))
		return nullptr;
	return campaign.findUnlockedMap(missionNames[std::size_t(selectedMission)]);
}

void CampaignMenuScreen::repopulateAvailableMissions()
{
	missionNames.clear();
	missionCompleted.clear();
	for (unsigned i = 0; i < campaign.getMapCount(); ++i)
		if (campaign.getMap(i).isUnlocked())
		{
			missionNames.push_back(campaign.getMap(i).getMapName());
			missionCompleted.push_back(campaign.getMap(i).isCompleted());
		}
	selectedMission = -1;
	description.clear();
	mapPreview->setMapThumbnail(MapThumbnail());
	invalidate();
}

void CampaignMenuScreen::setNewCampaign()
{
	dirty = true;
	campaign.setPlayerName(globalContainer->settings.getUsername());
	playerName = globalContainer->settings.getUsername();
	invalidate();
}

void CampaignMenuScreen::saveFailure()
{
	persistence.reset();
	saveFailed = true;
	showStatus(tr("[campaign save failed]"));
	GAGCore::ApplicationHost::importChanged("failed");
}

void CampaignMenuScreen::saveProgress(bool leave)
{
	leaveAfterSave = leave;
	try
	{
		if (GAGCore::ApplicationHost::storageRestoreFailed())
		{
			saveFailure();
			return;
		}
		capturePrevious();
		if (!campaign.save(true))
		{
			saveFailure();
			return;
		}
		persistence = GAGCore::ApplicationHost::persistStorage();
		if (!persistence)
		{
			saveFailure();
			return;
		}
		saveFailed = false;
		showStatus(tr("[saving to storage]"));
		GAGCore::ApplicationHost::importChanged("persisting");
	}
	catch (const std::exception &)
	{
		saveFailure();
	}
}

void CampaignMenuScreen::exportProgress()
{
	try
	{
		if (GAGCore::ApplicationHost::exportFile("campaign-progress.campaign", campaign.exportProgress()))
			return;
	}
	catch (const std::exception &)
	{
	}
	showStatus(tr("[export failed]"));
}

void CampaignMenuScreen::onTimer(Uint32)
{
	if (fileSelection)
	{
		const auto state = fileSelection->state();
		if (state == GAGCore::ApplicationHost::FileSelectionState::Pending)
			return;
		if (state == GAGCore::ApplicationHost::FileSelectionState::Selected &&
			campaign.importProgress(fileSelection->takeFile().bytes))
		{
			fileSelection.reset();
			playerName = campaign.getPlayerName();
			repopulateAvailableMissions();
			dirty = true;
			saveProgress();
		}
		else
		{
			const bool cancelled = state == GAGCore::ApplicationHost::FileSelectionState::Cancelled;
			showStatus(tr(cancelled ? "[import cancelled]" : "[campaign import failed]"));
			GAGCore::ApplicationHost::importChanged(cancelled ? "cancelled" : "invalid");
			fileSelection.reset();
		}
	}
	if (persistence)
	{
		const auto state = persistence->state();
		if (state == GAGCore::ApplicationHost::PersistenceState::Pending)
			return;
		if (state == GAGCore::ApplicationHost::PersistenceState::Failed)
		{
			saveFailure();
			return;
		}
		persistence.reset();
		dirty = false;
		saveFailed = false;
		previousCaptured = false;
		previousFile.clear();
		showStatus(campaign.getName());
		GAGCore::ApplicationHost::importChanged("succeeded");
		if (leaveAfterSave)
			endExecute(EXIT);
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
