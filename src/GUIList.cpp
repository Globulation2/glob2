/*
 * GAG GUI list file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GUIList.h"

List::List(int x, int y, int w, int h, const Font *font)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->font=font;
	textHeight=font->getStringHeight(NULL);
}

List::~List()
{
	for (std::vector<char *>::iterator it=strings.begin(); it!=strings.end(); ++it)
	{
		delete (*it);
	}
}

void List::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			int id=event->button.y-y;
			id/=textHeight;
			if (id<(int)strings.size())
				parent->onAction(this, LIST_ELEMENT_SELECTED, id, 0);
		}
	}
}

void List::paint(GraphicContext *gfx)
{
	int nextSize=textHeight;
	int yPos=y;
	int i=0;
	while (textHeight<h)
	{
		gfx->drawString(x, yPos, font, strings[i]);
		nextSize+=textHeight;
		i++;
	}
}

void List::addText(const char *text, int pos)
{
	if ((pos>=0) && (pos<(int)strings.size()))
	{
		int textLength=strlen(text);
		char *newText=new char[textLength+1];
		strcpy(newText, text);
		strings.insert(strings.begin()+pos, newText);
	}
}

void List::addText(const char *text)
{
	int textLength=strlen(text);
	char *newText=new char[textLength+1];
	strcpy(newText, text);
	strings.push_back(newText);
}

void List::removeText(int pos)
{
	if ((pos>=0) && (pos<(int)strings.size()))
	{
		char *text=strings[pos];
		delete[] text;
		strings.erase(strings.begin()+pos);
	}
}
