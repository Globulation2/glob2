/*
 * GAG GUI list file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GUILIST_H
#define __GUILIST_H

#include "GUIBase.h"
#include <vector>

class List: public Widget
{
public:
	List(int x, int y, int w, int h, const Font *font);
	virtual ~List();

	virtual void onTimer(Uint32 tick) { }
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(DrawableSurface *gfx);

	void addText(const char *text, int pos);
	void addText(const char *text);
	void removeText(int pos);
	void clear(void);

protected:
	virtual void internalPaint(void);
	virtual void repaint(void);

protected:
	DrawableSurface *gfx;
	int x, y, w, h;
	int textHeight;
	const Font *font;
	std::vector<char *> strings;
};

#endif

