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
    			repaint();
				parent->onAction(this, BUTTON_GOT_MOUSEOVER, returnCode, 0);
			}
		}
		else
		{
			if (highlighted)
			{
				highlighted=false;
				assert(gfx);
				repaint();
				parent->onAction(this, BUTTON_LOST_MOUSEOVER, returnCode, 0);
			}
		}
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
}

void Button::repaint(void)
{
	if (highlighted)
	{
		if (highlightID>=0)
			gfx->drawSprite(arch->getSprite(highlightID), x, y);
		else
			parent->paint(x, y, w, h);
	}
	else
	{
		if (standardId>=0)
			gfx->drawSprite(arch->getSprite(standardId), x, y);
		else
			parent->paint(x, y, w, h);
	}
	parent->addUpdateRect(x, y, w, h);
}

void Button::paint(GraphicContext *gfx)
{
	this->gfx=gfx;
	if (standardId>=0)
		gfx->drawSprite(arch->getSprite(standardId), x, y);
	highlighted=false;
}

TextButton::TextButton(int x, int y, int w, int h, GraphicArchive *arch, int standardId, int highlightID, const Font *font, const char *text, int returnCode)
:Button(x, y, w, h, arch, standardId, highlightID, returnCode)
{
	this->font=font;
	setText(text);
}

void TextButton::paint(GraphicContext *gfx)
{
	Button::paint(gfx);
	gfx->drawString(x+decX, y+decY, font, text);
	gfx->drawRect(x, y, w, h, 180, 180, 180);
}

void TextButton::setText(const char *text)
{
	int textLength=strlen(text);
	this->text=new char[textLength+1];
	strcpy(this->text, text);
	decX=(w-font->getStringWidth(text))>>1;
	decY=(h-font->getStringHeight(text))>>1;
}

void TextButton::repaint(void)
{
	Button::repaint();
	parent->paint(x, y, w, h);
	gfx->drawString(x+decX, y+decY, font, text);
	if (highlighted)
	{
		gfx->drawRect(x+1, y+1, w-2, h-2, 255, 255, 255);
		gfx->drawRect(x, y, w, h, 255, 255, 255);
	}
	else
		gfx->drawRect(x, y, w, h, 180, 180, 180);
}

