/*
 * GAG GUI text input file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GUITextInput.h"

TextInput::TextInput(int x, int y, int w, int h, Font *font, const char *text)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;

	this->font=font;
	this->gfx=NULL;

	if (text)
		strncpy(this->text, text, MAX_TEXT_SIZE);
	else
		this->text[0]=0;

	this->text[MAX_TEXT_SIZE-1]=0;

	cursPos=strlen(text);
}

void TextInput::onTimer(Uint32 tick)
{
}

void TextInput::onSDLEvent(SDL_Event *event)
{
	// TODO : handle click in the box.
	
	if (event->type==SDL_KEYDOWN)
	{
		char c=event->key.keysym.unicode;
		SDLKey sym=event->key.keysym.sym;

		if (font->printable(c))
		{
			int l=strlen(text);
			if (l<MAX_TEXT_SIZE)
			{
				memmove( &(text[cursPos+1]), &(text[cursPos]), l-cursPos);
				
				//printf("printable: l=%d, cursPos=%d, c=%c(%d) \n", l, cursPos, c, c);

				text[cursPos]=c;
				cursPos++;
		
				assert(gfx);
				paint(gfx);
		
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
			}
		}
		else if (sym==SDLK_RIGHT)
		{
			int l=strlen(text);
			if (cursPos<l)
			{
				cursPos++;
				assert(gfx);
				paint(gfx);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
			}
		}
		else if (sym==SDLK_LEFT)
		{
			if (cursPos>0)
			{
				cursPos--;
				assert(gfx);
				paint(gfx);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
			}
		}
		else if (sym==SDLK_BACKSPACE)
		{
			if (cursPos>0)
			{
				int l=strlen(text);
				
				memmove( &(text[cursPos-1]), &(text[cursPos]), l-cursPos+1);
				
				//printf("clear: l=%d, cursPos=%d, text=%s \n", l, cursPos, text);
				cursPos--;
		
				assert(gfx);
				paint(gfx);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);

				//printf("now: l=%d, cursPos=%d, text=%s \n", strlen(text), cursPos, text);
			}
			
		}
		else if (sym==SDLK_DELETE)
		{
			int l=strlen(text);
			if (cursPos<l)
			{
				memmove( &(text[cursPos]), &(text[cursPos+1]), l-cursPos+1);

				assert(gfx);
				paint(gfx);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
			}
		}
		else
		{
			//printf("unused c=%c(%d).\n", c, c);
		}
		
	}
}

void TextInput::paint(GraphicContext *gfx)
{
	this->gfx=gfx;
	
	gfx->drawFilledRect(x, y, w, h, 0, 0, 0);

	static const int r= 40;
	static const int g=240;
	static const int b= 80;
	gfx->drawVertLine(x  , y  , h, r, g, b);
	gfx->drawHorzLine(x  , y  , w, r, g, b);
	gfx->drawVertLine(x+w, y  , h, r, g, b);
	gfx->drawHorzLine(x  , y+h, w, r, g, b);
	
	gfx->drawString(x+2, y+2, font, text);
	
	char textBeforeCurs[MAX_TEXT_SIZE];
	strncpy(textBeforeCurs, text, MAX_TEXT_SIZE);
	textBeforeCurs[cursPos]=0;
	int wbc=font->getStringWidth(textBeforeCurs);
	int hbc=font->getStringHeight(textBeforeCurs);
	gfx->drawVertLine(x+2+wbc, y+2 , hbc, r, g, b);
	
}

