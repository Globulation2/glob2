/*
 * GAG GUI text input file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GUITextInput.h"

TextInput::TextInput(int x, int y, int w, int h, Font *font, const char *text, bool activated)
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

	this->activated=activated;
}

void TextInput::onTimer(Uint32 tick)
{
}

#ifdef WIN32
#defien strncpy _strncpy
#endif

void TextInput::setText(const char *newText)
{
	strncpy(this->text, newText, MAX_TEXT_SIZE);
	cursPos=0;
	repaint(gfx);
	parent->addUpdateRect(x, y, w, h);
	parent->onAction(this, TEXT_SET, 0, 0);
}

void TextInput::onSDLEvent(SDL_Event *event)
{
	// TODO : handle click in the box.
	
	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			if (activated)
			{
				// we move cursor:
				int dx=event->motion.x-x-1;
				
				char textBeforeCurs[MAX_TEXT_SIZE];
				strncpy(textBeforeCurs, text, MAX_TEXT_SIZE);
				while(textBeforeCurs[cursPos]&&(cursPos<MAX_TEXT_SIZE))
					cursPos++;
				while((font->getStringWidth(textBeforeCurs)>dx)&&(cursPos>0))
					textBeforeCurs[--cursPos]=0;
				
				assert(gfx);
				repaint(gfx);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
			}
			else
			{
				assert(gfx);
				repaint(gfx);
				activated=true;
				parent->onAction(this, TEXT_ACTIVATED, 0, 0);
			}
		}
	}
	
	if (activated && event->type==SDL_KEYDOWN)
	{
		char c=event->key.keysym.unicode;
		SDLKey sym=event->key.keysym.sym;

		if (font->printable(c))
		{
			int l=strlen(text);
			if (l<MAX_TEXT_SIZE)
			{
				memmove( &(text[cursPos+1]), &(text[cursPos]), l-cursPos);
				
				text[cursPos]=c;
				cursPos++;
		
				assert(gfx);
				repaint(gfx);

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
				repaint(gfx);
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
				repaint(gfx);
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
				repaint(gfx);
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
				memmove( &(text[cursPos]), &(text[cursPos+1]), l-cursPos);

				assert(gfx);
				repaint(gfx);
				parent->addUpdateRect(x, y, w, h);
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
			}
		}
		else if (sym==SDLK_HOME)
		{
			cursPos=0;
			assert(gfx);
			repaint(gfx);
			parent->addUpdateRect(x, y, w, h);
		}
		else if (sym==SDLK_END)
		{
			cursPos=strlen(text);
			assert(gfx);
			repaint(gfx);
			parent->addUpdateRect(x, y, w, h);
		}
		else if (sym==SDLK_RETURN)
		{
			parent->onAction(this, TEXT_VALIDATED, 0, 0);
		}
		else
		{
			//printf("unused c=%c(%d).\n", c, c);
		}

	}
}

void TextInput::paint(DrawableSurface *gfx)
{
	this->gfx=gfx;

	static const int r= 180;
	static const int g= 180;
	static const int b= 180;
	gfx->drawRect(x, y, w, h, r, g, b);
	gfx->drawString(x+2, y+2, font, text);

	// we draw the cursor:
	if(activated)
	{
		char textBeforeCurs[MAX_TEXT_SIZE];
		strncpy(textBeforeCurs, text, MAX_TEXT_SIZE);
		textBeforeCurs[cursPos]=0;
		int wbc=font->getStringWidth(textBeforeCurs);
		int hbc=font->getStringHeight(textBeforeCurs);
		gfx->drawVertLine(x+2+wbc, y+2 , hbc, r, g, b);
	}
}

void TextInput::repaint(DrawableSurface *gfx)
{
	parent->paint(x, y, w, h);
	paint(gfx);
}

