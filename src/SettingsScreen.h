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

#ifndef __SETTINGSSCREEN_H
#define __SETTINGSSCREEN_H

#include <GUIBase.h>
using namespace GAGGUI;
#include <string>

namespace GAGGUI
{
	class List;
	class TextInput;
	class TextButton;
	class OnOffButton;
	class Text;
	class Selector;
}

class SettingsScreen:public Screen
{
public:
	enum
	{
		OK = 1,
		CANCEL = 2,
		FULLSCREEN = 3,
		HWACCLEL = 4,
		DBLBUFF = 5,
		LOWQUALITY = 6,
		CUSTOMCUR = 7,
	};
private:
	List *languageList;
	List *modeList;
	List *rendererList;
	List *depthList;
	TextInput *userName;
	int oldLanguage, oldScreenW, oldScreenH, oldScreenDepth, oldScreenFlags, oldGraphicType, oldOptionFlags, oldMusicVol;
	
	TextButton *ok, *cancel;
	OnOffButton *fullscreen, *hwaccel, *dblbuff, *lowquality, *customcur;
	Selector *musicVol;
	Text *title, *language, *display, *usernameText, *audio;
	Text *fullscreenText, *hwaccelText, *dblbuffText, *lowqualityText, *customcurText, *musicVolText;
	Text *actDisplay;
	Text *rebootWarning;

	bool gfxAltered;
	
	//! If GL is enabled, hide useless options
	void setVisibilityFromGraphicType(void);
	//! reset res and redraw everything
	void updateGfxCtx(void);
	//! Return a string representing the actual display mode
	std::string actDisplayModeToString(void);

public:
	SettingsScreen();
	virtual ~SettingsScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);
	static int menu(void);
};

#endif
