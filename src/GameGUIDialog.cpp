/*
 * Globulation 2 game dialog
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GameGUIDialog.h"
#include "GlobalContainer.h"

extern GlobalContainer globalContainer;

InGameSaveScreen::InGameSaveScreen()
:InGameScreen()
{
	filename=NULL;
}

void InGameSaveScreen::onAction(Widget *source, Action action, int par1, int par2)
{

}

void InGameSaveScreen::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_KEYDOWN)
		isEnded=true;
}

void InGameSaveScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(0, 0, gfxCtx->getW(), gfxCtx->getH(), 0, 0, 255);
}


InGameMainScreen::InGameMainScreen()
:InGameScreen()
{
	
}

void InGameMainScreen::onAction(Widget *source, Action action, int par1, int par2)
{

}

void InGameMainScreen::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_KEYDOWN)
		isEnded=true;
}

void InGameMainScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(0, 0, gfxCtx->getW(), gfxCtx->getH(), 0, 0, 255);
}
