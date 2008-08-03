/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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

#include <GUIBase.h>
namespace GAGGUI
{
	class TextArea;
	class Text;
	class TextButton;
	class TextInput;
}
using namespace GAGGUI;
class Game;
class MapScript;

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
		TAB_SCRIPT = 10,
		TAB_OBJECTIVES = 11,
		TAB_BRIEFING = 12,
		TAB_HINTS = 13,
		TAB_PRIMARY = 14,
		TAB_SECONDARY = 15,
	};
	
protected:
	TextArea *scriptEditor;
	Text *compilationResult;
	MapScript *mapScript;
	Game *game;
	Text *mode;
	Text *cursorPosition;
	TextInput *primaryObjectives[8];
	TextInput *secondaryObjectives[8];
	Text *primaryObjectiveLabels[8];
	Text *secondaryObjectiveLabels[8];
	TextArea* missionBriefing;
	TextInput *hints[8];
	Text *hintLabels[8];
	
	std::vector<Widget*> scriptWidgets;
	std::vector<Widget*> objectivesWidgets;
	std::vector<Widget*> briefingWidgets;
	std::vector<Widget*> hintWidgets;
	
	
	bool changeTabAgain;
protected:
	bool testCompile(void);
	
public:
	ScriptEditorScreen(MapScript *mapScript, Game *game);
	virtual ~ScriptEditorScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void onTimer(Uint32 tick);

private:
	void loadSave(bool isLoad, const char *dir, const char *ext);
};

#endif
