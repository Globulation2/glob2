/*
 * Globulation 2 Settings gui
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __SETTINGSSCREEN_H
#define __SETTINGSSCREEN_H

#include "GAG.h"

class SettingsScreen:public Screen
{
public:
	enum
	{
		QUIT = 0,
		JOIN = 1,
		CREATE = 2,
		UPDATE_LOST=3
	};

private:
	List *languageList;
	TextInput *userName;

public:
	SettingsScreen();
	virtual ~SettingsScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
};

#endif
