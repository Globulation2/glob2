/*
 * GAG GUI text input file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GUITEXTINPUT_H
#define __GUITEXTINPUT_H

#include "GUIBase.h"

class TextInput: public Widget
{
public:
	TextInput(int x, int y, int w, int h, Font *font, const char *text, bool activated);
	virtual ~TextInput() { }

	virtual void onTimer(Uint32 tick);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(DrawableSurface *gfx);

protected:
	int x, y, w, h;
	Font *font;
	DrawableSurface *gfx;
	int cursPos;
	
public:
	enum {
		MAX_TEXT_SIZE=256
	};
	char text[MAX_TEXT_SIZE];
	
	bool activated;
};

#endif 
