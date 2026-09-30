// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault
#include "CampaignEditor.h"
#include "ChooseMapScreen.h"
#include "MapHeader.h"
#include <algorithm>
#include <set>

using namespace Glob2UI;

CampaignEditor::CampaignEditor(const std::string &name, GAGGUI::ScreenStack &screens) : screens(screens)
{
	if (name != "" && !campaign.load(name))
		campaign.setName(name);
	syncMapList();
}

void CampaignEditor::syncMapList()
{
	mapNames.clear();
	for (unsigned n = 0; n < campaign.getMapCount(); n++)
		mapNames.push_back(campaign.getMap(n).getMapName());
	selectedMap = std::min(selectedMap, int(mapNames.size()) - 1);
	invalidate();
}

void CampaignEditor::onEscape()
{
	if (!persistence)
		endExecute(CANCEL);
}

Element CampaignEditor::build(const Presentation &p)
{
	const bool busy = bool(persistence);
	auto details = column({field(tr("[Campaign name]"), textField("name", campaign.getName(), [this](const std::string &v) { campaign.setName(v); }, {false, 0, "", {}, false, !busy})),
						   label(tr("[map description]")),
						   textEditor("description", campaign.getDescription(), [this](const std::string &v) { campaign.setDescription(v); }, {busy, 6})});
	auto maps = column({listView("maps", mapNames, selectedMap, [this](int i) { selectedMap = i; }, {{}, {}, {}, [this](int) { editMap(); }, {}, 8, tr("[No items]")}),
						wrap({button("add", tr("[add map]"), [this] { addMap(); }, {false, false, !busy}),
							  button("edit", tr("[edit map]"), [this] { editMap(); }, {false, false, !busy && selectedMap >= 0}),
							  button("remove", tr("[remove map]"), [this] { removeMap(); }, {false, false, !busy && selectedMap >= 0, false, false, true})},
							 {-1, p.pt(130)})});
	auto statusLine = saveStatus.empty() ? empty() : paragraph(saveStatus, {FontRole::Support, true});
	Element body = adaptive(
		[details, maps, statusLine](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(700))
				return scroll("editor/scroll", column({statusLine, maps, details}));
			return column({statusLine, expanded(row({expanded(maps), expanded(details)}, {-1, CrossAlign::Start}))});
		});
	return page(tr("[campaign editor]"), body,
				actions({{"ok", tr("[ok]"), [this] { saveCampaign(); }, true, SDLK_UNKNOWN, !busy},
						 {"cancel", tr("[Cancel]"), [this] { onEscape(); }, false, SDLK_ESCAPE, !busy}},
						p),
				p, 960);
}

void CampaignEditor::addMap()
{
	if (persistence)
		return;
	screens.push(std::make_unique<ChooseMapScreen>("campaigns", "map", false),
				 [this](GAGGUI::Screen &screen, int result)
				 {
					 if (result != ChooseMapScreen::OK)
						 return;
					 const auto name = static_cast<ChooseMapScreen &>(screen).getMapHeader().getMapName();
					 auto draft = std::make_shared<CampaignMapEntry>(name, glob2NameToFilename("campaigns", name, "map"));
					 screens.push(std::make_unique<CampaignMapEntryEditor>(campaign, *draft),
								  [this, draft](GAGGUI::Screen &, int result)
								  {
									  if (result == CampaignMapEntryEditor::OK)
									  {
										  campaign.appendMap(*draft);
										  syncMapList();
									  }
								  });
				 });
}

void CampaignEditor::editMap()
{
	if (persistence || selectedMap < 0 || selectedMap >= int(mapNames.size()))
		return;
	for (unsigned i = 0; i < campaign.getMapCount(); ++i)
		if (campaign.getMap(i).getMapName() == mapNames[std::size_t(selectedMap)])
		{
			screens.push(std::make_unique<CampaignMapEntryEditor>(campaign, campaign.getMap(i)),
						 [this](GAGGUI::Screen &, int result)
						 {
							 if (result == CampaignMapEntryEditor::OK)
								 syncMapList();
						 });
			break;
		}
}

