// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "EditorMainMenu.h"
#include "CampaignEditor.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "EditorGenerateScreen.h"
#include "EditorLoadScreen.h"
#include "MapEdit.h"
#include "MapEditorScreen.h"
#include "MessageScreen.h"
#include "NewMapScreen.h"
#include <ctime>

using namespace Glob2UI;

EditorMainMenu::EditorMainMenu(GAGGUI::ScreenStack &screens) : screens(screens) {}

Element EditorMainMenu::build(const Presentation &p)
{
	return menu(tr("[editor]"),
				{{"new-map", tr("[new map]"), [this] { newMap(); }, true, SDLK_RETURN},
				 {"load-map", tr("[load map]"), [this] { loadMap(); }},
				 {"new-campaign", tr("[new campaign]"), [this] { screens.push(std::make_unique<CampaignEditor>("", screens)); }},
				 {"load-campaign", tr("[load campaign]"), [this] { loadCampaign(); }},
				 {"back", tr("[goto main menu]"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE}},
				p);
}

void EditorMainMenu::newMap()
{
    screens.push(std::make_unique<NewMapScreen>(GeneratorRegistry::builtins(), &screens), [this](GAGGUI::Screen& screen, int result) {
        if (result != NewMapScreen::OK) return;
        screens.push(std::make_unique<EditorGenerateScreen>(static_cast<NewMapScreen&>(screen).descriptor,
            static_cast<Uint32>(std::time(nullptr))), [this](GAGGUI::Screen& generated, int result) {
                if (result == 1)
                    screens.push(std::make_unique<MapEditorScreen>(screens, static_cast<EditorGenerateScreen&>(generated).takeEditor()));
                else if (result == 2) {
                    screens.push(std::make_unique<MessageScreen>(tr("[ERROR_CANT_GENERATE_MAP]"),
                        std::vector<std::string>{tr("[ok]")}), [this](GAGGUI::Screen&, int) { newMap(); });
                } else newMap();
            });
    });
}

void EditorMainMenu::loadMap()
{
	screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false, "games", "game", false),
		[this](GAGGUI::Screen& screen, int result) {
			if (result != ChooseMapScreen::OK) return;
			const auto filename = static_cast<ChooseMapScreen&>(screen).getMapHeader().getFileName();
			screens.push(std::make_unique<EditorLoadScreen>(filename), [this](GAGGUI::Screen& loading, int result) {
				if (result == 1)
					screens.push(std::make_unique<MapEditorScreen>(screens,
						static_cast<EditorLoadScreen&>(loading).takeEditor()));
				else if (result == 2)
					screens.push(std::make_unique<MessageScreen>(tr("[ERROR_CANT_LOAD_MAP]"),
						std::vector<std::string>{tr("[ok]")}));
			});
		});
}

void EditorMainMenu::loadCampaign()
{
	screens.push(std::make_unique<CampaignSelectorScreen>(), [this](GAGGUI::Screen& screen, int result) {
		if (result == CampaignSelectorScreen::OK)
			screens.push(std::make_unique<CampaignEditor>(static_cast<CampaignSelectorScreen&>(screen).getCampaignName(), screens));
	});
}
