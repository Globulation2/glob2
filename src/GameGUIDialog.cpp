/*
 * Globulation 2 game dialog
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GameGUIDialog.h"
#include "GlobalContainer.h"

extern GlobalContainer globalContainer;

InGameScreen::InGameScreen(int w, int h)
{
	gfxCtx=new SDLOffScreenGraphicContext(w, h, false, 128);
	decX=(globalContainer.gfx.getW()-w)>>1;
	decY=(globalContainer.gfx.getH()-h)>>1;
	endValue=-1;
}

InGameScreen::~InGameScreen()
{
	delete gfxCtx;
}

void InGameScreen::translateAndProcessEvent(SDL_Event *event)
{
	SDL_Event ev=*event;
	switch (ev.type)
	{
		case SDL_MOUSEMOTION:
			ev.motion.x-=decX;
			ev.motion.y-=decY;
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			ev.button.x-=decX;
			ev.button.y-=decY;
			break;
		default:
			break;
	}
	dispatchEvents(&ev);
}


InGameSaveScreen::InGameSaveScreen()
:InGameScreen(300, 275)
{
	filename=NULL;
}

void InGameSaveScreen::onAction(Widget *source, Action action, int par1, int par2)
{

}

void InGameSaveScreen::onSDLEvent(SDL_Event *event)
{

}

void InGameSaveScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 255);
}


InGameMainScreen::InGameMainScreen()
:InGameScreen(300, 275)
{
	addWidget(new TextButton(10, 10, 280, 35, NULL, -1, -1, &globalContainer.menuFont, globalContainer.texts.getString("[load game]"), 0));
	addWidget(new TextButton(10, 50, 280, 35, NULL, -1, -1, &globalContainer.menuFont, globalContainer.texts.getString("[save game]"), 1));
	addWidget(new TextButton(10, 90, 280, 35, NULL, -1, -1, &globalContainer.menuFont, globalContainer.texts.getString("[options]"), 2));
	addWidget(new TextButton(10, 130, 280, 35, NULL, -1, -1, &globalContainer.menuFont, globalContainer.texts.getString("[alliances]"), 3));
	addWidget(new TextButton(10, 180, 280, 35, NULL, -1, -1, &globalContainer.menuFont, globalContainer.texts.getString("[return to game]"), 4));
	addWidget(new TextButton(10, 230, 280, 35, NULL, -1, -1, &globalContainer.menuFont, globalContainer.texts.getString("[quit the game]"), 5));
}

void InGameMainScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
		endValue=par1;
}

void InGameMainScreen::onSDLEvent(SDL_Event *event)
{

}

void InGameMainScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 255);
}
