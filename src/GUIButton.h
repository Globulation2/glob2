/*
 * GAG GUI button file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GUIBUTTON_H
#define __GUIBUTTON_H

#include "GUIBase.h"

class Button: public Widget
{
public:
	Button(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, int returnCode);
	virtual ~Button() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(DrawableSurface *gfx);
	virtual void repaint(void);

protected:
	int x, y, w, h;
	Sprite *arch;
	int standardId, highlightID, returnCode;
	bool highlighted;
	DrawableSurface *gfx;
};

class TextButton:public Button
{
public:
	TextButton(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, const Font *font, const char *text, int returnCode);
	virtual ~TextButton() { delete[] text; }

	virtual void paint(DrawableSurface *gfx);
	virtual void repaint(void);

	void setText(const char *text);

protected:
	char *text;
	const Font *font;
	int decX, decY;
};

class OnOffButton:public Widget
{
public:
	OnOffButton(int x, int y, int w, int h, bool startState, int returnCode);
	virtual ~OnOffButton() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(DrawableSurface *gfx);

protected:
	virtual void internalPaint(void);
	virtual void repaint(void);

protected:
	int x, y, w, h;
	bool state;
	int returnCode;
	bool highlighted;
	DrawableSurface *gfx;
};


#endif 
