/*
 * GAG GUI button file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GUIBUTTON_H
#define __GUIBUTTON_H

#include "GUIBase.h"
#include "Sprite.h"

class Button: public Widget
{
public:
	Button(int x, int y, int w, int h, GraphicArchive *arch, int standardId, int highlightID, int returnCode);
	virtual ~Button() { }

	virtual void onTimer(Uint32 tick);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(GraphicContext *gfx);
	virtual void repaint(void);

protected:
	int x, y, w, h;
	GraphicArchive *arch;
	int standardId, highlightID, returnCode;
	bool highlighted;
	GraphicContext *gfx;
};

class TextButton:public Button
{
public:
	TextButton(int x, int y, int w, int h, GraphicArchive *arch, int standardId, int highlightID, const Font *font, const char *text, int returnCode);
	virtual ~TextButton() { delete[] text; }

	virtual void paint(GraphicContext *gfx);
	virtual void repaint(void);

	void setText(const char *text);

protected:
	char *text;
	const Font *font;
	int decX, decY;
};

#endif 