void CampaignEditor::removeMap()
{
	if (persistence || selectedMap < 0 || selectedMap >= int(mapNames.size()))
		return;
	const std::string name = mapNames[std::size_t(selectedMap)];
	for (unsigned i = 0; i < campaign.getMapCount(); ++i)
	{
		auto &unlockedBy = campaign.getMap(i).getUnlockedByMaps();
		auto iter = std::find(unlockedBy.begin(), unlockedBy.end(), name);
		if (iter != unlockedBy.end())
			unlockedBy.erase(iter);
	}
	campaign.removeMap(std::size_t(selectedMap));
	syncMapList();
}

void CampaignEditor::saveFailed()
{
	persistence.reset();
	saveStatus = tr("[campaign editor save failed]");
	invalidate();
}

void CampaignEditor::saveCampaign()
{
	if (persistence)
		return;
	try
	{
		if (GAGCore::ApplicationHost::storageRestoreFailed() || !campaign.save())
		{
			saveFailed();
			return;
		}
		persistence = GAGCore::ApplicationHost::persistStorage();
		if (!persistence)
		{
			saveFailed();
			return;
		}
		saveStatus = tr("[saving to storage]");
		invalidate();
	}
	catch (const std::exception &)
	{
		saveFailed();
	}
}

void CampaignEditor::onTimer(Uint32)
{
	if (!persistence)
		return;
	const auto state = persistence->state();
	if (state == GAGCore::ApplicationHost::PersistenceState::Failed)
		saveFailed();
	else if (state == GAGCore::ApplicationHost::PersistenceState::Succeeded)
	{
		persistence.reset();
		endExecute(OK);
	}
}

CampaignMapEntryEditor::CampaignMapEntryEditor(Campaign &campaign, CampaignMapEntry &mapEntry)
	: entry(mapEntry), campaign(campaign), name(mapEntry.getMapName()), description(mapEntry.getDescription()),
	  unlocked(mapEntry.isUnlocked())
{
	std::set<std::string> unlockers(entry.getUnlockedByMaps().begin(), entry.getUnlockedByMaps().end());
	for (unsigned n = 0; n < campaign.getMapCount(); ++n)
		if (campaign.getMap(n).getMapName() != entry.getMapName())
		{
			otherMaps.push_back(campaign.getMap(n).getMapName());
			unlockedBy.push_back(unlockers.count(campaign.getMap(n).getMapName()) > 0);
		}
}

Element CampaignMapEntryEditor::build(const Presentation &p)
{
	auto details = column({field(tr("[map name]"), textField("name", name, [this](const std::string &v) { name = v; })),
						   toggle("unlocked", tr("[unlocked at start]"), unlocked, [this](bool v) { unlocked = v; }),
						   label(tr("[map description]")),
						   textEditor("description", description, [this](const std::string &v) { description = v; }, {false, 6})});
	ListOptions listOptions;
	listOptions.checked = unlockedBy;
	listOptions.toggle = [this](int i, bool v) { unlockedBy[std::size_t(i)] = v; };
	listOptions.visibleRows = 8;
	listOptions.emptyText = tr("[No items]");
	auto unlocking = column({label(tr("[unlocked by]")), listView("unlockers", otherMaps, selectedOther, [this](int i) { selectedOther = i; }, listOptions)});
	Element body = adaptive(
		[details, unlocking](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(700))
				return scroll("entry/scroll", column({details, unlocking}));
			return column({expanded(row({expanded(details), expanded(unlocking)}, {-1, CrossAlign::Start}))});
		});
	return page(tr("[editing map]"), body,
				actions({{"ok", tr("[ok]"), [this] { accept(); }, true},
						 {"cancel", tr("[Cancel]"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE}},
						p),
				p, 960);
}

void CampaignMapEntryEditor::accept()
{
	// A renamed map must keep unlocking the entries that referenced it.
	for (unsigned i = 0; i < campaign.getMapCount(); ++i)
	{
		auto &unlockers = campaign.getMap(i).getUnlockedByMaps();
		auto iter = std::find(unlockers.begin(), unlockers.end(), entry.getMapName());
		if (iter != unlockers.end())
			*iter = name;
	}
	entry.setMapName(name);
	entry.setDescription(description);
	entry.getUnlockedByMaps().clear();
	for (std::size_t i = 0; i < otherMaps.size(); ++i)
		if (unlockedBy[i])
			entry.getUnlockedByMaps().push_back(otherMaps[i]);
	if (!unlocked)
		entry.lockMap();
	else
		entry.unlockMap();
	endExecute(OK);
}
