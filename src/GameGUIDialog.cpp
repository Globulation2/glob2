/*
 * Globulation 2 game dialog
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GameGUIDialog.h"
#include "GlobalContainer.h"



InGameScreen::InGameScreen(int w, int h)
{
	gfxCtx=globalContainer->gfx->createDrawableSurface();
	gfxCtx->setRes(w, h);
	gfxCtx->setAlpha(false, 128);
	decX=(globalContainer->gfx->getW()-w)>>1;
	decY=(globalContainer->gfx->getH()-h)>>1;
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

void InGameScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 255);
}

//! Save screen
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


//! Main menu screen
InGameMainScreen::InGameMainScreen()
:InGameScreen(300, 275)
{
	addWidget(new TextButton(10, 10, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[load game]"), 0));
	addWidget(new TextButton(10, 50, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[save game]"), 1));
	addWidget(new TextButton(10, 90, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[options]"), 2));
	addWidget(new TextButton(10, 130, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[alliances]"), 3));
	addWidget(new TextButton(10, 180, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[return to game]"), 4));
	addWidget(new TextButton(10, 230, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit the game]"), 5));
}

void InGameMainScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
		endValue=par1;
}

void InGameMainScreen::onSDLEvent(SDL_Event *event)
{
	if ((event->type==SDL_KEYDOWN) && (event->key.keysym.sym==SDLK_ESCAPE))
		endValue=4;
}

//! Alliance screen
InGameAlliance8Screen::InGameAlliance8Screen()
:InGameScreen(300, 295)
{
	{
		for (int i=0; i<8; i++)
		{
			allied[i]=new OnOffButton(230, 40+i*25, 20, 20, false, i);
			addWidget(allied[i]);
			chat[i]=new OnOffButton(270, 40+i*25, 20, 20, false, 10+i);
			addWidget(chat[i]);
		}
	}
	addWidget(new TextButton(10, 250, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), 30));
	firstPaint=true;
}

void InGameAlliance8Screen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
		endValue=par1;
}

void InGameAlliance8Screen::onSDLEvent(SDL_Event *event)
{
	if ((event->type==SDL_KEYDOWN) && (event->key.keysym.sym==SDLK_ESCAPE))
		endValue=30;
}

void InGameAlliance8Screen::paint(int x, int y, int w, int h)
{
	InGameScreen::paint(x, y, w, h);
	if (firstPaint)
	{
		gfxCtx->drawString(231, 10, globalContainer->menuFont, "A");
		gfxCtx->drawString(272, 10, globalContainer->menuFont, "C");
		{
			for (int i=0; i<8; i++)
			{
				gfxCtx->drawString(10, 40+i*25, globalContainer->menuFont, names[i]);
			}
		}
		firstPaint=false;
	}
}
