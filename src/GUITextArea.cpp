/*
 * Globulation 2 Text Area
 * (c) 2002 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GUITextArea.h"

TextArea::TextArea(int x, int y, int w, int h, const Font *font)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->font=font;
	textBuffer=NULL;
	gfx=NULL;
	assert(font);
	charHeight=font->getStringHeight(NULL);
	assert(charHeight);
	areaHeight=(h-8)/charHeight;
	areaPos=0;
	textBufferLength=0;
}

TextArea::~TextArea(void)
{
	if (textBuffer)
		free(textBuffer);
}

void TextArea::internalPaint(void)
{
	assert(gfx);
	gfx->drawRect(x, y, w, h, 180, 180, 180);
	if (textBuffer)
	{
		for (unsigned i=0;(i<areaHeight)&&((signed)i<(signed)(lines.size()-areaPos));i++)
		{
			assert(i+areaPos<lines.size());
			gfx->drawString(x+4, y+4+(charHeight*i), font, (textBuffer+lines[i+areaPos]));
		}
	}
	/*if (areaPos>0)
	{
		DisplayManager::drawGraphicContent(theme->scrollUp,size.w+theme->xScroll,theme->y1Scroll);
	}
	if (areaPos+areaHeight<lines.size()-1)
	{
		DisplayManager::drawGraphicContent(theme->scrollDown,size.w+theme->xScroll,size.h+theme->y2Scroll);
	}*/
}

void TextArea::paint(DrawableSurface *gfx)
{
	this->gfx=gfx;
	internalPaint();
}

void TextArea::repaint(void)
{
	parent->paint(x, y, w, h);
	internalPaint();
	parent->addUpdateRect(x, y, w, h);
}

void TextArea::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_KEYDOWN)
	{
		switch (event->key.keysym.sym)
		{
		case SDLK_UP:
			{
				scrollUp();
				//return true;
			}
			break;
		case SDLK_DOWN:
			{
				scrollDown();
				//return true;
			}
			break;
		default:
			break;
		}
	}
	/*else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if ((event->button.x>size.x+size.w+theme->xScroll) &&
			(event->button.x<size.x+size.w+theme->xScroll+theme->scrollUp->getW()) &&
			(event->button.y>size.y+theme->y1Scroll) &&
			(event->button.y<size.y+theme->y1Scroll+theme->scrollUp->getH()))
		{
			if (areaPos>0)
			{
				areaPos--;
			}
			return true;
		}
		else if ((event->button.x>size.x+size.w+theme->xScroll) &&
				(event->button.x<size.x+size.w+theme->xScroll+theme->scrollUp->getW()) &&
				(event->button.y>size.y+size.h+theme->y2Scroll) &&
				(event->button.y<size.y+size.h+theme->y2Scroll+theme->scrollUp->getH()))
		{
			if (areaPos<lines.size()-areaHeight-1)
			{
				areaPos++;
			}
			return true;
		}
	}*/
	//return false;
}


void TextArea::setText(const char *text, int ap)
{
	if (textBuffer)
	{
		free(textBuffer);
		lines.clear();
	}
	textBufferLength=strlen(text);
	textBuffer=(char *)malloc(textBufferLength+1);
	unsigned pos=0;

	char temp[1024];
	int temppos;
	lines.push_back(0);
	while (pos<textBufferLength)
	{
		temppos=0;
		temp[temppos]=0;
		while ((font->getStringHeight(temp)<w-8)&&(pos<textBufferLength)&&(text[pos]!='\n'))
		{
			textBuffer[pos]=text[pos];
			temp[temppos]=text[pos];
			temppos++;
			pos++;
			temp[temppos]=0;
		}
		if (pos<textBufferLength)
		{
			if (text[pos]=='\n')
			{
				textBuffer[pos]='\n';
				pos++;
			}
			lines.push_back(pos);
		}
		else
		{
			textBuffer[pos]=0;
		}
	}
	textBuffer[pos]=0;
	if (ap==-1)
		areaPos=0;
	else
		areaPos=ap;
	repaint();
}

void TextArea::addText(const char *text)
{
	char *temp;
	int ts=strlen(text);

	temp=(char *)malloc(textBufferLength+ts+1);

	memcpy(temp,textBuffer,textBufferLength);
	memcpy(&(temp[textBufferLength]),text,ts);

	temp[textBufferLength+ts]=0;

	setText(temp,areaPos);
}

void TextArea::scrollDown(void)
{
	if (lines.size()>=areaHeight)
	{
		if (areaPos<lines.size()-areaHeight-1)
		{
			areaPos++;
		}
	}
	repaint();
}

void TextArea::scrollUp(void)
{
	if (lines.size()>=areaHeight)
	{
		if (areaPos>0)
		{
			areaPos--;
		}
	}
	repaint();
}

void TextArea::scrollToBottom(void)
{
	while ((signed)areaPos<(signed)lines.size()-(signed)areaHeight-1)
	{
		areaPos++;
	}
	repaint();
}
