// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// All construction and event handling for the "Keyboard Shortcuts" tab of the
// settings screen. Split out of SettingsScreen.cpp to keep each file under
// 500 lines. Owns the two KeyboardManager mirrors (game GUI / map editor)
// and the widget state that lets the user list, add, remove, and re-bind
// individual shortcuts.

#include "SettingsScreen.h"
#include <GUIText.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <string>
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"


void SettingsScreen::buildKeyboardShortcutsTab()
{
	game_shortcuts=new TextButton( 10, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[game shortcuts]"), GAMESHORTCUTS);

	editor_shortcuts=new TextButton( 140, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[editor shortcuts]"), EDITORSHORTCUTS);

	shortcut_list = new List(20, 110, 325, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	action_list = new List(365, 110 , 265, 190, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	select_key_1 = new KeySelector(20, 275, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", 100, 25);
	key_2_active = new OnOffButton(125, 275, 25, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, false, SECONDKEY);
	select_key_2 = new KeySelector(155, 275, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", 100, 25);
	pressedUnpressedSelector = new MultiTextButton(260, 275, 80, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", PRESSEDSELECTOR);
	add_shortcut = new TextButton(20, 305, 158, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[add shortcut]"), ADDSHORTCUT);
	remove_shortcut = new TextButton(188, 305, 157, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[remove shortcut]"), REMOVESHORTCUT);
	restore_default_shortcuts = new TextButton(365, 305, 265, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[restore default shortcuts]"), RESTOREDEFAULTSHORTCUTS);

	pressedUnpressedSelector->clearTexts();
	pressedUnpressedSelector->addText(Toolkit::getStringTable()->getString("[on press]"));
	pressedUnpressedSelector->addText(Toolkit::getStringTable()->getString("[on unpress]"));

	addWidgetToGroup(game_shortcuts, keyboardGroup);
	addWidgetToGroup(editor_shortcuts, keyboardGroup);
	addWidgetToGroup(shortcut_list, keyboardGroup);
	addWidgetToGroup(action_list, keyboardGroup);
	addWidgetToGroup(select_key_1, keyboardGroup);
	addWidgetToGroup(key_2_active, keyboardGroup);
	addWidgetToGroup(select_key_2, keyboardGroup);
	addWidgetToGroup(pressedUnpressedSelector, keyboardGroup);
	addWidgetToGroup(add_shortcut, keyboardGroup);
	addWidgetToGroup(remove_shortcut, keyboardGroup);
	addWidgetToGroup(restore_default_shortcuts, keyboardGroup);
}


void SettingsScreen::updateShortcutList(int an)
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;

	const std::list<KeyboardShortcut>& shortcuts = m->getKeyboardShortcuts();
	size_t n = 0;
	for(std::list<KeyboardShortcut>::const_iterator i = shortcuts.begin(); i!=shortcuts.end(); ++i)
	{
		if(an==-1 || int(n) == an)
		{
			std::string name = i->formatTranslated(currentMode);
			if(n >= shortcut_list->getCount())
				shortcut_list->addText(name);
			else if(shortcut_list->getText(n) != name)
				shortcut_list->setText(n, name);
		}
		n += 1;
	}
	//Remove entries that are off the end
	while(n < shortcut_list->getCount())
		shortcut_list->removeText(n);
}



void SettingsScreen::updateActionList()
{
	action_list->clear();
	if(shortcut_list->selection())
	{
		if(currentMode == GameGUIShortcuts)
		{
			for(int i=GameGUIKeyActions::DoNothing; i<GameGUIKeyActions::ActionSize; ++i)
			{
				std::string key = "[" + GameGUIKeyActions::getName(i) + "]";
				action_list->addText(Toolkit::getStringTable()->getString(key.c_str()));
			}
		}
		else if(currentMode == MapEditShortcuts)
		{
			for(int i=MapEditKeyActions::DoNothing; i<MapEditKeyActions::ActionSize; ++i)
			{
				std::string key = "[" + MapEditKeyActions::getName(i) + "]";
				action_list->addText(Toolkit::getStringTable()->getString(key.c_str()));
			}
		}
	}
}



void SettingsScreen::updateShortcutInfoFromSelection()
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;

	const std::list<KeyboardShortcut>& shortcuts = m->getKeyboardShortcuts();
	auto sel = shortcut_list->selection();

	if(!sel)
	{
		select_key_1->visible=false;
		key_2_active->visible=false;
		select_key_2->visible=false;
		action_list->visible=false;
	}
	else
	{
		std::list<KeyboardShortcut>::const_iterator i = shortcuts.begin();
		std::advance(i, *sel);
		select_key_1->setKey(i->getKeyPress(0));
		if(i->getKeyPressCount() == 1)
		{
			key_2_active->setState(false);
			select_key_2->visible=false;
		}
		else
		{
			select_key_2->setKey(i->getKeyPress(1));
			key_2_active->setState(true);
			select_key_2->visible=true;
		}

		if(i->getKeyPress(0).getPressed())
			pressedUnpressedSelector->setIndex(0);
		else
			pressedUnpressedSelector->setIndex(1);

		action_list->setSelectionIndex(i->getAction());
		action_list->centerOnItem(action_list->getSelectionIndex());
	}
}



void SettingsScreen::updateKeyboardManagerFromShortcutInfo()
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;

	std::list<KeyboardShortcut>& shortcuts = m->getKeyboardShortcuts();
	auto sel = shortcut_list->selection();

	if(sel)
	{
		std::list<KeyboardShortcut>::iterator i = shortcuts.begin();
		std::advance(i, *sel);
		KeyboardShortcut new_shortcut;

		KeyPress first = KeyPress(select_key_1->getKey(), (pressedUnpressedSelector->getIndex() == 0 ? true : false));
		KeyPress second = KeyPress(select_key_2->getKey(), (pressedUnpressedSelector->getIndex() == 0 ? true : false));

		new_shortcut.addKeyPress(first);
		if(key_2_active->getState())
			new_shortcut.addKeyPress(second);
		new_shortcut.setAction(action_list->getSelectionIndex());
		(*i) = new_shortcut;
		updateShortcutList(*sel);
	}
}



void SettingsScreen::loadDefaultKeyboardShortcuts()
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;
	m->loadDefaultShortcuts();
	updateShortcutList();
	updateShortcutInfoFromSelection();
}



void SettingsScreen::addNewShortcut()
{
	KeyboardShortcut ks;
	ks.addKeyPress(KeyPress());
	if(currentMode == GameGUIShortcuts)
	{
		ks.setAction(GameGUIKeyActions::DoNothing);
		std::list<KeyboardShortcut>& shortcuts = guiKeyboardManager.getKeyboardShortcuts();
		shortcuts.push_back(ks);
	}
	else if(currentMode == MapEditShortcuts)
	{
		ks.setAction(MapEditKeyActions::DoNothing);
		std::list<KeyboardShortcut>& shortcuts = mapeditKeyboardManager.getKeyboardShortcuts();
		shortcuts.push_back(ks);
	}
	updateShortcutList(shortcut_list->getCount());
	shortcut_list->setSelectionIndex(shortcut_list->getCount()-1);
	shortcut_list->centerOnItem(shortcut_list->getCount()-1);
	updateShortcutInfoFromSelection();
}



void SettingsScreen::removeShortcut()
{
	int selection_n = shortcut_list->getSelectionIndex();
	if(currentMode == GameGUIShortcuts)
	{
		std::list<KeyboardShortcut>& shortcuts = guiKeyboardManager.getKeyboardShortcuts();
		std::list<KeyboardShortcut>::iterator i = shortcuts.begin();
		std::advance(i, selection_n);
		shortcuts.erase(i);
	}
	else if(currentMode == MapEditShortcuts)
	{
		std::list<KeyboardShortcut>& shortcuts = mapeditKeyboardManager.getKeyboardShortcuts();
		std::list<KeyboardShortcut>::iterator i = shortcuts.begin();
		std::advance(i, selection_n);
		shortcuts.erase(i);
	}
	shortcut_list->setSelectionIndex(std::max(0, selection_n-1));
	updateShortcutList();
	updateShortcutInfoFromSelection();
}
