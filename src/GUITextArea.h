/*
 * GAG GUI button file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GUITEXTAREA_H
#define __GUITEXTAREA_H

#include "GUIBase.h"

class TextArea:public Widget
{
public:
	TextArea(int x, int y, int w, int h, const Font *font);
	virtual ~TextArea();

	virtual void paint(DrawableSurface *gfx);
	virtual void onSDLEvent(SDL_Event *event);

	virtual void setText(const char *text, int ap=-1);
	virtual void addText(const char *text);
	virtual void scrollDown(void);
	virtual void scrollUp(void);
	virtual void scrollToBottom(void);

protected:
	virtual void internalPaint(void);
	virtual void repaint(void);

protected:
	int x, y, w, h;
	DrawableSurface *gfx;
	const Font *font;
	char *textBuffer;
	unsigned int textBufferLength;
	unsigned int areaHeight;
	unsigned int areaPos;
	unsigned int charHeight;
	vector <unsigned int> lines;
};

#endif