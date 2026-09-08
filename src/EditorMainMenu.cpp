// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "CampaignEditor.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "EditorMainMenu.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GUIText.h>
#include "MapEdit.h"
#include "MapGenerator.h"
#include "NewMapScreen.h"
#include <StringTable.h>
#include <Toolkit.h>
#include "Utilities.h"



using namespace GAGGUI;

EditorMainMenu::EditorMainMenu(GAGGUI::ScreenStack& screens) : screens(screens)
{
	addWidget(new TextButton(0,  70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[new map]"), NEWMAP, 13));
	addWidget(new TextButton(0,  130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load map]"), LOADMAP));
	addWidget(new TextButton(0, 190, 300, 40,  ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[new campaign]"), NEWCAMPAIGN));
	addWidget(new TextButton(0, 250, 300, 40,  ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load campaign]"), LOADCAMPAIGN));
	addWidget(new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), CANCEL, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[editor]")));
}

void EditorMainMenu::newMap()
{
    screens.push(std::make_unique<NewMapScreen>(), [this](Screen& screen, int result) {
        if (result != NewMapScreen::OK) return;
        MapEdit editor;
        MapGenerator generator;
        setRandomSyncRandSeed();
        if (!generator.generateMap(editor.game, static_cast<NewMapScreen&>(screen).descriptor)) {
            newMap();
            return;
        }
        editor.mapHasBeenModified();
        editor.regenerateGameHeader();
        if (editor.run() == Screen::QUIT_APPLICATION) screens.stop();
    });
}

void EditorMainMenu::onAction(Widget*, Action action, int choice, int)
{
    if (action != BUTTON_RELEASED && action != BUTTON_SHORTCUT) return;
    switch (choice) {
    case NEWMAP: newMap(); break;
    case LOADMAP:
        screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false, "games", "game", false),
            [this](Screen& screen, int result) {
                if (result != ChooseMapScreen::OK) return;
                MapEdit editor;
                const auto filename = static_cast<ChooseMapScreen&>(screen).getMapHeader().getFileName();
                if (!editor.load(filename)) return;
                if (editor.run() == Screen::QUIT_APPLICATION) screens.stop();
            });
        break;
    case NEWCAMPAIGN: screens.push(std::make_unique<CampaignEditor>("", screens)); break;
    case LOADCAMPAIGN:
        screens.push(std::make_unique<CampaignSelectorScreen>(), [this](Screen& screen, int result) {
            if (result == CampaignSelectorScreen::OK)
                screens.push(std::make_unique<CampaignEditor>(static_cast<CampaignSelectorScreen&>(screen).getCampaignName(), screens));
        });
        break;
    case CANCEL: endExecute(CANCEL); break;
    }
}
