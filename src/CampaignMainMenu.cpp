// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault
#include "CampaignMainMenu.h"
#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"

using namespace Glob2UI;

CampaignMainMenu::CampaignMainMenu(GAGGUI::ScreenStack &screens) : screens(screens) {}

Element CampaignMainMenu::build(const Presentation &p)
{
	return menu(tr("[campaign]"),
				{{"new", tr("[start new campaign]"), [this] { runCampaignSelection(true); }, true},
				 {"load", tr("[load campaign]"), [this] { runCampaignSelection(false); }, false, SDLK_RETURN},
				 {"back", tr("[goto main menu]"), [this] { endExecute(CANCELLED); }, false, SDLK_ESCAPE}},
				p);
}

void CampaignMainMenu::runCampaignSelection(bool newCampaign)
{
	screens.push(std::make_unique<CampaignSelectorScreen>(!newCampaign),
				 [this, newCampaign](GAGGUI::Screen &selected, int result)
				 {
					 if (result != CampaignSelectorScreen::OK)
						 return;
					 auto menu = std::make_unique<CampaignMenuScreen>(
						 static_cast<CampaignSelectorScreen &>(selected).getCampaignName(), screens);
					 if (newCampaign)
						 menu->setNewCampaign();
					 screens.push(std::move(menu));
				 });
}
