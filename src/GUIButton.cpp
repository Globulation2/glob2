/*
 * GAG GUI button file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GUIButton.h"

Button::Button(int x, int y, int w, int h, GraphicArchive *arch, int standardId, int highlightID, int returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->arch=arch;
	this->standardId=standardId;
	this->highlightID=highlightID;
	this->returnCode=returnCode;
	highlighted=false;
}

void Button::onTimer(Uint32 tick)
{
}

void Button::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_MOUSEMOTION)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			if (!highlighted)
			{
				highlighted=true;
				assert(gfx);
				if (highlightID>=0)
					gfx->drawSprite(arch->getSprite(highlightID), x, y);
 				else
					parent->paint(x, y, w, h);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, BUTTON_GOT_MOUSEOVER, returnCode, 0);
			}
		}
		else
		{
			if (highlighted)
			{
				highlighted=false;
				assert(gfx);
				if (standardId>=0)
					gfx->drawSprite(arch->getSprite(standardId), x, y);
				else
					parent->paint(x, y, w, h);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, BUTTON_LOST_MOUSEOVER, returnCode, 0);
			}
		}
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
}

void Button::paint(GraphicContext *gfx)
{
	this->gfx=gfx;
	if (standardId>=0)
		gfx->drawSprite(arch->getSprite(standardId), x, y);
	highlighted=false;
}

