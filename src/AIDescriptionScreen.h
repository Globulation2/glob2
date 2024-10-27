/*
  Copyright (C) 2008 Bradley Arenault

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

#ifndef AIDescriptionScreen_h
#define AIDescriptionScreen_h

#include "Glob2Screen.h"

namespace GAGGUI
{
	class TextButton;
	class TextArea;
	class List;
	class Text;
};

///This screen shows descriptions for the various types of AI
class AIDescriptionScreen : public Glob2Screen
{
public:
	///This shows descriptions for the various types of AI
	AIDescriptionScreen();
	
	virtual void onAction(Widget *source, Action action, int par1, int par2);

	enum
	{
		OK,
	};	

private:
	TextButton* ok;
	TextArea *description;
	List *aiList;
	Text *title;
};

#endif
