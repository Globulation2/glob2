/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __SCRIPT_EDITOR_SCREEN_H
#define __SCRIPT_EDITOR_SCREEN_H

#include "GameGUIDialog.h"
namespace GAGGUI
{
	class TextArea;
	class Text;
}
class Game;
class Mapscript;

class ScriptEditorScreen:public OverlayScreen
{
public:
	enum
	{
		OK = 0,
		CANCEL = 1,
		COMPILE = 2,
		LOAD,
		SAVE,
	};
	
protected:
	TextArea *editor;
	Text *compilationResult;
	Mapscript *mapScript;
	Game *game;
	
protected:
	bool testCompile(void);
	
public:
	ScriptEditorScreen(Mapscript *mapScript, Game *game);
	virtual ~ScriptEditorScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);

private:
	void loadSave(bool isLoad);
};

#endif
