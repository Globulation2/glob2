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

protected:
	int x, y, w, h;
	GraphicArchive *arch;
	int standardId, highlightID, returnCode;
	bool highlighted;
	GraphicContext *gfx;
};

#endif 
