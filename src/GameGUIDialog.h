/*
 * Globulation 2 game dialog
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GAME_GUI_DIALOG_H
#define __GAME_GUI_DIALOG_H

#include "GAG.h"

class InGameScreen:public Screen
{
public:
	InGameScreen() { isEnded=false;}
	virtual ~InGameScreen() { }

public:
	bool isEnded;
};

class InGameSaveScreen:public InGameScreen
{
private:
	int decX, decY;

public:
	char *filename;

public:
	InGameSaveScreen();
	virtual ~InGameSaveScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(int x, int y, int w, int h);
};

class InGameMainScreen:public InGameScreen
{
private:
	int decX, decY;

public:
	InGameMainScreen();
	virtual ~InGameMainScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(int x, int y, int w, int h);
};

#endif